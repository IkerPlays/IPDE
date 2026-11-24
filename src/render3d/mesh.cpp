#include "render3d/mesh.h"

#include "core/fs.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>

namespace ipde {

void Mesh::compute_bounds() {
	if (positions.empty()) {
		aabb_min = aabb_max = Vec3(0, 0, 0);
		return;
	}
	aabb_min = aabb_max = positions[0];
	for (const Vec3 &p : positions) {
		aabb_min = Vec3(std::min(aabb_min.x, p.x), std::min(aabb_min.y, p.y), std::min(aabb_min.z, p.z));
		aabb_max = Vec3(std::max(aabb_max.x, p.x), std::max(aabb_max.y, p.y), std::max(aabb_max.z, p.z));
	}
}

void Mesh::fix_winding() {
	if (normals.size() != positions.size()) {
		return;
	}
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const Vec3 &p0 = positions[indices[i]];
		const Vec3 &p1 = positions[indices[i + 1]];
		const Vec3 &p2 = positions[indices[i + 2]];
		const Vec3 n = normals[indices[i]] + normals[indices[i + 1]] + normals[indices[i + 2]];
		if (dot(cross(p1 - p0, p2 - p0), n) < 0) {
			std::swap(indices[i + 1], indices[i + 2]);
		}
	}
}

void Mesh::compute_flat_normals() {
	std::vector<Vec3> np;
	std::vector<Vec3> nn;
	std::vector<Vec2> nuv;
	std::vector<uint32_t> ni;
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const Vec3 &a = positions[indices[i]];
		const Vec3 &b = positions[indices[i + 1]];
		const Vec3 &c = positions[indices[i + 2]];
		const Vec3 n = cross(b - a, c - a).normalized();
		for (int k = 0; k < 3; k++) {
			const uint32_t idx = indices[i + k];
			np.push_back(positions[idx]);
			nn.push_back(n);
			nuv.push_back(idx < uvs.size() ? uvs[idx] : Vec2());
			ni.push_back((uint32_t)ni.size());
		}
	}
	positions.swap(np);
	normals.swap(nn);
	uvs.swap(nuv);
	indices.swap(ni);
}

namespace meshes {

static void add_vertex(Mesh &m, const Vec3 &p, const Vec3 &n, float u, float v) {
	m.positions.push_back(p);
	m.normals.push_back(n);
	m.uvs.push_back(Vec2{ u, v });
}

static void add_quad(Mesh &m, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	// Counter-clockwise when looking at the front face.
	m.indices.insert(m.indices.end(), { a, b, c, a, c, d });
}

std::shared_ptr<Mesh> cube() {
	auto m = std::make_shared<Mesh>();
	const Vec3 normals[6] = { Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1) };
	for (const Vec3 &n : normals) {
		// Two axes perpendicular to the normal.
		Vec3 u = std::fabs(n.y) > 0.5f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
		Vec3 v = cross(n, u);
		u = cross(v, n);
		const uint32_t base = (uint32_t)m->positions.size();
		const Vec3 c = n * 0.5f;
		add_vertex(*m, c - u * 0.5f - v * 0.5f, n, 0, 1);
		add_vertex(*m, c + u * 0.5f - v * 0.5f, n, 1, 1);
		add_vertex(*m, c + u * 0.5f + v * 0.5f, n, 1, 0);
		add_vertex(*m, c - u * 0.5f + v * 0.5f, n, 0, 0);
		// Make sure the winding faces outwards.
		const Vec3 fn = cross(m->positions[base + 1] - m->positions[base], m->positions[base + 2] - m->positions[base]);
		if (dot(fn, n) > 0) {
			add_quad(*m, base, base + 1, base + 2, base + 3);
		} else {
			add_quad(*m, base, base + 3, base + 2, base + 1);
		}
	}
	m->fix_winding();
	m->compute_bounds();
	return m;
}

std::shared_ptr<Mesh> sphere(int p_rings, int p_segments) {
	auto m = std::make_shared<Mesh>();
	for (int r = 0; r <= p_rings; r++) {
		const float v = (float)r / p_rings;
		const float phi = v * PI;
		for (int s = 0; s <= p_segments; s++) {
			const float u = (float)s / p_segments;
			const float theta = u * 2.0f * PI;
			const Vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
			add_vertex(*m, n * 0.5f, n, u, v);
		}
	}
	const int stride = p_segments + 1;
	for (int r = 0; r < p_rings; r++) {
		for (int s = 0; s < p_segments; s++) {
			const uint32_t a = r * stride + s;
			const uint32_t b = a + stride;
			m->indices.insert(m->indices.end(), { a, a + 1, b, a + 1, b + 1, b });
		}
	}
	m->fix_winding();
	m->compute_bounds();
	return m;
}

