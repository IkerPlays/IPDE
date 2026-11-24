#pragma once

// IPDE meshes: built-in primitives and Wavefront OBJ loading.

#include "render3d/math3d.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ipde {

struct Mesh {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> uvs;
	std::vector<uint32_t> indices; // Triangles.
	Vec3 aabb_min;
	Vec3 aabb_max;

	void compute_bounds();
	void compute_flat_normals(); // Splits vertices so every triangle has its own normal.
	void fix_winding(); // Makes every triangle counter-clockwise when seen from the side its normals point to.
	size_t triangle_count() const { return indices.size() / 3; }
};

namespace meshes {
// All primitives fit in a 1x1x1 box centered at the origin (scale them with the entity size).
std::shared_ptr<Mesh> cube();
std::shared_ptr<Mesh> sphere(int p_rings = 16, int p_segments = 24);
std::shared_ptr<Mesh> plane();
std::shared_ptr<Mesh> cylinder(int p_segments = 24);
std::shared_ptr<Mesh> cone(int p_segments = 24);
std::shared_ptr<Mesh> torus(int p_rings = 24, int p_segments = 12);
std::shared_ptr<Mesh> capsule(int p_rings = 8, int p_segments = 20);

// Hilly terrain (heightfield) in the unit box; the shape depends on p_seed. Cached.
constexpr int TERRAIN_RES = 64; // Grid cells per side.
std::shared_ptr<Mesh> terrain(int p_seed);
// Height of the terrain surface in 0..1 at (p_u, p_v) in 0..1 (same shape as the mesh).
float terrain_height(int p_seed, float p_u, float p_v);

// Loads an OBJ file and normalizes it to fit a 1x1x1 box (keeping proportions).
// Cached by path. Returns null if the file can't be read.
std::shared_ptr<Mesh> load_obj(const std::string &p_path, std::string *r_error = nullptr);
void clear_cache();
} // namespace meshes

} // namespace ipde
