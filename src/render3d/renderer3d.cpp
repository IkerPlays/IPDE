#include "render3d/renderer3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace ipde {

/* Color helpers */

static inline float srgb_to_linear(float c) {
	return c * c; // Gamma 2.0 approximation (fast).
}

Vec3 argb_to_linear(uint32_t p_argb) {
	return Vec3(srgb_to_linear(((p_argb >> 16) & 0xFF) / 255.0f), srgb_to_linear(((p_argb >> 8) & 0xFF) / 255.0f), srgb_to_linear((p_argb & 0xFF) / 255.0f));
}

static inline uint32_t to_byte(float c) {
	c = c <= 0.0f ? 0.0f : (c >= 1.0f ? 1.0f : c);
	return (uint32_t)(std::sqrt(c) * 255.0f + 0.5f);
}

uint32_t linear_to_argb(const Vec3 &p_color) {
	return 0xFF000000u | (to_byte(p_color.x) << 16) | (to_byte(p_color.y) << 8) | to_byte(p_color.z);
}

static inline Vec3 tonemap(const Vec3 &c) {
	// ACES approximation (Narkowicz).
	auto f = [](float x) {
		const float a = 2.51f, b = 0.03f, cc = 2.43f, d = 0.59f, e = 0.14f;
		const float v = (x * (a * x + b)) / (x * (cc * x + d) + e);
		return v < 0 ? 0 : (v > 1 ? 1 : v);
	};
	return Vec3(f(c.x), f(c.y), f(c.z));
}

Vec3 Texture3D::sample(float p_u, float p_v) const {
	if (width <= 0 || height <= 0) {
		return Vec3(1, 1, 1);
	}
	// Bilinear with wrapping.
	float fx = p_u * width - 0.5f;
	float fy = p_v * height - 0.5f;
	const float flx = std::floor(fx);
	const float fly = std::floor(fy);
	const float tx = fx - flx;
	const float ty = fy - fly;
	auto wrap = [](int v, int n) {
		v %= n;
		return v < 0 ? v + n : v;
	};
	const int x0 = wrap((int)flx, width), x1 = wrap((int)flx + 1, width);
	const int y0 = wrap((int)fly, height), y1 = wrap((int)fly + 1, height);
	const Vec3 &a = texels[(size_t)y0 * width + x0];
	const Vec3 &b = texels[(size_t)y0 * width + x1];
	const Vec3 &c = texels[(size_t)y1 * width + x0];
	const Vec3 &d = texels[(size_t)y1 * width + x1];
	return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
}

/* Renderer */

Renderer3D::Renderer3D() {}
Renderer3D::~Renderer3D() {}

void Renderer3D::resize(int p_width, int p_height) {
	p_width = std::max(1, p_width);
	p_height = std::max(1, p_height);
	if (p_width == width && p_height == height) {
		return;
	}
	width = p_width;
	height = p_height;
	color.assign((size_t)width * height, 0xFF000000u);
	depth.assign((size_t)width * height, 1.0f);
	ids.assign((size_t)width * height, 0);
}

void Renderer3D::begin(const Camera3D &p_camera, const Environment3D &p_env, const std::vector<PointLight3D> &p_lights) {
	camera = p_camera;
	env = p_env;
	lights = p_lights;
	view = Mat4::look_at(camera.position, camera.target, Vec3(0, 1, 0));
	proj = Mat4::perspective(camera.fov * DEG2RAD, (float)width / (float)height, camera.znear, camera.zfar);
	view_proj = proj * view;
	inv_view_proj = view_proj.inverse();
	sun_dir_to_light = (-env.sun_direction).normalized();
	triangles.clear();
	lines.clear();
	materials.clear();
	shadow_casters.clear();
	shadow_valid = false;
}