/* Terrain */

static float hash_noise(int p_x, int p_y, int p_seed) {
	uint32_t h = (uint32_t)p_x * 374761393u + (uint32_t)p_y * 668265263u + (uint32_t)p_seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

static float smooth_noise(float p_x, float p_y, int p_seed) {
	const int ix = (int)std::floor(p_x), iy = (int)std::floor(p_y);
	float tx = p_x - ix, ty = p_y - iy;
	tx = tx * tx * (3 - 2 * tx);
	ty = ty * ty * (3 - 2 * ty);
	const float a = hash_noise(ix, iy, p_seed), b = hash_noise(ix + 1, iy, p_seed);
	const float c = hash_noise(ix, iy + 1, p_seed), d = hash_noise(ix + 1, iy + 1, p_seed);
	return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}

// Normalized (0..1) heights of the terrain grid, cached by seed.
static const std::vector<float> &terrain_grid(int p_seed) {
	static std::map<int, std::vector<float>> cache;
	static std::mutex lock;
	std::lock_guard<std::mutex> guard(lock);
	auto it = cache.find(p_seed);
	if (it != cache.end()) {
		return it->second;
	}
	const int n = TERRAIN_RES + 1;
	std::vector<float> h((size_t)n * n);
	float lo = 1e9f, hi = -1e9f;
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < n; i++) {
			const float u = (float)i / TERRAIN_RES * 3.0f, v = (float)j / TERRAIN_RES * 3.0f;
			// A few octaves of smooth noise: big hills plus smaller bumps.
			float value = smooth_noise(u, v, p_seed) * 0.6f + smooth_noise(u * 2.1f, v * 2.1f, p_seed + 17) * 0.28f + smooth_noise(u * 4.3f, v * 4.3f, p_seed + 31) * 0.12f;
			h[(size_t)j * n + i] = value;
			lo = std::min(lo, value);
			hi = std::max(hi, value);
		}
	}
	const float range = std::max(1e-4f, hi - lo);
	for (float &v : h) {
		v = (v - lo) / range;
	}
	return cache.emplace(p_seed, std::move(h)).first->second;
}

float terrain_height(int p_seed, float p_u, float p_v) {
	const std::vector<float> &h = terrain_grid(p_seed);
	const int n = TERRAIN_RES + 1;
	const float x = std::clamp(p_u, 0.0f, 1.0f) * TERRAIN_RES, y = std::clamp(p_v, 0.0f, 1.0f) * TERRAIN_RES;
	const int ix = std::min((int)x, TERRAIN_RES - 1), iy = std::min((int)y, TERRAIN_RES - 1);
	const float fx = x - ix, fy = y - iy;
	const float a = h[(size_t)iy * n + ix], b = h[(size_t)iy * n + ix + 1];
	const float c = h[(size_t)(iy + 1) * n + ix], d = h[(size_t)(iy + 1) * n + ix + 1];
	// Same split as the mesh triangles (a-c-d and a-d-b).
	if (fy >= fx) {
		return a + (d - c) * fx + (c - a) * fy;
	}
	return a + (b - a) * fx + (d - b) * fy;
}

std::shared_ptr<Mesh> terrain(int p_seed) {
	static std::map<int, std::shared_ptr<Mesh>> cache;
	auto it = cache.find(p_seed);
	if (it != cache.end()) {
		return it->second;
	}
	const std::vector<float> &h = terrain_grid(p_seed);
	const int n = TERRAIN_RES + 1;
	auto m = std::make_shared<Mesh>();
	auto height_at = [&](int i, int j) { return h[(size_t)std::clamp(j, 0, n - 1) * n + std::clamp(i, 0, n - 1)]; };
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < n; i++) {
			const float u = (float)i / TERRAIN_RES, v = (float)j / TERRAIN_RES;
			// Normal from the height differences (in unit-box space).
			const float dx = (height_at(i + 1, j) - height_at(i - 1, j)) * TERRAIN_RES * 0.5f;
			const float dz = (height_at(i, j + 1) - height_at(i, j - 1)) * TERRAIN_RES * 0.5f;
			add_vertex(*m, Vec3(u - 0.5f, h[(size_t)j * n + i] - 0.5f, v - 0.5f), Vec3(-dx, 1.0f, -dz).normalized(), u * 8.0f, v * 8.0f);
		}
	}
	for (int j = 0; j < TERRAIN_RES; j++) {
		for (int i = 0; i < TERRAIN_RES; i++) {
			const uint32_t a = (uint32_t)(j * n + i), b = a + 1, c = a + n, d = c + 1;
			m->indices.insert(m->indices.end(), { a, c, d, a, d, b });
		}
	}
	m->fix_winding();
	m->compute_bounds();
	cache[p_seed] = m;
	return m;
}

