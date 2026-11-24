#pragma once

// IPDE software 3D renderer.
// - Perspective-correct triangle rasterization with a depth buffer and near-plane clipping.
// - Per-pixel lighting: hemisphere ambient, a sun (directional light) with shadows, point lights,
//   Blinn-Phong specular, emission, fog, tone mapping and gamma.
// - Triangles are binned and rasterized in parallel (one horizontal band per thread).
// - An object ID buffer allows exact mouse picking in the editor.

#include "render3d/math3d.h"
#include "render3d/mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ipde {

struct Texture3D {
	int width = 0;
	int height = 0;
	std::vector<Vec3> texels; // Linear color.
	std::vector<float> alpha;
	Vec3 sample(float p_u, float p_v) const;
};

struct Material3D {
	Vec3 albedo = Vec3(0.8f, 0.8f, 0.8f); // Linear color.
	float roughness = 0.6f; // 0 = shiny, 1 = matte.
	float metallic = 0.0f;
	Vec3 emission = Vec3(0, 0, 0);
	bool unshaded = false;
	bool double_sided = false;
	bool cast_shadows = true;
	const Texture3D *texture = nullptr;
	float uv_scale = 1.0f;
};

struct PointLight3D {
	Vec3 position;
	Vec3 color = Vec3(1, 1, 1);
	float energy = 1.0f;
	float range = 10.0f;
};

struct Environment3D {
	Vec3 sky_top = Vec3(0.18f, 0.34f, 0.75f);
	Vec3 sky_horizon = Vec3(0.62f, 0.72f, 0.86f);
	Vec3 ground = Vec3(0.20f, 0.18f, 0.16f);
	float ambient = 0.55f;
	Vec3 sun_direction = Vec3(-0.4f, -1.0f, -0.6f); // Direction the light travels.
	Vec3 sun_color = Vec3(1.0f, 0.96f, 0.88f);
	float sun_energy = 1.2f;
	bool shadows = true;
	bool fog = true;
	float fog_start = 25.0f;
	float fog_end = 120.0f;
	float exposure = 1.0f;
};

struct Camera3D {
	Vec3 position = Vec3(0, 3, 8);
	Vec3 target = Vec3(0, 0, 0);
	float fov = 60.0f; // Degrees, vertical.
	float znear = 0.05f;
	float zfar = 500.0f;
};

class Renderer3D {
public:
	Renderer3D();
	~Renderer3D();

	void resize(int p_width, int p_height);
	int get_width() const { return width; }
	int get_height() const { return height; }
	const uint32_t *get_pixels() const { return color.data(); }
	uint32_t *get_pixels_mut() { return color.data(); }

	// Starts a frame: sets the camera, lights and environment.
	void begin(const Camera3D &p_camera, const Environment3D &p_env, const std::vector<PointLight3D> &p_lights);
	// Queues a mesh. p_object_id is written to the ID buffer (0 = not pickable).
	void draw_mesh(const Mesh &p_mesh, const Mat4 &p_model, const Material3D &p_material, uint32_t p_object_id = 0);
	// Queues a 3D line drawn after the meshes (p_color is 0xAARRGGBB).
	void draw_line(const Vec3 &p_a, const Vec3 &p_b, uint32_t p_color, bool p_depth_test = true);
	// Renders everything queued since begin().
	void end();

	uint32_t pick(int p_x, int p_y) const;
	// World-space ray from a pixel (for editor interaction).
	void screen_ray(float p_x, float p_y, Vec3 &r_origin, Vec3 &r_dir) const;
	bool world_to_screen(const Vec3 &p_world, float &r_x, float &r_y) const;

	const Mat4 &get_view() const { return view; }
	const Mat4 &get_projection() const { return proj; }
	size_t get_triangle_count() const { return triangles.size(); }

private:
	struct ShadeVertex {
		Vec4 clip;
		Vec3 world;
		Vec3 normal;
		Vec2 uv;
	};
	struct ScreenVertex {
		float x, y, z, inv_w;
		Vec3 world_w; // Attributes divided by w (perspective-correct interpolation).
		Vec3 normal_w;
		Vec2 uv_w;
	};
	struct Triangle {
		ScreenVertex v[3];
		const Material3D *material;
		uint32_t object_id;
		bool front;
		int min_y, max_y;
	};
	struct Line {
		Vec3 a, b;
		uint32_t color;
		bool depth_test;
	};
	struct ShadowCaster {
		const Mesh *mesh;
		Mat4 model;
	};

	int width = 0;
	int height = 0;
	std::vector<uint32_t> color;
	std::vector<float> depth;
	std::vector<uint32_t> ids;

	Camera3D camera;
	Environment3D env;
	std::vector<PointLight3D> lights;
	Mat4 view, proj, view_proj, inv_view_proj;
	Vec3 sun_dir_to_light;

	std::vector<Triangle> triangles;
	std::vector<Line> lines;
	std::vector<std::unique_ptr<Material3D>> materials; // Copies owned for the frame.
	std::vector<ShadowCaster> shadow_casters;

	// Shadow map (orthographic, from the sun).
	int shadow_size = 1024;
	std::vector<float> shadow_map;
	Mat4 shadow_view_proj;
	bool shadow_valid = false;

	void _clip_and_queue(const ShadeVertex *p_verts, const Material3D *p_material, uint32_t p_id);
	void _rasterize_band(int p_y0, int p_y1);
	void _rasterize_triangle(const Triangle &p_tri, int p_y0, int p_y1);
	Vec3 _shade(const Material3D &p_mat, const Vec3 &p_world, Vec3 p_normal, const Vec2 &p_uv, bool p_front) const;
	void _draw_sky();
	void _build_shadow_map();
	float _shadow_factor(const Vec3 &p_world, const Vec3 &p_normal) const;
	void _draw_lines();
};

// Color helpers (sRGB 0xAARRGGBB <-> linear).
Vec3 argb_to_linear(uint32_t p_argb);
uint32_t linear_to_argb(const Vec3 &p_color);

} // namespace ipde