void Renderer3D::draw_mesh(const Mesh &p_mesh, const Mat4 &p_model, const Material3D &p_material, uint32_t p_object_id) {
	materials.push_back(std::make_unique<Material3D>(p_material));
	const Material3D *mat = materials.back().get();
	if (p_material.cast_shadows && !p_material.unshaded) {
		shadow_casters.push_back({ &p_mesh, p_model });
	}
	const Mat4 nmat = p_model.normal_matrix();
	const Mat4 mvp = view_proj * p_model;

	std::vector<ShadeVertex> verts(p_mesh.positions.size());
	for (size_t i = 0; i < p_mesh.positions.size(); i++) {
		const Vec3 &p = p_mesh.positions[i];
		verts[i].clip = mvp * Vec4(p.x, p.y, p.z, 1.0f);
		verts[i].world = p_model.transform_point(p);
		verts[i].normal = i < p_mesh.normals.size() ? nmat.transform_dir(p_mesh.normals[i]).normalized() : Vec3(0, 1, 0);
		verts[i].uv = i < p_mesh.uvs.size() ? p_mesh.uvs[i] : Vec2();
	}
	for (size_t i = 0; i + 2 < p_mesh.indices.size(); i += 3) {
		const ShadeVertex tri[3] = { verts[p_mesh.indices[i]], verts[p_mesh.indices[i + 1]], verts[p_mesh.indices[i + 2]] };
		// Trivial rejection when all vertices are outside the same frustum plane.
		bool outside = false;
		for (int axis = 0; axis < 3 && !outside; axis++) {
			auto comp = [&](const Vec4 &c) { return axis == 0 ? c.x : (axis == 1 ? c.y : c.z); };
			if (comp(tri[0].clip) > tri[0].clip.w && comp(tri[1].clip) > tri[1].clip.w && comp(tri[2].clip) > tri[2].clip.w) {
				outside = true;
			}
			if (comp(tri[0].clip) < -tri[0].clip.w && comp(tri[1].clip) < -tri[1].clip.w && comp(tri[2].clip) < -tri[2].clip.w) {
				outside = true;
			}
		}
		if (!outside) {
			_clip_and_queue(tri, mat, p_object_id);
		}
	}
}

void Renderer3D::_clip_and_queue(const ShadeVertex *p_verts, const Material3D *p_material, uint32_t p_id) {
	// Clip against the near plane (z >= -w) with Sutherland-Hodgman.
	ShadeVertex in[4];
	int count = 0;
	auto dist = [](const ShadeVertex &v) { return v.clip.z + v.clip.w; };
	for (int i = 0; i < 3; i++) {
		const ShadeVertex &a = p_verts[i];
		const ShadeVertex &b = p_verts[(i + 1) % 3];
		const float da = dist(a);
		const float db = dist(b);
		if (da >= 0) {
			in[count++] = a;
		}
		if ((da >= 0) != (db >= 0)) {
			const float t = da / (da - db);
			ShadeVertex v;
			v.clip = Vec4(a.clip.x + (b.clip.x - a.clip.x) * t, a.clip.y + (b.clip.y - a.clip.y) * t, a.clip.z + (b.clip.z - a.clip.z) * t, a.clip.w + (b.clip.w - a.clip.w) * t);
			v.world = lerp(a.world, b.world, t);
			v.normal = lerp(a.normal, b.normal, t);
			v.uv = Vec2{ a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t };
			in[count++] = v;
		}
	}
	if (count < 3) {
		return;
	}

	ScreenVertex sv[4];
	for (int i = 0; i < count; i++) {
		const float w = std::max(in[i].clip.w, 1e-6f);
		const float iw = 1.0f / w;
		sv[i].x = (in[i].clip.x * iw * 0.5f + 0.5f) * width;
		sv[i].y = (1.0f - (in[i].clip.y * iw * 0.5f + 0.5f)) * height;
		sv[i].z = in[i].clip.z * iw * 0.5f + 0.5f;
		sv[i].inv_w = iw;
		sv[i].world_w = in[i].world * iw;
		sv[i].normal_w = in[i].normal * iw;
		sv[i].uv_w = Vec2{ in[i].uv.x * iw, in[i].uv.y * iw };
	}

	for (int i = 1; i + 1 < count; i++) {
		Triangle t;
		t.v[0] = sv[0];
		t.v[1] = sv[i];
		t.v[2] = sv[i + 1];
		// Screen space has Y down: counter-clockwise (front) triangles have a negative area here.
		const float area = (t.v[1].x - t.v[0].x) * (t.v[2].y - t.v[0].y) - (t.v[1].y - t.v[0].y) * (t.v[2].x - t.v[0].x);
		if (std::fabs(area) < 1e-9f) {
			continue;
		}
		t.front = area < 0;
		if (!t.front && !p_material->double_sided) {
			continue;
		}
		const float min_y = std::min({ t.v[0].y, t.v[1].y, t.v[2].y });
		const float max_y = std::max({ t.v[0].y, t.v[1].y, t.v[2].y });
		if (max_y < 0 || min_y > height) {
			continue;
		}
		const float min_x = std::min({ t.v[0].x, t.v[1].x, t.v[2].x });
		const float max_x = std::max({ t.v[0].x, t.v[1].x, t.v[2].x });
		if (max_x < 0 || min_x > width) {
			continue;
		}
		t.min_y = std::max(0, (int)std::floor(min_y));
		t.max_y = std::min(height, (int)std::ceil(max_y));
		t.material = p_material;
		t.object_id = p_id;
		triangles.push_back(t);
	}
}