/* Decorations */

// Appends a frustum (cone when p_r1 == 0) around the Y axis with a solid color.
static void add_frustum(Mesh &m, float p_y0, float p_y1, float p_r0, float p_r1, int p_segments, const Vec3 &p_color, float p_twist) {
	const uint32_t base = (uint32_t)m.positions.size();
	const float slope = (p_r0 - p_r1) / std::max(1e-4f, p_y1 - p_y0);
	for (int s = 0; s <= p_segments; s++) {
		const float a = (float)s / p_segments * 2.0f * PI + p_twist;
		const float cx = std::cos(a), sz = std::sin(a);
		const Vec3 n = Vec3(cx, slope, sz).normalized();
		add_vertex(m, Vec3(cx * p_r0, p_y0, sz * p_r0), n, (float)s / p_segments, 1);
		add_vertex(m, Vec3(cx * p_r1, p_y1, sz * p_r1), n, (float)s / p_segments, 0);
		m.colors.push_back(p_color);
		m.colors.push_back(p_color * 1.15f);
	}
	for (int s = 0; s < p_segments; s++) {
		const uint32_t a = base + s * 2, b = a + 1, c = a + 2, d = a + 3;
		m.indices.insert(m.indices.end(), { a, c, d, a, d, b });
	}
	// Bottom cap.
	const uint32_t center = (uint32_t)m.positions.size();
	add_vertex(m, Vec3(0, p_y0, 0), Vec3(0, -1, 0), 0.5f, 0.5f);
	m.colors.push_back(p_color * 0.7f);
	for (int s = 0; s <= p_segments; s++) {
		const float a = (float)s / p_segments * 2.0f * PI + p_twist;
		add_vertex(m, Vec3(std::cos(a) * p_r0, p_y0, std::sin(a) * p_r0), Vec3(0, -1, 0), 0, 0);
		m.colors.push_back(p_color * 0.7f);
	}
	for (int s = 0; s < p_segments; s++) {
		m.indices.insert(m.indices.end(), { center, center + 1 + s, center + 2 + s });
	}
}

std::shared_ptr<Mesh> tree(int p_seed) {
	static std::map<int, std::shared_ptr<Mesh>> cache;
	auto it = cache.find(p_seed);
	if (it != cache.end()) {
		return it->second;
	}
	auto m = std::make_shared<Mesh>();
	const float v = hash_noise(p_seed, 3, 11);
	const Vec3 trunk(0.22f, 0.11f, 0.05f);
	const Vec3 leaves = Vec3(0.05f + 0.04f * v, 0.28f + 0.12f * hash_noise(p_seed, 5, 2), 0.06f);
	add_frustum(*m, -0.5f, -0.18f, 0.07f, 0.055f, 8, trunk, 0);
	const int layers = 3;
	for (int i = 0; i < layers; i++) {
		const float y0 = -0.32f + i * 0.24f;
		const float r = 0.5f - i * 0.12f;
		add_frustum(*m, y0, std::min(0.5f, y0 + 0.42f), r, 0.0f, 10, leaves * (0.85f + 0.12f * i), i * 0.3f);
	}
	m->fix_winding();
	m->compute_bounds();
	cache[p_seed] = m;
	return m;
}

