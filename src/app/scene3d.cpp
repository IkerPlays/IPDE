#include "app/scene3d.h"

#include "core/fs.h"

#include <stb/stb_image.h>

#include <cmath>

namespace ipde {

Environment3D SceneRenderer3D::make_environment(const Scene &p_scene, double p_time) {
	const Environment &e = p_scene.environment;
	Environment3D env;
	env.sky_top = argb_to_linear(e.sky_top);
	env.sky_horizon = argb_to_linear(e.sky_horizon);
	env.ground = argb_to_linear(e.ground);
	env.ambient = e.ambient;
	const float pitch = e.sun_pitch * DEG2RAD;
	const float yaw = e.sun_yaw * DEG2RAD;
	// Direction the sunlight travels (from the sky towards the ground).
	env.sun_direction = Vec3(-std::cos(pitch) * std::sin(yaw), -std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
	env.sun_color = argb_to_linear(e.sun_color);
	env.sun_energy = e.sun_energy;
	env.shadows = e.shadows;
	env.fog = e.fog;
	env.fog_start = e.fog_start;
	env.fog_end = e.fog_end;
	env.time = (float)p_time;
	return env;
}

float SceneRenderer3D::motion_factor(const Entity &p_e, double p_time) {
	const double period = std::max(0.05, (double)p_e.move_time) * 2.0;
	return (float)(0.5 - 0.5 * std::cos(p_time / period * 2.0 * PI));
}

Vec3 SceneRenderer3D::entity_position(const Entity &p_e, double p_time, bool p_animate) {
	Vec3 p(p_e.x, p_e.y, p_e.z);
	if (p_animate && p_e.bob_amplitude != 0.0f) {
		p.y += p_e.bob_amplitude * (float)std::sin(p_time * p_e.bob_frequency * 2.0 * PI);
	}
	if (p_animate && p_e.has_motion()) {
		p += Vec3(p_e.move_x, p_e.move_y, p_e.move_z) * motion_factor(p_e, p_time);
	}
	return p;
}

Mat4 SceneRenderer3D::entity_transform(const Entity &p_e, double p_time, bool p_animate) {
	Vec3 rot(p_e.rot_x, p_e.rot_y, p_e.rotation);
	if (p_animate) {
		rot.y += (float)(p_e.spin_speed * p_time);
	}
	Vec3 size(p_e.width, p_e.height, p_e.depth);
	if (p_e.type == EntityType::SPHERE) {
		size = Vec3(p_e.width, p_e.width, p_e.width);
	}
	if (p_e.type == EntityType::LIGHT) {
		size = Vec3(0.3f, 0.3f, 0.3f);
		rot = Vec3();
	}
	// Spinning around the world Y axis looks better than around the local one.
	if (p_animate && p_e.spin_speed != 0.0f) {
		const Mat4 base = Mat4::rotate_euler(Vec3(p_e.rot_x, p_e.rot_y, p_e.rotation));
		return Mat4::translate(entity_position(p_e, p_time, p_animate)) * Mat4::rotate_y((float)(p_e.spin_speed * p_time) * DEG2RAD) * base * Mat4::scale(size);
	}
	return Mat4::translate(entity_position(p_e, p_time, p_animate)) * Mat4::rotate_euler(rot) * Mat4::scale(size);
}

std::vector<PointLight3D> SceneRenderer3D::make_lights(const Scene &p_scene, double p_time, bool p_animate) {
	std::vector<PointLight3D> lights;
	for (const Entity &e : p_scene.entities) {
		if (e.type == EntityType::LIGHT && e.visible) {
			PointLight3D l;
			l.position = entity_position(e, p_time, p_animate);
			l.color = argb_to_linear(e.color);
			l.energy = e.light_energy;
			l.range = std::max(0.1f, e.light_range);
			lights.push_back(l);
		}
	}
	return lights;
}

std::shared_ptr<Mesh> SceneRenderer3D::get_mesh(const Entity &p_e, const std::string &p_project_dir) {
	if (p_e.type == EntityType::MODEL) {
		auto m = p_e.model.empty() ? nullptr : meshes::load_obj(fs::join(p_project_dir, p_e.model));
		if (m) {
			return m;
		}
		Entity fallback;
		fallback.type = EntityType::CUBE;
		return get_mesh(fallback, p_project_dir);
	}
	if (p_e.type == EntityType::TERRAIN) {
		return meshes::terrain(p_e.seed); // Cached by seed.
	}
	if (p_e.type == EntityType::TREE) {
		return meshes::tree(p_e.seed % 8); // A few variations are enough.
	}
	if (p_e.type == EntityType::ROCK) {
		return meshes::rock(p_e.seed % 16);
	}
	auto it = primitives.find(p_e.type);
	if (it != primitives.end()) {
		return it->second;
	}
	std::shared_ptr<Mesh> m;
	switch (p_e.type) {
		case EntityType::SPHERE: m = meshes::sphere(18, 28); break;
		case EntityType::PLANE: m = meshes::plane(); break;
		case EntityType::CYLINDER: m = meshes::cylinder(); break;
		case EntityType::CONE: m = meshes::cone(); break;
		case EntityType::TORUS: m = meshes::torus(28, 14); break;
		case EntityType::CAPSULE: m = meshes::capsule(); break;
		case EntityType::LIGHT: m = meshes::sphere(8, 12); break;
		default: m = meshes::cube(); break;
	}
	primitives[p_e.type] = m;
	return m;
}

const Texture3D *SceneRenderer3D::get_texture(const std::string &p_project_dir, const std::string &p_relative) {
	if (p_relative.empty()) {
		return nullptr;
	}
	const std::string path = fs::join(p_project_dir, p_relative);
	auto it = textures.find(path);
	if (it != textures.end()) {
		return it->second.get();
	}
	std::string data;
	std::unique_ptr<Texture3D> tex;
	if (fs::read_file(path, data)) {
		int w = 0, h = 0, n = 0;
		unsigned char *px = stbi_load_from_memory((const unsigned char *)data.data(), (int)data.size(), &w, &h, &n, 4);
		if (px) {
			tex = std::make_unique<Texture3D>();
			tex->width = w;
			tex->height = h;
			tex->texels.resize((size_t)w * h);
			tex->alpha.resize((size_t)w * h);
			for (size_t i = 0; i < tex->texels.size(); i++) {
				const float r = px[i * 4] / 255.0f, g = px[i * 4 + 1] / 255.0f, b = px[i * 4 + 2] / 255.0f;
				tex->texels[i] = Vec3(r * r, g * g, b * b);
				tex->alpha[i] = px[i * 4 + 3] / 255.0f;
			}
			stbi_image_free(px);
		}
	}
	const Texture3D *result = tex.get();
	textures[path] = std::move(tex);
	return result;
}

void SceneRenderer3D::draw(Renderer3D &p_renderer, const Scene &p_scene, const std::string &p_project_dir, double p_time, bool p_animate, bool p_editor) {
	for (size_t i = 0; i < p_scene.entities.size(); i++) {
		const Entity &e = p_scene.entities[i];
		if (!e.visible || !entity_type_is_3d(e.type)) {
			continue;
		}
		if (e.type == EntityType::LIGHT && !p_editor) {
			continue; // Lights are invisible in the game.
		}
		std::shared_ptr<Mesh> mesh = get_mesh(e, p_project_dir);
		Material3D mat;
		mat.albedo = argb_to_linear(e.color);
		mat.roughness = e.roughness;
		mat.metallic = e.metallic;
		mat.emission = mat.albedo * e.emission;
		mat.double_sided = e.type == EntityType::PLANE || e.type == EntityType::MODEL;
		mat.texture = get_texture(p_project_dir, e.texture);
		mat.uv_scale = e.uv_scale;
		mat.pattern = e.pattern;
		mat.pattern_scale = e.pattern_scale;
		if (e.type == EntityType::LIGHT) {
			mat.unshaded = true;
			mat.cast_shadows = false;
		}
		p_renderer.draw_mesh(*mesh, entity_transform(e, p_time, p_animate), mat, (uint32_t)i + 1);
	}
}

} // namespace ipde