void Renderer3D::draw_line(const Vec3 &p_a, const Vec3 &p_b, uint32_t p_color, bool p_depth_test) {
	lines.push_back({ p_a, p_b, p_color, p_depth_test });
}

void Renderer3D::_draw_sky() {
	// Vertical gradient using the view ray of each row (center column).
	const Vec3 top = env.sky_top, hor = env.sky_horizon, gnd = env.ground;
	for (int y = 0; y < height; y++) {
		const float ndc_y = 1.0f - 2.0f * ((float)y + 0.5f) / height;
		const Vec4 pn = inv_view_proj * Vec4(0, ndc_y, -1, 1);
		const Vec4 pf = inv_view_proj * Vec4(0, ndc_y, 1, 1);
		const Vec3 dir = (pf.xyz() / pf.w - pn.xyz() / pn.w).normalized();
		Vec3 c;
		if (dir.y >= 0) {
			c = lerp(hor, top, std::pow(std::min(1.0f, dir.y), 0.55f));
		} else {
			c = lerp(hor, gnd, std::min(1.0f, -dir.y * 3.0f));
		}
		const uint32_t px = linear_to_argb(c * env.exposure);
		uint32_t *row = color.data() + (size_t)y * width;
		std::fill(row, row + width, px);
	}
}

void Renderer3D::_build_shadow_map() {
	if (!env.shadows || shadow_casters.empty()) {
		return;
	}
	// Bounds of all casters.
	Vec3 bmin(1e30f, 1e30f, 1e30f), bmax(-1e30f, -1e30f, -1e30f);
	for (const ShadowCaster &c : shadow_casters) {
		const Vec3 &mn = c.mesh->aabb_min;
		const Vec3 &mx = c.mesh->aabb_max;
		for (int i = 0; i < 8; i++) {
			const Vec3 p = c.model.transform_point(Vec3(i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z));
			bmin = Vec3(std::min(bmin.x, p.x), std::min(bmin.y, p.y), std::min(bmin.z, p.z));
			bmax = Vec3(std::max(bmax.x, p.x), std::max(bmax.y, p.y), std::max(bmax.z, p.z));
		}
	}
	// Limit the covered area around the camera target so big scenes keep detail.
	const Vec3 focus = camera.target;
	const float max_extent = 60.0f;
	bmin = Vec3(std::max(bmin.x, focus.x - max_extent), std::max(bmin.y, focus.y - max_extent), std::max(bmin.z, focus.z - max_extent));
	bmax = Vec3(std::min(bmax.x, focus.x + max_extent), std::min(bmax.y, focus.y + max_extent), std::min(bmax.z, focus.z + max_extent));
	if (bmin.x > bmax.x || bmin.y > bmax.y || bmin.z > bmax.z) {
		return;
	}
	const Vec3 center = (bmin + bmax) * 0.5f;
	const float radius = std::max(0.5f, (bmax - bmin).length() * 0.5f);
	const Vec3 light_dir = env.sun_direction.normalized();
	const Mat4 lview = Mat4::look_at(center - light_dir * radius * 2.0f, center, std::fabs(light_dir.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0));
	const Mat4 lproj = Mat4::orthographic(-radius, radius, -radius, radius, 0.01f, radius * 4.0f);
	shadow_view_proj = lproj * lview;

	const int S = shadow_size;
	shadow_map.assign((size_t)S * S, 1.0f);
	for (const ShadowCaster &c : shadow_casters) {
		const Mat4 mvp = shadow_view_proj * c.model;
		std::vector<Vec3> sp(c.mesh->positions.size());
		for (size_t i = 0; i < sp.size(); i++) {
			const Vec3 &p = c.mesh->positions[i];
			const Vec4 cl = mvp * Vec4(p.x, p.y, p.z, 1.0f);
			sp[i] = Vec3((cl.x * 0.5f + 0.5f) * S, (1.0f - (cl.y * 0.5f + 0.5f)) * S, cl.z * 0.5f + 0.5f);
		}
		for (size_t i = 0; i + 2 < c.mesh->indices.size(); i += 3) {
			const Vec3 &a = sp[c.mesh->indices[i]];
			const Vec3 &b = sp[c.mesh->indices[i + 1]];
			const Vec3 &d = sp[c.mesh->indices[i + 2]];
			const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
			if (std::fabs(area) < 1e-9f) {
				continue;
			}
			const int x0 = std::max(0, (int)std::floor(std::min({ a.x, b.x, d.x })));
			const int x1 = std::min(S - 1, (int)std::ceil(std::max({ a.x, b.x, d.x })));
			const int y0 = std::max(0, (int)std::floor(std::min({ a.y, b.y, d.y })));
			const int y1 = std::min(S - 1, (int)std::ceil(std::max({ a.y, b.y, d.y })));
			const float inv_area = 1.0f / area;
			for (int y = y0; y <= y1; y++) {
				for (int x = x0; x <= x1; x++) {
					const float px = x + 0.5f, py = y + 0.5f;
					float w0 = ((b.x - px) * (d.y - py) - (b.y - py) * (d.x - px)) * inv_area;
					float w1 = ((d.x - px) * (a.y - py) - (d.y - py) * (a.x - px)) * inv_area;
					float w2 = 1.0f - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) {
						continue;
					}
					const float z = a.z * w0 + b.z * w1 + d.z * w2;
					float &dst = shadow_map[(size_t)y * S + x];
					if (z < dst) {
						dst = z;
					}
				}
			}
		}
	}
	shadow_valid = true;
}

