#include "render3d/mesh.h"

#include "core/fs.h"

#include <algorithm>
#include <cstdlib>
#include <map>
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
