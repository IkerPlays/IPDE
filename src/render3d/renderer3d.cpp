#include "render3d/renderer3d.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
	vis_tri.assign((size_t)width * height, UINT32_MAX);
	vis_l0.assign((size_t)width * height, 0.0f);
	vis_l1.assign((size_t)width * height, 0.0f);
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
	points.clear();
	materials.clear();
	shadow_casters.clear();
	shadow_valid = false;
}

void Renderer3D::draw_mesh(const Mesh &p_mesh, const Mat4 &p_model, const Material3D &p_material, uint32_t p_object_id) {
	materials.push_back(std::make_unique<Material3D>(p_material));
	materials.back()->pattern_origin = p_model.transform_point(Vec3(0, 0, 0));
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

void Renderer3D::draw_point(const Vec3 &p_center, float p_radius, uint32_t p_color) {
	points.push_back({ p_center, p_radius, p_color });
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
	// Sun disc and glow.
	const Vec3 to_sun = (env.sun_direction * -1.0f).normalized();
	const Vec3 far_point = camera.position + to_sun * (camera.zfar * 0.5f);
	const Vec4 cs = view_proj * Vec4(far_point.x, far_point.y, far_point.z, 1);
	if (cs.w <= 0 || to_sun.y < -0.05f || env.sun_energy <= 0) {
		return;
	}
	const float sx = (cs.x / cs.w * 0.5f + 0.5f) * width;
	const float sy = (1.0f - (cs.y / cs.w * 0.5f + 0.5f)) * height;
	const float px_per_rad = height * 0.5f / std::tan(camera.fov * DEG2RAD * 0.5f);
	const float glow_r = 0.35f * px_per_rad;
	const int x0 = std::max(0, (int)(sx - glow_r)), x1 = std::min(width - 1, (int)(sx + glow_r));
	const int y0 = std::max(0, (int)(sy - glow_r)), y1 = std::min(height - 1, (int)(sy + glow_r));
	const Vec3 sun = env.sun_color * std::min(env.sun_energy, 3.0f);
	for (int y = y0; y <= y1; y++) {
		const float ndc_y = 1.0f - 2.0f * ((float)y + 0.5f) / height;
		const Vec4 pn = inv_view_proj * Vec4(0, ndc_y, -1, 1);
		const Vec4 pf = inv_view_proj * Vec4(0, ndc_y, 1, 1);
		const Vec3 dir = (pf.xyz() / pf.w - pn.xyz() / pn.w).normalized();
		const Vec3 base = dir.y >= 0 ? lerp(hor, top, std::pow(std::min(1.0f, dir.y), 0.55f)) : lerp(hor, gnd, std::min(1.0f, -dir.y * 3.0f));
		uint32_t *row = color.data() + (size_t)y * width;
		for (int x = x0; x <= x1; x++) {
			const float dx = x + 0.5f - sx, dy = y + 0.5f - sy;
			const float ang = std::sqrt(dx * dx + dy * dy) / px_per_rad;
			if (ang > 0.35f) {
				continue;
			}
			const float disc = std::clamp((0.03f - ang) / 0.006f, 0.0f, 1.0f);
			const float glow = std::exp(-ang * 14.0f) * 0.45f;
			row[x] = linear_to_argb((base + sun * (disc * 4.0f + glow)) * env.exposure);
		}
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
	// Transform every caster triangle once, then rasterize horizontal bands in parallel.
	struct ShadowTri {
		Vec3 a, b, d;
		float inv_area;
		int x0, x1, y0, y1;
	};
	std::vector<ShadowTri> tris;
	for (const ShadowCaster &c : shadow_casters) {
		const Mat4 mvp = shadow_view_proj * c.model;
		std::vector<Vec3> sp(c.mesh->positions.size());
		for (size_t i = 0; i < sp.size(); i++) {
			const Vec3 &p = c.mesh->positions[i];
			const Vec4 cl = mvp * Vec4(p.x, p.y, p.z, 1.0f);
			sp[i] = Vec3((cl.x * 0.5f + 0.5f) * S, (1.0f - (cl.y * 0.5f + 0.5f)) * S, cl.z * 0.5f + 0.5f);
		}
		for (size_t i = 0; i + 2 < c.mesh->indices.size(); i += 3) {
			ShadowTri t;
			t.a = sp[c.mesh->indices[i]];
			t.b = sp[c.mesh->indices[i + 1]];
			t.d = sp[c.mesh->indices[i + 2]];
			const float area = (t.b.x - t.a.x) * (t.d.y - t.a.y) - (t.b.y - t.a.y) * (t.d.x - t.a.x);
			if (std::fabs(area) < 1e-9f) {
				continue;
			}
			t.inv_area = 1.0f / area;
			t.x0 = std::max(0, (int)std::floor(std::min({ t.a.x, t.b.x, t.d.x })));
			t.x1 = std::min(S - 1, (int)std::ceil(std::max({ t.a.x, t.b.x, t.d.x })));
			t.y0 = std::max(0, (int)std::floor(std::min({ t.a.y, t.b.y, t.d.y })));
			t.y1 = std::min(S - 1, (int)std::ceil(std::max({ t.a.y, t.b.y, t.d.y })));
			if (t.x0 <= t.x1 && t.y0 <= t.y1) {
				tris.push_back(t);
			}
		}
	}
	auto raster = [&](int p_y0, int p_y1) {
		for (const ShadowTri &t : tris) {
			const int y0 = std::max(t.y0, p_y0), y1 = std::min(t.y1, p_y1 - 1);
			const Vec3 &a = t.a, &b = t.b, &d = t.d;
			// Barycentric weights are linear in x: step them instead of recomputing.
			const float dw0 = -(b.y - d.y) * t.inv_area * -1.0f;
			const float dw1 = -(d.y - a.y) * t.inv_area * -1.0f;
			for (int y = y0; y <= y1; y++) {
				const float py = y + 0.5f;
				const float px = t.x0 + 0.5f;
				float w0 = ((b.x - px) * (d.y - py) - (b.y - py) * (d.x - px)) * t.inv_area;
				float w1 = ((d.x - px) * (a.y - py) - (d.y - py) * (a.x - px)) * t.inv_area;
				float *row = shadow_map.data() + (size_t)y * S;
				for (int x = t.x0; x <= t.x1; x++, w0 += dw0, w1 += dw1) {
					const float w2 = 1.0f - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) {
						continue;
					}
					const float z = a.z * w0 + b.z * w1 + d.z * w2;
					if (z < row[x]) {
						row[x] = z;
					}
				}
			}
		}
	};
	const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
	if (threads == 1 || tris.size() < 32) {
		raster(0, S);
	} else {
		std::vector<std::thread> pool;
		const int band = (S + (int)threads - 1) / (int)threads;
		for (unsigned i = 0; i < threads; i++) {
			const int y0 = (int)i * band, y1 = std::min(S, y0 + band);
			if (y0 < y1) {
				pool.emplace_back([&raster, y0, y1]() { raster(y0, y1); });
			}
		}
		for (std::thread &th : pool) {
			th.join();
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

// Fast replacement for pow(x, n) in specular highlights (Schlick's approximation).
static inline float spec_pow(float p_x, float p_n) {
	if (p_x <= 0.0f) {
		return 0.0f;
	}
	return p_x / (p_n - p_n * p_x + p_x);
}

static float hash2(int p_x, int p_y) {
	uint32_t h = (uint32_t)p_x * 374761393u + (uint32_t)p_y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

static float value_noise(float p_x, float p_y) {
	const float fx = std::floor(p_x), fy = std::floor(p_y);
	const int ix = (int)fx, iy = (int)fy;
	float tx = p_x - fx, ty = p_y - fy;
	tx = tx * tx * (3 - 2 * tx);
	ty = ty * ty * (3 - 2 * ty);
	const float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
	return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

// Brightness factor of a procedural pattern at a point (planar projection on the dominant axis).
static float pattern_factor(const Material3D &p_mat, const Vec3 &p_world, const Vec3 &p_normal, float p_time) {
	const Vec3 p = (p_world - p_mat.pattern_origin) * (1.0f / std::max(0.01f, p_mat.pattern_scale));
	const float ax = std::fabs(p_normal.x), ay = std::fabs(p_normal.y), az = std::fabs(p_normal.z);
	float u, v;
	if (ay >= ax && ay >= az) {
		u = p.x;
		v = p.z;
	} else if (ax >= az) {
		u = p.z;
		v = p.y;
	} else {
		u = p.x;
		v = p.y;
	}
	// Small offset so faces exactly on tile borders don't flicker.
	u += 0.0013f;
	v += 0.0017f;
	switch (p_mat.pattern) {
		case Material3D::PATTERN_CHECKER:
			return (((int)std::floor(u) + (int)std::floor(v)) & 1) ? 1.0f : 0.72f;
		case Material3D::PATTERN_BRICKS: {
			const float row = std::floor(v * 2.0f);
			const float bu = u + (((int)row & 1) ? 0.5f : 0.0f);
			const float fu = bu - std::floor(bu), fv = v * 2.0f - row;
			if (fu < 0.04f || fv < 0.08f) {
				return 0.55f; // Mortar.
			}
			return 0.82f + 0.18f * hash2((int)std::floor(bu), (int)row);
		}
		case Material3D::PATTERN_STRIPES:
			return ((int)std::floor(u * 2.0f) & 1) ? 1.0f : 0.75f;
		case Material3D::PATTERN_TILES: {
			const float fu = u - std::floor(u), fv = v - std::floor(v);
			if (fu < 0.03f || fv < 0.03f || fu > 0.97f || fv > 0.97f) {
				return 0.6f;
			}
			return 0.93f + 0.07f * hash2((int)std::floor(u), (int)std::floor(v));
		}
		case Material3D::PATTERN_WATER: {
			// Two layers of noise drifting in different directions.
			const float a = value_noise(u * 1.5f + p_time * 0.35f, v * 1.5f + p_time * 0.2f);
			const float b = value_noise(u * 3.1f - p_time * 0.25f, v * 3.1f + p_time * 0.3f);
			return 0.8f + 0.35f * (a * 0.6f + b * 0.4f);
		}
		case Material3D::PATTERN_LAVA: {
			const float a = value_noise(u * 1.2f + p_time * 0.12f, v * 1.2f - p_time * 0.08f);
			const float b = value_noise(u * 2.7f - p_time * 0.1f, v * 2.7f + p_time * 0.15f);
			const float f = a * 0.6f + b * 0.4f;
			return 0.45f + 1.1f * f * f;
		}
		case Material3D::PATTERN_NOISE:
			return 0.7f + 0.3f * (value_noise(u * 2.0f, v * 2.0f) * 0.65f + value_noise(u * 6.0f, v * 6.0f) * 0.35f);
		default:
			return 1.0f;
	}
}

Vec3 Renderer3D::_shade(const Material3D &p_mat, const Vec3 &p_world, Vec3 p_normal, const Vec2 &p_uv, bool p_front) const {
	Vec3 albedo = p_mat.albedo;
	if (p_mat.texture) {
		albedo = albedo * p_mat.texture->sample(p_uv.x * p_mat.uv_scale, p_uv.y * p_mat.uv_scale);
	}
	float lava_glow = 0.0f;
	if (p_mat.pattern != Material3D::PATTERN_NONE) {
		float f = pattern_factor(p_mat, p_world, p_normal, env.time);
		if (p_mat.pattern == Material3D::PATTERN_LAVA) {
			lava_glow = std::max(0.0f, f - 0.7f) * 2.5f; // Bright veins glow.
		} else if (p_mat.pattern == Material3D::PATTERN_WATER) {
			// Ripples: tilt the normal with the animated noise so highlights sparkle.
			const Vec3 q = (p_world - p_mat.pattern_origin) * (1.0f / std::max(0.01f, p_mat.pattern_scale));
			const float dx = value_noise(q.x * 2.0f + env.time * 0.5f, q.z * 2.0f) - 0.5f;
			const float dz = value_noise(q.x * 2.0f + 7.3f, q.z * 2.0f - env.time * 0.45f) - 0.5f;
			p_normal = p_normal + Vec3(dx, 0, dz) * 0.5f;
		}
		// Fade the contrast with distance to hide aliasing.
		const float dist = (p_world - camera.position).length();
		const float fade = std::clamp(1.0f - (dist / std::max(0.01f, p_mat.pattern_scale) - 25.0f) / 40.0f, 0.0f, 1.0f);
		f = 1.0f + (f - 1.0f) * fade - (1.0f - fade) * 0.12f;
		albedo = albedo * f;
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
			const float spec = spec_pow(dot(n, h), shininess) * spec_strength * (shininess + 8.0f) / 32.0f;
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
		const float spec = spec_pow(dot(n, h), shininess) * spec_strength * (shininess + 8.0f) / 32.0f;
		result += (albedo * (ndl_p * diffuse_weight) + spec_color * (spec * ndl_p)) * l.color * (l.energy * att);
	}

	result += p_mat.emission + albedo * lava_glow;
	return result;
}

void Renderer3D::_rasterize_triangle(const Triangle &p_tri, uint32_t p_index, int p_y0, int p_y1) {
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
	// between triangles never leave cracks or get drawn twice. Evaluated incrementally.
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
	// Change of each edge function per pixel step in x (16 fixed-point units).
	const int64_t step0 = -(cy - by) * 16 * sign;
	const int64_t step1 = -(ay - cy) * 16 * sign;
	const int64_t step2 = -(by - ay) * 16 * sign;
	const float dz0 = a.z, dz1 = b.z, dz2 = c.z;

	for (int y = y_start; y < y_end; y++) {
		const int64_t pyf = (int64_t)y * 16 + 8;
		const int64_t px0 = (int64_t)x_start * 16 + 8;
		int64_t e0 = edge(bx, by, cx, cy, px0, pyf) * sign;
		int64_t e1 = edge(cx, cy, ax, ay, px0, pyf) * sign;
		int64_t e2 = edge(ax, ay, bx, by, px0, pyf) * sign;
		const size_t row = (size_t)y * width;
		float *drow = depth.data() + row;
		for (int x = x_start; x < x_end; x++, e0 += step0, e1 += step1, e2 += step2) {
			if ((e0 + bias0) < 0 || (e1 + bias1) < 0 || (e2 + bias2) < 0) {
				continue;
			}
			const float l0 = (float)e0 * inv_area;
			const float l1 = (float)e1 * inv_area;
			const float l2 = (float)e2 * inv_area;
			const float z = dz0 * l0 + dz1 * l1 + dz2 * l2;
			if (z >= drow[x] || z < 0.0f) {
				continue;
			}
			drow[x] = z;
			vis_tri[row + x] = p_index;
			vis_l0[row + x] = l0;
			vis_l1[row + x] = l1;
		}
	}
}

void Renderer3D::_rasterize_band(int p_y0, int p_y1) {
	const size_t first = (size_t)p_y0 * width, last = (size_t)p_y1 * width;
	std::fill(vis_tri.begin() + first, vis_tri.begin() + last, UINT32_MAX);
	for (size_t i = 0; i < triangles.size(); i++) {
		const Triangle &t = triangles[i];
		if (t.max_y <= p_y0 || t.min_y >= p_y1) {
			continue;
		}
		_rasterize_triangle(t, (uint32_t)i, p_y0, p_y1);
	}
	_shade_band(p_y0, p_y1);
}

// Shades every visible pixel once (no work is wasted on hidden surfaces).
void Renderer3D::_shade_band(int p_y0, int p_y1) {
	const float fog_range = std::max(0.001f, env.fog_end - env.fog_start);
	const Vec3 fog_color = env.sky_horizon;
	for (int y = p_y0; y < p_y1; y++) {
		const size_t row = (size_t)y * width;
		for (int x = 0; x < width; x++) {
			const size_t idx = row + x;
			const uint32_t ti = vis_tri[idx];
			if (ti == UINT32_MAX) {
				continue;
			}
			const Triangle &tri = triangles[ti];
			const ScreenVertex &a = tri.v[0];
			const ScreenVertex &b = tri.v[1];
			const ScreenVertex &c = tri.v[2];
			const float l0 = vis_l0[idx], l1 = vis_l1[idx], l2 = 1.0f - l0 - l1;
			const float iw = a.inv_w * l0 + b.inv_w * l1 + c.inv_w * l2;
			const float w = 1.0f / iw;
			const Vec3 world = (a.world_w * l0 + b.world_w * l1 + c.world_w * l2) * w;
			const Vec3 normal = (a.normal_w * l0 + b.normal_w * l1 + c.normal_w * l2) * w;
			const Vec2 uv{ (a.uv_w.x * l0 + b.uv_w.x * l1 + c.uv_w.x * l2) * w, (a.uv_w.y * l0 + b.uv_w.y * l1 + c.uv_w.y * l2) * w };
			Vec3 col = _shade(*tri.material, world, normal, uv, tri.front);
			col = tonemap(col * env.exposure);
			if (env.fog) {
				const float dist = (world - camera.position).length();
				const float f = std::clamp((dist - env.fog_start) / fog_range, 0.0f, 1.0f);
				if (f > 0) {
					col = lerp(col, fog_color, f * f);
				}
			}
			color[idx] = linear_to_argb(col);
			ids[idx] = tri.object_id;
		}
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

void Renderer3D::_draw_points() {
	const float focal = height * 0.5f / std::tan(camera.fov * DEG2RAD * 0.5f);
	for (const Point &pt : points) {
		const Vec4 c = view_proj * Vec4(pt.center.x, pt.center.y, pt.center.z, 1);
		if (c.w <= camera.znear) {
			continue;
		}
		const float iw = 1.0f / c.w;
		const float sx = (c.x * iw * 0.5f + 0.5f) * width;
		const float sy = (1.0f - (c.y * iw * 0.5f + 0.5f)) * height;
		const float z = c.z * iw * 0.5f + 0.5f;
		const float r = std::max(0.75f, pt.radius * focal * iw);
		const int x0 = std::max(0, (int)(sx - r)), x1 = std::min(width - 1, (int)(sx + r));
		const int y0 = std::max(0, (int)(sy - r)), y1 = std::min(height - 1, (int)(sy + r));
		const uint32_t base_alpha = pt.color >> 24;
		for (int y = y0; y <= y1; y++) {
			for (int x = x0; x <= x1; x++) {
				const float dx = x + 0.5f - sx, dy = y + 0.5f - sy;
				const float d2 = (dx * dx + dy * dy) / (r * r);
				if (d2 > 1.0f) {
					continue;
				}
				const size_t idx = (size_t)y * width + x;
				if (z > depth[idx]) {
					continue;
				}
				// Soft edge.
				const uint32_t a = (uint32_t)(base_alpha * std::min(1.0f, (1.0f - d2) * 2.5f));
				color[idx] = blend_argb(color[idx], (a << 24) | (pt.color & 0xFFFFFF));
			}
		}
	}
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
	auto T = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
	double t0 = T();
	_draw_sky();
	double t1 = T();
	std::fill(depth.begin(), depth.end(), 1.0f);
	std::fill(ids.begin(), ids.end(), 0u);
	_build_shadow_map();
	double t2 = T();
	// IPDE_PROFILE=1 prints the time of each stage every 10 frames.
	static int frame = 0;
	static const bool profile = getenv("IPDE_PROFILE") != nullptr;
	const bool prof = profile && (frame++ % 10) == 0;

	// Rasterize and shade in small horizontal bands; threads take the next free band, so busy
	// areas (many objects) don't leave the other threads waiting.
	unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
	if (triangles.size() < 64 || height < 64) {
		threads = 1;
	}
	if (threads == 1) {
		_rasterize_band(0, height);
	} else {
		const int band = 16;
		std::atomic<int> next{ 0 };
		auto worker = [this, &next, band]() {
			for (;;) {
				const int y0 = next.fetch_add(band);
				if (y0 >= height) {
					break;
				}
				_rasterize_band(y0, std::min(height, y0 + band));
			}
		};
		std::vector<std::thread> pool;
		for (unsigned i = 1; i < threads; i++) {
			pool.emplace_back(worker);
		}
		worker(); // This thread works too.
		for (std::thread &t : pool) {
			t.join();
		}
	}
	double t3 = T();
	_draw_lines();
	_draw_points();
	if (prof) fprintf(stderr, "sky %.2f shadow %.2f raster+shade %.2f lines %.2f\n", t1 - t0, t2 - t1, t3 - t2, T() - t3);
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