float Renderer3D::_shadow_factor(const Vec3 &p_world, const Vec3 &p_normal) const {
	if (!shadow_valid) {
		return 1.0f;
	}
	// Normal offset reduces shadow acne.
	const Vec3 p = p_world + p_normal * 0.04f;
	const Vec4 cl = shadow_view_proj * Vec4(p.x, p.y, p.z, 1.0f);
	const int S = shadow_size;
	const float sx = (cl.x * 0.5f + 0.5f) * S - 0.5f;
	const float sy = (1.0f - (cl.y * 0.5f + 0.5f)) * S - 0.5f;
	const float sz = cl.z * 0.5f + 0.5f - 0.0015f;
	if (sx < 0 || sy < 0 || sx >= S - 1 || sy >= S - 1 || sz > 1.0f) {
		return 1.0f;
	}
	// 2x2 PCF with bilinear weights.
	const int ix = (int)sx, iy = (int)sy;
	const float fx = sx - ix, fy = sy - iy;
	auto lit = [&](int x, int y) { return sz <= shadow_map[(size_t)y * S + x] ? 1.0f : 0.0f; };
	const float a = lit(ix, iy) * (1 - fx) + lit(ix + 1, iy) * fx;
	const float b = lit(ix, iy + 1) * (1 - fx) + lit(ix + 1, iy + 1) * fx;
	return a * (1 - fy) + b * fy;
}

