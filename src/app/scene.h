#pragma once

// IPDE 2D scenes: a list of entities with simple built-in behaviors.

#include "core/json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ipde {

enum class EntityType {
	RECT,
	CIRCLE,
	TEXT,
};

struct Entity {
	std::string name = "Entidad";
	EntityType type = EntityType::RECT;
	float x = 0.0f; // Center position.
	float y = 0.0f;
	float width = 64.0f; // Diameter for circles.
	float height = 64.0f;
	float rotation = 0.0f; // Degrees.
	uint32_t color = 0xFF6C7BFF; // 0xAARRGGBB
	std::string text = "Texto";
	float text_size = 32.0f;
	bool visible = true;

	// Built-in behaviors.
	float spin_speed = 0.0f; // Degrees per second.
	float bob_amplitude = 0.0f; // Pixels.
	float bob_frequency = 0.5f; // Cycles per second.
	bool player = false; // Moved with WASD / arrow keys.
	float player_speed = 300.0f;
	bool camera_follow = false;
};

struct Scene {
	std::string name = "Main";
	uint32_t background = 0xFF1B1D2E;
	std::vector<Entity> entities;

	Json to_json() const;
	static Scene from_json(const Json &p_json);
	bool save(const std::string &p_path) const;
	static bool load(const std::string &p_path, Scene &r_scene);
};

struct Project {
	std::string name;
	std::string path; // Project folder.
	std::string engine_version;

	std::string get_file() const;
	std::string get_scene_file() const;
	bool save() const;
	static bool load(const std::string &p_folder, Project &r_project);
	// Creates the folder, the project file and an example scene.
	static bool create(const std::string &p_folder, const std::string &p_name, std::string &r_error);
};

const char *entity_type_name(EntityType p_type);

} // namespace ipde