std::shared_ptr<Mesh> rock(int p_seed) {
	static std::map<int, std::shared_ptr<Mesh>> cache;
	auto it = cache.find(p_seed);
	if (it != cache.end()) {
		return it->second;
	}
	// A low-poly sphere with bumpy radius; every triangle gets its own normal (faceted look).
	const int rings = 6, segs = 9;
	std::vector<Vec3> pts;
	for (int r = 0; r <= rings; r++) {
		const float phi = (float)r / rings * PI;
		for (int sgm = 0; sgm < segs; sgm++) {
			const float th = (float)sgm / segs * 2.0f * PI;
			const float k = (r == 0 || r == rings) ? 1.0f : 0.75f + 0.35f * hash_noise(r, sgm, p_seed);
			pts.push_back(Vec3(std::sin(phi) * std::cos(th) * k, std::cos(phi) * k * 0.75f, std::sin(phi) * std::sin(th) * k));
		}
	}
	auto m = std::make_shared<Mesh>();
	auto tri = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c) {
		Vec3 n = cross(b - a, c - a).normalized();
		const Vec3 center = (a + b + c) * (1.0f / 3.0f);
		if (dot(n, center) < 0) {
			n = n * -1.0f;
		}
		const float shade = 0.8f + 0.2f * hash_noise((int)(center.x * 97), (int)(center.z * 89), p_seed);
		const Vec3 col = Vec3(0.30f, 0.29f, 0.27f) * shade;
		const uint32_t base = (uint32_t)m->positions.size();
		for (const Vec3 *p : { &a, &b, &c }) {
			add_vertex(*m, *p, n, p->x * 0.5f + 0.5f, p->z * 0.5f + 0.5f);
			m->colors.push_back(col);
		}
		m->indices.insert(m->indices.end(), { base, base + 1, base + 2 });
	};
	for (int r = 0; r < rings; r++) {
		for (int sgm = 0; sgm < segs; sgm++) {
			const Vec3 &a = pts[r * segs + sgm], &b = pts[r * segs + (sgm + 1) % segs];
			const Vec3 &c = pts[(r + 1) * segs + sgm], &d = pts[(r + 1) * segs + (sgm + 1) % segs];
			tri(a, c, d);
			tri(a, d, b);
		}
	}
	// Fit the unit box.
	m->compute_bounds();
	const Vec3 size = m->aabb_max - m->aabb_min, mid = (m->aabb_max + m->aabb_min) * 0.5f;
	for (Vec3 &p : m->positions) {
		p = Vec3((p.x - mid.x) / size.x, (p.y - mid.y) / size.y, (p.z - mid.z) / size.z);
	}
	m->fix_winding();
	m->compute_bounds();
	cache[p_seed] = m;
	return m;
}

std::shared_ptr<Mesh> plane() {
	auto m = std::make_shared<Mesh>();
	const Vec3 n(0, 1, 0);
	add_vertex(*m, Vec3(-0.5f, 0, -0.5f), n, 0, 0);
	add_vertex(*m, Vec3(-0.5f, 0, 0.5f), n, 0, 1);
	add_vertex(*m, Vec3(0.5f, 0, 0.5f), n, 1, 1);
	add_vertex(*m, Vec3(0.5f, 0, -0.5f), n, 1, 0);
	add_quad(*m, 0, 1, 2, 3);
	m->fix_winding();
	m->compute_bounds();
	return m;
}

static void add_cap(Mesh &m, float p_y, float p_radius, int p_segments, bool p_top) {
	const Vec3 n(0, p_top ? 1.0f : -1.0f, 0);
	const uint32_t center = (uint32_t)m.positions.size();
	add_vertex(m, Vec3(0, p_y, 0), n, 0.5f, 0.5f);
	for (int s = 0; s <= p_segments; s++) {
		const float t = (float)s / p_segments * 2.0f * PI;
		add_vertex(m, Vec3(std::cos(t) * p_radius, p_y, std::sin(t) * p_radius), n, 0.5f + std::cos(t) * 0.5f, 0.5f + std::sin(t) * 0.5f);
	}
	for (int s = 0; s < p_segments; s++) {
		if (p_top) {
			m.indices.insert(m.indices.end(), { center, center + 2 + s, center + 1 + s });
		} else {
			m.indices.insert(m.indices.end(), { center, center + 1 + s, center + 2 + s });
		}
	}
}

std::shared_ptr<Mesh> cylinder(int p_segments) {
	auto m = std::make_shared<Mesh>();
	for (int s = 0; s <= p_segments; s++) {
		const float u = (float)s / p_segments;
		const float t = u * 2.0f * PI;
		const Vec3 n(std::cos(t), 0, std::sin(t));
		add_vertex(*m, Vec3(n.x * 0.5f, 0.5f, n.z * 0.5f), n, u, 0);
		add_vertex(*m, Vec3(n.x * 0.5f, -0.5f, n.z * 0.5f), n, u, 1);
	}
	for (int s = 0; s < p_segments; s++) {
		const uint32_t a = s * 2;
		m->indices.insert(m->indices.end(), { a, a + 2, a + 1, a + 2, a + 3, a + 1 });
	}
	add_cap(*m, 0.5f, 0.5f, p_segments, true);
	add_cap(*m, -0.5f, 0.5f, p_segments, false);
	m->fix_winding();
	m->compute_bounds();
	return m;
}