Vec3 Renderer3D::_shade(const Material3D &p_mat, const Vec3 &p_world, Vec3 p_normal, const Vec2 &p_uv, bool p_front) const {
	Vec3 albedo = p_mat.albedo;
	if (p_mat.texture) {
		albedo = albedo * p_mat.texture->sample(p_uv.x * p_mat.uv_scale, p_uv.y * p_mat.uv_scale);
	}
	if (p_mat.unshaded) {
		return albedo + p_mat.emission;
	}
	Vec3 n = p_normal.normalized();
	if (!p_front) {
		n = -n;
	}
	const Vec3 v = (camera.position - p_world).normalized();

	// Hemisphere ambient.
	const float up = n.y * 0.5f + 0.5f;
	Vec3 result = albedo * lerp(env.ground, lerp(env.sky_horizon, env.sky_top, 0.5f), up) * env.ambient * (1.0f - p_mat.metallic * 0.5f);

	const float rough = std::clamp(p_mat.roughness, 0.02f, 1.0f);
	const float shininess = 2.0f + (1.0f - rough) * (1.0f - rough) * 254.0f;
	const float spec_strength = (1.0f - rough) * (1.0f - rough) * 0.9f + 0.04f;
	const Vec3 spec_color = lerp(Vec3(1, 1, 1), albedo, p_mat.metallic);
	const float diffuse_weight = 1.0f - p_mat.metallic * 0.85f;

	// Sun.
	const float ndl = dot(n, sun_dir_to_light);
	if (ndl > 0) {
		const float shadow = _shadow_factor(p_world, n);
		if (shadow > 0) {
			const Vec3 h = (sun_dir_to_light + v).normalized();
			const float spec = std::pow(std::max(0.0f, dot(n, h)), shininess) * spec_strength * (shininess + 8.0f) / 32.0f;
			result += (albedo * (ndl * diffuse_weight) + spec_color * (spec * ndl)) * env.sun_color * (env.sun_energy * shadow);
		}
	}

	// Point lights.
	for (const PointLight3D &l : lights) {
		Vec3 to_light = l.position - p_world;
		const float d2 = to_light.length_squared();
		if (d2 > l.range * l.range) {
			continue;
		}
		const float d = std::sqrt(d2);
		to_light = to_light / std::max(d, 1e-4f);
		const float ndl_p = dot(n, to_light);
		if (ndl_p <= 0) {
			continue;
		}
		float att = 1.0f - d / l.range;
		att = att * att;
		const Vec3 h = (to_light + v).normalized();
		const float spec = std::pow(std::max(0.0f, dot(n, h)), shininess) * spec_strength * (shininess + 8.0f) / 32.0f;
		result += (albedo * (ndl_p * diffuse_weight) + spec_color * (spec * ndl_p)) * l.color * (l.energy * att);
	}

	result += p_mat.emission;
	return result;
}

