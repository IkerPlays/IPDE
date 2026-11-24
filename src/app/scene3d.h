#pragma once

// Draws IPDE 3D scenes with the software 3D renderer (shared by the editor and the game).

#include "app/scene.h"
#include "render3d/renderer3d.h"

#include <map>
#include <memory>
#include <string>

namespace ipde {

class SceneRenderer3D {
public:
	// Queues the whole scene into p_renderer (between begin() and end()).
	// Entity i gets the picking ID i + 1.
	void draw(Renderer3D &p_renderer, const Scene &p_scene, const std::string &p_project_dir, double p_time, bool p_animate, bool p_editor);

	static Environment3D make_environment(const Scene &p_scene);
	static std::vector<PointLight3D> make_lights(const Scene &p_scene, double p_time, bool p_animate);
	static Mat4 entity_transform(const Entity &p_e, double p_time, bool p_animate);
	static Vec3 entity_position(const Entity &p_e, double p_time, bool p_animate);

	std::shared_ptr<Mesh> get_mesh(const Entity &p_e, const std::string &p_project_dir);
	const Texture3D *get_texture(const std::string &p_project_dir, const std::string &p_relative);

private:
	std::map<EntityType, std::shared_ptr<Mesh>> primitives;
	std::map<std::string, std::unique_ptr<Texture3D>> textures;
};

} // namespace ipde