std::shared_ptr<Mesh> cone(int p_segments) {
	auto m = std::make_shared<Mesh>();
	const float slope = 0.5f; // radius / height
	for (int s = 0; s <= p_segments; s++) {
		const float u = (float)s / p_segments;
		const float t = u * 2.0f * PI;
		const Vec3 n = Vec3(std::cos(t), slope, std::sin(t)).normalized();
		add_vertex(*m, Vec3(0, 0.5f, 0), n, u, 0);
		add_vertex(*m, Vec3(std::cos(t) * 0.5f, -0.5f, std::sin(t) * 0.5f), n, u, 1);
	}
	for (int s = 0; s < p_segments; s++) {
		const uint32_t a = s * 2;
		m->indices.insert(m->indices.end(), { a, a + 3, a + 1 });
	}
	add_cap(*m, -0.5f, 0.5f, p_segments, false);
	m->fix_winding();
	m->compute_bounds();
	return m;
}

std::shared_ptr<Mesh> torus(int p_rings, int p_segments) {
	auto m = std::make_shared<Mesh>();
	const float big = 0.35f, small = 0.15f;
	for (int r = 0; r <= p_rings; r++) {
		const float u = (float)r / p_rings * 2.0f * PI;
		const Vec3 center(std::cos(u) * big, 0, std::sin(u) * big);
		for (int s = 0; s <= p_segments; s++) {
			const float v = (float)s / p_segments * 2.0f * PI;
			const Vec3 n(std::cos(u) * std::cos(v), std::sin(v), std::sin(u) * std::cos(v));
			add_vertex(*m, center + n * small, n, (float)r / p_rings, (float)s / p_segments);
		}
	}
	const int stride = p_segments + 1;
	for (int r = 0; r < p_rings; r++) {
		for (int s = 0; s < p_segments; s++) {
			const uint32_t a = r * stride + s;
			const uint32_t b = a + stride;
			m->indices.insert(m->indices.end(), { a, a + 1, b, a + 1, b + 1, b });
		}
	}
	// Fix winding so faces point outwards.
	for (size_t i = 0; i + 2 < m->indices.size(); i += 3) {
		const Vec3 &p0 = m->positions[m->indices[i]];
		const Vec3 &p1 = m->positions[m->indices[i + 1]];
		const Vec3 &p2 = m->positions[m->indices[i + 2]];
		if (dot(cross(p1 - p0, p2 - p0), m->normals[m->indices[i]]) < 0) {
			std::swap(m->indices[i + 1], m->indices[i + 2]);
		}
	}
	m->fix_winding();
	m->compute_bounds();
	return m;
}

std::shared_ptr<Mesh> capsule(int p_rings, int p_segments) {
	auto m = std::make_shared<Mesh>();
	// Fits a 1x1x1 box: hemispheres of radius 0.5 squashed to 0.25 in Y plus a 0.5 tall cylinder.
	const float radius = 0.5f;
	const float half = 0.25f; // Half height of the cylindrical part.
	const float squash = 0.5f;
	const int total_rings = p_rings * 2 + 1;
	for (int r = 0; r <= total_rings; r++) {
		float phi;
		float yoff;
		if (r <= p_rings) {
			phi = (float)r / p_rings * PI * 0.5f;
			yoff = half;
		} else {
			phi = PI * 0.5f + (float)(r - p_rings - 1) / p_rings * PI * 0.5f;
			yoff = -half;
		}
		for (int s = 0; s <= p_segments; s++) {
			const float t = (float)s / p_segments * 2.0f * PI;
			const Vec3 n(std::sin(phi) * std::cos(t), std::cos(phi), std::sin(phi) * std::sin(t));
			add_vertex(*m, Vec3(n.x * radius, n.y * radius * squash + yoff, n.z * radius), Vec3(n.x, n.y * 2.0f, n.z).normalized(), (float)s / p_segments, (float)r / total_rings);
		}
	}
	const int stride = p_segments + 1;
	for (int r = 0; r < total_rings; r++) {
		for (int s = 0; s < p_segments; s++) {
			const uint32_t a = r * stride + s;
			const uint32_t b = a + stride;
			m->indices.insert(m->indices.end(), { a, a + 1, b, a + 1, b + 1, b });
		}
	}
	m->fix_winding();
	m->compute_bounds();
	return m;
}