void Renderer3D::_rasterize_triangle(const Triangle &p_tri, int p_y0, int p_y1) {
	const ScreenVertex &a = p_tri.v[0];
	const ScreenVertex &b = p_tri.v[1];
	const ScreenVertex &c = p_tri.v[2];
	const int y_start = std::max(p_tri.min_y, p_y0);
	const int y_end = std::min(p_tri.max_y, p_y1);
	if (y_start >= y_end) {
		return;
	}
	const int x_start = std::max(0, (int)std::floor(std::min({ a.x, b.x, c.x })));
	const int x_end = std::min(width, (int)std::ceil(std::max({ a.x, b.x, c.x })));
	if (x_start >= x_end) {
		return;
	}
	// Exact edge functions in fixed point (1/16 pixel) with the top-left rule: shared edges
	// between triangles never leave cracks or get drawn twice.
	const int64_t ax = (int64_t)std::lround(a.x * 16.0f), ay = (int64_t)std::lround(a.y * 16.0f);
	const int64_t bx = (int64_t)std::lround(b.x * 16.0f), by = (int64_t)std::lround(b.y * 16.0f);
	const int64_t cx = (int64_t)std::lround(c.x * 16.0f), cy = (int64_t)std::lround(c.y * 16.0f);
	int64_t area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	if (area == 0) {
		return;
	}
	const int64_t sign = area > 0 ? 1 : -1;
	area *= sign;
	const float inv_area = 1.0f / (float)area;
	auto edge = [](int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t px, int64_t py) { return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0); };
	// An edge is "top-left" if, with the triangle oriented positively, it's a top or left edge.
	auto top_left = [sign](int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
		const int64_t dx = (x1 - x0) * sign, dy = (y1 - y0) * sign;
		return (dy < 0) || (dy == 0 && dx > 0);
	};
	const int64_t bias0 = top_left(bx, by, cx, cy) ? 0 : -1;
	const int64_t bias1 = top_left(cx, cy, ax, ay) ? 0 : -1;
	const int64_t bias2 = top_left(ax, ay, bx, by) ? 0 : -1;
	const Material3D &mat = *p_tri.material;
	const float fog_range = std::max(0.001f, env.fog_end - env.fog_start);
	const Vec3 fog_color = env.sky_horizon;

	for (int y = y_start; y < y_end; y++) {
		const int64_t pyf = (int64_t)y * 16 + 8;
		float *drow = depth.data() + (size_t)y * width;
		uint32_t *crow = color.data() + (size_t)y * width;
		uint32_t *irow = ids.data() + (size_t)y * width;
		for (int x = x_start; x < x_end; x++) {
			const int64_t pxf = (int64_t)x * 16 + 8;
			const int64_t e0 = edge(bx, by, cx, cy, pxf, pyf) * sign;
			const int64_t e1 = edge(cx, cy, ax, ay, pxf, pyf) * sign;
			const int64_t e2 = edge(ax, ay, bx, by, pxf, pyf) * sign;
			if (e0 + bias0 < 0 || e1 + bias1 < 0 || e2 + bias2 < 0) {
				continue;
			}
			const float l0 = (float)e0 * inv_area;
			const float l1 = (float)e1 * inv_area;
			const float l2 = (float)e2 * inv_area;
			const float z = a.z * l0 + b.z * l1 + c.z * l2;
			if (z >= drow[x] || z < 0.0f) {
				continue;
			}
			const float iw = a.inv_w * l0 + b.inv_w * l1 + c.inv_w * l2;
			const float w = 1.0f / iw;
			const Vec3 world = (a.world_w * l0 + b.world_w * l1 + c.world_w * l2) * w;
			const Vec3 normal = (a.normal_w * l0 + b.normal_w * l1 + c.normal_w * l2) * w;
			const Vec2 uv{ (a.uv_w.x * l0 + b.uv_w.x * l1 + c.uv_w.x * l2) * w, (a.uv_w.y * l0 + b.uv_w.y * l1 + c.uv_w.y * l2) * w };
			Vec3 col = _shade(mat, world, normal, uv, p_tri.front);
			col = tonemap(col * env.exposure);
			if (env.fog) {
				const float dist = (world - camera.position).length();
				const float f = std::clamp((dist - env.fog_start) / fog_range, 0.0f, 1.0f);
				if (f > 0) {
					col = lerp(col, fog_color, f * f);
				}
			}
			drow[x] = z;
			crow[x] = linear_to_argb(col);
			irow[x] = p_tri.object_id;
		}
	}
}

void Renderer3D::_rasterize_band(int p_y0, int p_y1) {
	for (const Triangle &t : triangles) {
		if (t.max_y <= p_y0 || t.min_y >= p_y1) {
			continue;
		}
		_rasterize_triangle(t, p_y0, p_y1);
	}
}

