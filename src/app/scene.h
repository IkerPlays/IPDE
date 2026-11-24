#pragma once

// IPDE scenes (2D or 3D): a list of entities with built-in behaviors.

#include "core/json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ipde {

enum class EntityType {
	// 2D.
	RECT,
	CIRCLE,
	TEXT,
	// 3D.
	CUBE,
	SPHERE,
	PLANE,
	CYLINDER,
	CONE,
	TORUS,
	CAPSULE,
	MODEL, // Wavefront OBJ file inside the project.
	LIGHT, // Point light.
	TERRAIN, // Hilly ground generated from a seed.
};

bool entity_type_is_3d(EntityType p_type);
const char *entity_type_name(EntityType p_type);

struct Entity {
	std::string name = "Entidad";
	EntityType type = EntityType::RECT;
	float x = 0.0f; // Center position.
	float y = 0.0f;
	float z = 0.0f; // 3D only.
	float width = 64.0f; // Size. Diameter for circles. In 3D: size along X.
	float height = 64.0f; // In 3D: size along Y.
	float depth = 1.0f; // 3D only: size along Z.
	float rotation = 0.0f; // Degrees. In 3D: rotation around Z.
	float rot_x = 0.0f; // 3D only (degrees).
	float rot_y = 0.0f; // 3D only (degrees).
	uint32_t color = 0xFF6C7BFF; // 0xAARRGGBB
	std::string text = "Texto";
	float text_size = 32.0f;
	bool visible = true;

	// 3D material.
	float roughness = 0.6f;
	float metallic = 0.0f;
	float emission = 0.0f; // Glow strength (uses the color).
	std::string model; // Path relative to the project (MODEL).
	std::string texture; // Optional PNG, relative to the project. In 2D, rectangles become sprites.
	bool flip_x = false; // 2D sprite mirrored horizontally (players face where they walk).
	float uv_scale = 1.0f;
	int seed = 1; // Terrain shape.
	int pattern = 0; // 3D procedural pattern (Material3D::Pattern).
	float pattern_scale = 1.0f;
	// Lights.
	float light_energy = 2.0f;
	float light_range = 8.0f;

	// Built-in behaviors.
	float spin_speed = 0.0f; // Degrees per second (around Y in 3D).
	float bob_amplitude = 0.0f; // Pixels (2D) or meters (3D).
	float bob_frequency = 0.5f; // Cycles per second.
	bool player = false; // Moved with WASD / arrow keys.
	float player_speed = 300.0f; // Pixels or meters per second.
	bool can_jump = true; // 3D: Space jumps (with gravity).
	float jump_force = 6.0f;
	bool camera_follow = false;
	bool collectible = false; // Disappears and adds a point when the player touches it.
	bool solid = true; // The player collides with it.
	// Moving platform: goes from its position to position + move and back.
	float move_x = 0.0f, move_y = 0.0f, move_z = 0.0f;
	float move_time = 2.0f; // Seconds for one way.
	bool hazard = false; // Touching it sends the player back to the start.
	bool goal = false; // Touching it completes the level.
	float bounce = 0.0f; // Jump pad: upward speed given to the player (0 = off).
	float chase_speed = 0.0f; // Enemy: follows the player when near (0 = off).
	float chase_range = 8.0f;
	std::string sound; // WAV played when it is collected or touched (instead of the built-in effect).
	std::string message; // Shown on screen while the player touches it (signs, characters...).
	bool door = false; // Disappears when every coin of the level has been collected.
	bool checkpoint = false; // Touching it makes the player respawn there.
	std::string switch_target; // Touching it shows/hides every entity with this name (once).
	bool triggered = false; // Runtime only (checkpoints and switches already used).

	bool has_motion() const { return move_x != 0.0f || move_y != 0.0f || move_z != 0.0f; }
};

struct Environment {
	uint32_t sky_top = 0xFF5B8FDE;
	uint32_t sky_horizon = 0xFFC8D6EC;
	uint32_t ground = 0xFF6E6358;
	float ambient = 0.5f;
	float sun_pitch = 50.0f; // Degrees above the horizon.
	float sun_yaw = 35.0f; // Degrees.
	uint32_t sun_color = 0xFFFFF4E0;
	float sun_energy = 1.4f;
	bool shadows = true;
	bool fog = true;
	float fog_start = 30.0f;
	float fog_end = 140.0f;
};

struct Scene {
	std::string name = "Main";
	bool is_3d = false;
	uint32_t background = 0xFF1B1D2E; // 2D background.
	bool platformer = false; // 2D: gravity and jumping (side view) instead of top-down movement.
	float gravity_2d = 1800.0f; // Pixels per second squared.
	Environment environment; // 3D.
	// 3D default camera (used when no entity has camera_follow).
	float camera_x = 0, camera_y = 4, camera_z = 10;
	float camera_target_x = 0, camera_target_y = 0, camera_target_z = 0;
	float camera_fov = 60.0f;
	int camera_mode = 0; // 3D camera that follows the player: 0 = third person, 1 = first person, 2 = top-down.
	// Rules.
	int lives = 0; // Falls allowed before "game over" (0 = unlimited).
	bool show_timer = false; // Shows the level time in the HUD.
	// Audio.
	std::string music; // WAV file (relative to the project) played in a loop.
	float music_volume = 0.6f;
	bool sound_effects = true; // Built-in effects (coins, jumps, goal...).
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
	// Scene files (levels) in play order; the first one is where the game starts.
	std::vector<std::string> scenes = { "main.scene" };
	bool start_fullscreen = false; // The game starts in fullscreen (F11 toggles it).

	std::string get_file() const;
	std::string get_scene_file() const; // First scene.
	std::string get_scene_path(const std::string &p_file) const;
	// A free file name for a new level ("nivel 2.scene", ...).
	std::string make_scene_file_name() const;
	// Creates a small starter scene (ground and player) and adds it to the list.
	bool add_scene(bool p_3d, std::string &r_file);
	bool save() const;
	static bool load(const std::string &p_folder, Project &r_project);
	// Creates the folder, the project file and an example scene (2D or 3D).
	static bool create(const std::string &p_folder, const std::string &p_name, bool p_3d, std::string &r_error);
};

} // namespace ipde