static std::map<std::string, std::shared_ptr<Mesh>> g_obj_cache;

void clear_cache() {
	g_obj_cache.clear();
}

std::shared_ptr<Mesh> load_obj(const std::string &p_path, std::string *r_error) {
	auto cached = g_obj_cache.find(p_path);
	if (cached != g_obj_cache.end()) {
		return cached->second;
	}
	std::string text;
	if (!fs::read_file(p_path, text)) {
		if (r_error) {
			*r_error = "No se pudo leer el archivo.";
		}
		g_obj_cache[p_path] = nullptr;
		return nullptr;
	}
	std::vector<Vec3> pos;
	std::vector<Vec3> nor;
	std::vector<Vec2> uv;
	auto m = std::make_shared<Mesh>();
	std::map<std::string, uint32_t> vertex_map;
	bool has_normals = true;

	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (line.size() < 2) {
			continue;
		}
		std::istringstream ls(line);
		std::string tag;
		ls >> tag;
		if (tag == "v") {
			Vec3 p;
			ls >> p.x >> p.y >> p.z;
			pos.push_back(p);
		} else if (tag == "vn") {
			Vec3 n;
			ls >> n.x >> n.y >> n.z;
			nor.push_back(n);
		} else if (tag == "vt") {
			Vec2 t;
			ls >> t.x >> t.y;
			t.y = 1.0f - t.y;
			uv.push_back(t);
		} else if (tag == "f") {
			std::vector<uint32_t> face;
			std::string token;
			while (ls >> token) {
				auto it = vertex_map.find(token);
				if (it != vertex_map.end()) {
					face.push_back(it->second);
					continue;
				}
				int vi = 0, ti = 0, ni = 0;
				const size_t s1 = token.find('/');
				vi = atoi(token.c_str());
				if (s1 != std::string::npos) {
					const size_t s2 = token.find('/', s1 + 1);
					if (s2 != s1 + 1) {
						ti = atoi(token.c_str() + s1 + 1);
					}
					if (s2 != std::string::npos) {
						ni = atoi(token.c_str() + s2 + 1);
					}
				}
				// Negative indices are relative to the end.
				if (vi < 0) {
					vi = (int)pos.size() + vi + 1;
				}
				if (ti < 0) {
					ti = (int)uv.size() + ti + 1;
				}
				if (ni < 0) {
					ni = (int)nor.size() + ni + 1;
				}
				if (vi <= 0 || vi > (int)pos.size()) {
					continue;
				}
				const uint32_t index = (uint32_t)m->positions.size();
				m->positions.push_back(pos[vi - 1]);
				m->uvs.push_back(ti > 0 && ti <= (int)uv.size() ? uv[ti - 1] : Vec2());
				if (ni > 0 && ni <= (int)nor.size()) {
					m->normals.push_back(nor[ni - 1].normalized());
				} else {
					m->normals.push_back(Vec3());
					has_normals = false;
				}
				vertex_map[token] = index;
				face.push_back(index);
			}
			for (size_t k = 1; k + 1 < face.size(); k++) {
				m->indices.insert(m->indices.end(), { face[0], face[k], face[k + 1] });
			}
		}
	}
	if (m->indices.empty()) {
		if (r_error) {
			*r_error = "El archivo no contiene caras.";
		}
		g_obj_cache[p_path] = nullptr;
		return nullptr;
	}
	if (!has_normals) {
		m->compute_flat_normals();
	}
	// Normalize to a unit box centered at the origin.
	m->compute_bounds();
	const Vec3 size = m->aabb_max - m->aabb_min;
	const float max_dim = std::max(size.x, std::max(size.y, size.z));
	const Vec3 center = (m->aabb_min + m->aabb_max) * 0.5f;
	const float inv = max_dim > 0 ? 1.0f / max_dim : 1.0f;
	for (Vec3 &p : m->positions) {
		p = (p - center) * inv;
	}
	m->compute_bounds();
	g_obj_cache[p_path] = m;
	return m;
}

} // namespace meshes

} // namespace ipde