static inline uint32_t blend_argb(uint32_t p_dst, uint32_t p_src) {
	const uint32_t a = (p_src >> 24) & 0xFF;
	if (a == 255) {
		return p_src;
	}
	const uint32_t ia = 255 - a;
	const uint32_t r = (((p_src >> 16) & 0xFF) * a + ((p_dst >> 16) & 0xFF) * ia) / 255;
	const uint32_t g = (((p_src >> 8) & 0xFF) * a + ((p_dst >> 8) & 0xFF) * ia) / 255;
	const uint32_t b = ((p_src & 0xFF) * a + (p_dst & 0xFF) * ia) / 255;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void Renderer3D::_draw_lines() {
	for (const Line &l : lines) {
		Vec4 ca = view_proj * Vec4(l.a.x, l.a.y, l.a.z, 1);
		Vec4 cb = view_proj * Vec4(l.b.x, l.b.y, l.b.z, 1);
		// Clip against the near plane.
		const float da = ca.z + ca.w;
		const float db = cb.z + cb.w;
		if (da < 0 && db < 0) {
			continue;
		}
		if (da < 0 || db < 0) {
			const float t = da / (da - db);
			const Vec4 p(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t);
			if (da < 0) {
				ca = p;
			} else {
				cb = p;
			}
		}
		auto project = [&](const Vec4 &c, float &x, float &y, float &z) {
			const float iw = 1.0f / std::max(c.w, 1e-6f);
			x = (c.x * iw * 0.5f + 0.5f) * width;
			y = (1.0f - (c.y * iw * 0.5f + 0.5f)) * height;
			z = c.z * iw * 0.5f + 0.5f;
		};
		float x0, y0, z0, x1, y1, z1;
		project(ca, x0, y0, z0);
		project(cb, x1, y1, z1);
		const float dx = x1 - x0, dy = y1 - y0;
		const int steps = (int)std::min(8192.0f, std::max(std::fabs(dx), std::fabs(dy)));
		if (steps <= 0) {
			continue;
		}
		for (int i = 0; i <= steps; i++) {
			const float t = (float)i / steps;
			const int x = (int)(x0 + dx * t);
			const int y = (int)(y0 + dy * t);
			if (x < 0 || y < 0 || x >= width || y >= height) {
				continue;
			}
			const size_t idx = (size_t)y * width + x;
			const float z = z0 + (z1 - z0) * t;
			if (l.depth_test && z - 0.0004f > depth[idx]) {
				continue;
			}
			color[idx] = blend_argb(color[idx], l.color);
		}
	}
}

void Renderer3D::end() {
	_draw_sky();
	std::fill(depth.begin(), depth.end(), 1.0f);
	std::fill(ids.begin(), ids.end(), 0u);
	_build_shadow_map();

	// Rasterize in horizontal bands, one per thread.
	unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
	if (triangles.size() < 64 || height < 64) {
		threads = 1;
	}
	if (threads == 1) {
		_rasterize_band(0, height);
	} else {
		std::vector<std::thread> pool;
		const int band = (height + (int)threads - 1) / (int)threads;
		for (unsigned i = 0; i < threads; i++) {
			const int y0 = (int)i * band;
			const int y1 = std::min(height, y0 + band);
			if (y0 >= y1) {
				break;
			}
			pool.emplace_back([this, y0, y1]() { _rasterize_band(y0, y1); });
		}
		for (std::thread &t : pool) {
			t.join();
		}
	}
	_draw_lines();
}

uint32_t Renderer3D::pick(int p_x, int p_y) const {
	if (p_x < 0 || p_y < 0 || p_x >= width || p_y >= height) {
		return 0;
	}
	return ids[(size_t)p_y * width + p_x];
}

void Renderer3D::screen_ray(float p_x, float p_y, Vec3 &r_origin, Vec3 &r_dir) const {
	const float nx = p_x / width * 2.0f - 1.0f;
	const float ny = 1.0f - p_y / height * 2.0f;
	const Vec4 pn = inv_view_proj * Vec4(nx, ny, -1, 1);
	const Vec4 pf = inv_view_proj * Vec4(nx, ny, 1, 1);
	r_origin = pn.xyz() / pn.w;
	r_dir = (pf.xyz() / pf.w - r_origin).normalized();
}

bool Renderer3D::world_to_screen(const Vec3 &p_world, float &r_x, float &r_y) const {
	const Vec4 c = view_proj * Vec4(p_world.x, p_world.y, p_world.z, 1);
	if (c.w <= 1e-5f) {
		return false;
	}
	r_x = (c.x / c.w * 0.5f + 0.5f) * width;
	r_y = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * height;
	return true;
}

} // namespace ipde
