#include "app/scene.h"

#include "core/fs.h"
#include "version.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ipde {

struct TypeInfo {
	EntityType type;
	const char *id;
	const char *name;
};

static const TypeInfo TYPES[] = {
	{ EntityType::RECT, "rect", "Rectángulo" },
	{ EntityType::CIRCLE, "circle", "Círculo" },
	{ EntityType::TEXT, "text", "Texto" },
	{ EntityType::CUBE, "cube", "Cubo" },
	{ EntityType::SPHERE, "sphere", "Esfera" },
	{ EntityType::PLANE, "plane", "Plano" },
	{ EntityType::CYLINDER, "cylinder", "Cilindro" },
	{ EntityType::CONE, "cone", "Cono" },
	{ EntityType::TORUS, "torus", "Toro" },
	{ EntityType::CAPSULE, "capsule", "Cápsula" },
	{ EntityType::MODEL, "model", "Modelo" },
	{ EntityType::LIGHT, "light", "Luz" },
};

const char *entity_type_name(EntityType p_type) {
	for (const TypeInfo &t : TYPES) {
		if (t.type == p_type) {
			return t.name;
		}
	}
	return "?";
}

bool entity_type_is_3d(EntityType p_type) {
	return p_type != EntityType::RECT && p_type != EntityType::CIRCLE && p_type != EntityType::TEXT;
}

static const char *type_id(EntityType p_type) {
	for (const TypeInfo &t : TYPES) {
		if (t.type == p_type) {
			return t.id;
		}
	}
	return "rect";
}

static EntityType type_from_id(const std::string &p_id) {
	for (const TypeInfo &t : TYPES) {
		if (p_id == t.id) {
			return t.type;
		}
	}
	return EntityType::RECT;
}

static std::string color_to_hex(uint32_t p_color) {
	char buf[16];
	snprintf(buf, sizeof(buf), "#%08X", p_color);
	return buf;
}

static uint32_t color_from_hex(const std::string &p_hex, uint32_t p_default) {
	if (p_hex.size() != 9 || p_hex[0] != '#') {
		return p_default;
	}
	return (uint32_t)strtoul(p_hex.c_str() + 1, nullptr, 16);
}

Json Scene::to_json() const {
	Json j = Json::object();
	j["format"] = 2;
	j["name"] = name;
	j["mode"] = is_3d ? "3d" : "2d";
	j["background"] = color_to_hex(background);
	j["platformer"] = platformer;
	j["gravity_2d"] = (double)gravity_2d;

	Json env = Json::object();
	env["sky_top"] = color_to_hex(environment.sky_top);
	env["sky_horizon"] = color_to_hex(environment.sky_horizon);
	env["ground"] = color_to_hex(environment.ground);
	env["ambient"] = (double)environment.ambient;
	env["sun_pitch"] = (double)environment.sun_pitch;
	env["sun_yaw"] = (double)environment.sun_yaw;
	env["sun_color"] = color_to_hex(environment.sun_color);
	env["sun_energy"] = (double)environment.sun_energy;
	env["shadows"] = environment.shadows;
	env["fog"] = environment.fog;
	env["fog_start"] = (double)environment.fog_start;
	env["fog_end"] = (double)environment.fog_end;
	j["environment"] = env;

	Json cam = Json::object();
	cam["x"] = (double)camera_x;
	cam["y"] = (double)camera_y;
	cam["z"] = (double)camera_z;
	cam["target_x"] = (double)camera_target_x;
	cam["target_y"] = (double)camera_target_y;
	cam["target_z"] = (double)camera_target_z;
	cam["fov"] = (double)camera_fov;
	j["camera"] = cam;

	Json audio = Json::object();
	audio["music"] = music;
	audio["music_volume"] = (double)music_volume;
	audio["effects"] = sound_effects;
	j["audio"] = audio;

	Json list = Json::array();
	for (const Entity &e : entities) {
		Json je = Json::object();
		je["name"] = e.name;
		je["type"] = type_id(e.type);
		je["x"] = (double)e.x;
		je["y"] = (double)e.y;
		je["z"] = (double)e.z;
		je["width"] = (double)e.width;
		je["height"] = (double)e.height;
		je["depth"] = (double)e.depth;
		je["rotation"] = (double)e.rotation;
		je["rot_x"] = (double)e.rot_x;
		je["rot_y"] = (double)e.rot_y;
		je["color"] = color_to_hex(e.color);
		je["text"] = e.text;
		je["text_size"] = (double)e.text_size;
		je["visible"] = e.visible;
		je["roughness"] = (double)e.roughness;
		je["metallic"] = (double)e.metallic;
		je["emission"] = (double)e.emission;
		je["model"] = e.model;
		je["texture"] = e.texture;
		if (e.flip_x) {
			je["flip_x"] = true;
		}
		je["uv_scale"] = (double)e.uv_scale;
		if (e.pattern != 0) {
			je["pattern"] = e.pattern;
			je["pattern_scale"] = (double)e.pattern_scale;
		}
		je["light_energy"] = (double)e.light_energy;
		je["light_range"] = (double)e.light_range;
		je["spin_speed"] = (double)e.spin_speed;
		je["bob_amplitude"] = (double)e.bob_amplitude;
		je["bob_frequency"] = (double)e.bob_frequency;
		je["player"] = e.player;
		je["player_speed"] = (double)e.player_speed;
		je["can_jump"] = e.can_jump;
		je["jump_force"] = (double)e.jump_force;
		je["camera_follow"] = e.camera_follow;
		je["collectible"] = e.collectible;
		je["solid"] = e.solid;
		if (e.has_motion()) {
			je["move_x"] = (double)e.move_x;
			je["move_y"] = (double)e.move_y;
			je["move_z"] = (double)e.move_z;
			je["move_time"] = (double)e.move_time;
		}
		je["hazard"] = e.hazard;
		je["goal"] = e.goal;
		je["bounce"] = (double)e.bounce;
		je["chase_speed"] = (double)e.chase_speed;
		je["chase_range"] = (double)e.chase_range;
		if (!e.sound.empty()) {
			je["sound"] = e.sound;
		}
		if (e.door) {
			je["door"] = true;
		}
		if (e.checkpoint) {
			je["checkpoint"] = true;
		}
		if (!e.switch_target.empty()) {
			je["switch_target"] = e.switch_target;
		}
		list.push_back(je);
	}
	j["entities"] = list;
	return j;
}

Scene Scene::from_json(const Json &p_json) {
	Scene s;
	s.name = p_json["name"].as_string("Main");
	s.is_3d = p_json["mode"].as_string("2d") == "3d";
	s.background = color_from_hex(p_json["background"].as_string(), s.background);
	s.platformer = p_json["platformer"].as_bool(false);
	s.gravity_2d = (float)p_json["gravity_2d"].as_number(s.gravity_2d);

	const Json &env = p_json["environment"];
	Environment &e = s.environment;
	e.sky_top = color_from_hex(env["sky_top"].as_string(), e.sky_top);
	e.sky_horizon = color_from_hex(env["sky_horizon"].as_string(), e.sky_horizon);
	e.ground = color_from_hex(env["ground"].as_string(), e.ground);
	e.ambient = (float)env["ambient"].as_number(e.ambient);
	e.sun_pitch = (float)env["sun_pitch"].as_number(e.sun_pitch);
	e.sun_yaw = (float)env["sun_yaw"].as_number(e.sun_yaw);
	e.sun_color = color_from_hex(env["sun_color"].as_string(), e.sun_color);
	e.sun_energy = (float)env["sun_energy"].as_number(e.sun_energy);
	e.shadows = env["shadows"].as_bool(e.shadows);
	e.fog = env["fog"].as_bool(e.fog);
	e.fog_start = (float)env["fog_start"].as_number(e.fog_start);
	e.fog_end = (float)env["fog_end"].as_number(e.fog_end);

	const Json &cam = p_json["camera"];
	s.camera_x = (float)cam["x"].as_number(s.camera_x);
	s.camera_y = (float)cam["y"].as_number(s.camera_y);
	s.camera_z = (float)cam["z"].as_number(s.camera_z);
	s.camera_target_x = (float)cam["target_x"].as_number(s.camera_target_x);
	s.camera_target_y = (float)cam["target_y"].as_number(s.camera_target_y);
	s.camera_target_z = (float)cam["target_z"].as_number(s.camera_target_z);
	s.camera_fov = (float)cam["fov"].as_number(s.camera_fov);

	const Json &audio = p_json["audio"];
	s.music = audio["music"].as_string();
	s.music_volume = (float)audio["music_volume"].as_number(s.music_volume);
	s.sound_effects = audio["effects"].as_bool(true);

	for (const Json &je : p_json["entities"].get_items()) {
		Entity en;
		en.name = je["name"].as_string(en.name);
		en.type = type_from_id(je["type"].as_string());
		en.x = (float)je["x"].as_number(en.x);
		en.y = (float)je["y"].as_number(en.y);
		en.z = (float)je["z"].as_number(en.z);
		en.width = (float)je["width"].as_number(en.width);
		en.height = (float)je["height"].as_number(en.height);
		en.depth = (float)je["depth"].as_number(entity_type_is_3d(en.type) ? 1.0 : en.depth);
		en.rotation = (float)je["rotation"].as_number(en.rotation);
		en.rot_x = (float)je["rot_x"].as_number(0);
		en.rot_y = (float)je["rot_y"].as_number(0);
		en.color = color_from_hex(je["color"].as_string(), en.color);
		en.text = je["text"].as_string(en.text);
		en.text_size = (float)je["text_size"].as_number(en.text_size);
		en.visible = je["visible"].as_bool(true);
		en.roughness = (float)je["roughness"].as_number(en.roughness);
		en.metallic = (float)je["metallic"].as_number(en.metallic);
		en.emission = (float)je["emission"].as_number(en.emission);
		en.model = je["model"].as_string();
		en.texture = je["texture"].as_string();
		en.flip_x = je["flip_x"].as_bool(false);
		en.uv_scale = (float)je["uv_scale"].as_number(1.0);
		en.pattern = std::clamp((int)je["pattern"].as_number(0), 0, 5);
		en.pattern_scale = (float)je["pattern_scale"].as_number(1.0);
		en.light_energy = (float)je["light_energy"].as_number(en.light_energy);
		en.light_range = (float)je["light_range"].as_number(en.light_range);
		en.spin_speed = (float)je["spin_speed"].as_number(0);
		en.bob_amplitude = (float)je["bob_amplitude"].as_number(0);
		en.bob_frequency = (float)je["bob_frequency"].as_number(0.5);
		en.player = je["player"].as_bool(false);
		en.player_speed = (float)je["player_speed"].as_number(entity_type_is_3d(en.type) ? 5.0 : 300.0);
		en.can_jump = je["can_jump"].as_bool(true);
		en.jump_force = (float)je["jump_force"].as_number(6.0);
		en.camera_follow = je["camera_follow"].as_bool(false);
		en.collectible = je["collectible"].as_bool(false);
		en.solid = je["solid"].as_bool(true);
		en.move_x = (float)je["move_x"].as_number(0);
		en.move_y = (float)je["move_y"].as_number(0);
		en.move_z = (float)je["move_z"].as_number(0);
		en.move_time = (float)je["move_time"].as_number(2.0);
		en.hazard = je["hazard"].as_bool(false);
		en.goal = je["goal"].as_bool(false);
		en.bounce = (float)je["bounce"].as_number(0);
		en.chase_speed = (float)je["chase_speed"].as_number(0);
		en.sound = je["sound"].as_string();
		en.door = je["door"].as_bool(false);
		en.checkpoint = je["checkpoint"].as_bool(false);
		en.switch_target = je["switch_target"].as_string();
		en.chase_range = (float)je["chase_range"].as_number(entity_type_is_3d(en.type) ? 8.0 : 300.0);
		s.entities.push_back(en);
	}
	return s;
}

bool Scene::save(const std::string &p_path) const {
	return fs::write_file(p_path, to_json().dump(true));
}

bool Scene::load(const std::string &p_path, Scene &r_scene) {
	std::string text;
	if (!fs::read_file(p_path, text)) {
		return false;
	}
	std::string error;
	const Json j = Json::parse(text, &error);
	if (!j.is_object()) {
		return false;
	}
	r_scene = from_json(j);
	return true;
}

std::string Project::get_file() const {
	return fs::join(path, "project.ipde");
}

std::string Project::get_scene_file() const {
	return get_scene_path(scenes.empty() ? std::string("main.scene") : scenes[0]);
}

std::string Project::get_scene_path(const std::string &p_file) const {
	return fs::join(path, p_file);
}

std::string Project::make_scene_file_name() const {
	for (int i = (int)scenes.size() + 1;; i++) {
		const std::string name = "nivel " + std::to_string(i) + ".scene";
		if (std::find(scenes.begin(), scenes.end(), name) == scenes.end() && !fs::exists(get_scene_path(name))) {
			return name;
		}
	}
}

bool Project::save() const {
	Json j = Json::object();
	j["format"] = 1;
	j["name"] = name;
	j["engine_version"] = engine_version.empty() ? std::string(IPDE_VERSION_NUMBER) : engine_version;
	j["main_scene"] = scenes.empty() ? std::string("main.scene") : scenes[0];
	Json list = Json::array();
	for (const std::string &sc : scenes) {
		list.push_back(sc);
	}
	j["scenes"] = list;
	return fs::write_file(get_file(), j.dump(true));
}

bool Project::load(const std::string &p_folder, Project &r_project) {
	std::string text;
	if (!fs::read_file(fs::join(p_folder, "project.ipde"), text)) {
		return false;
	}
	const Json j = Json::parse(text);
	if (!j.is_object()) {
		return false;
	}
	r_project.path = p_folder;
	r_project.name = j["name"].as_string(fs::get_file_name(p_folder));
	r_project.engine_version = j["engine_version"].as_string();
	r_project.scenes.clear();
	for (const Json &sc : j["scenes"].get_items()) {
		const std::string f = sc.as_string();
		// Only plain file names inside the project folder.
		if (!f.empty() && f.find("..") == std::string::npos && std::find(r_project.scenes.begin(), r_project.scenes.end(), f) == r_project.scenes.end()) {
			r_project.scenes.push_back(f);
		}
	}
	if (r_project.scenes.empty()) {
		r_project.scenes.push_back(j["main_scene"].as_string("main.scene"));
	}
	return true;
}

static Scene example_scene_2d(const std::string &p_name) {
	Scene s;
	s.platformer = true;
	Entity ground;
	ground.name = "Suelo";
	ground.x = 0;
	ground.y = 220;
	ground.width = 900;
	ground.height = 40;
	ground.color = 0xFF3A3F66;
	s.entities.push_back(ground);

	Entity player;
	player.name = "Jugador";
	player.x = -200;
	player.y = 150;
	player.width = 64;
	player.height = 64;
	player.color = 0xFF6C7BFF;
	player.player = true;
	s.entities.push_back(player);

	for (int i = 0; i < 3; i++) {
		Entity coin;
		coin.name = "Moneda " + std::to_string(i + 1);
		coin.x = 60.0f + i * 110.0f;
		coin.y = 100;
		coin.width = 36;
		coin.height = 36;
		coin.rotation = 45;
		coin.color = 0xFFFFC53D;
		coin.spin_speed = 120;
		coin.bob_amplitude = 10;
		coin.bob_frequency = 0.8f;
		coin.collectible = true;
		coin.solid = false;
		s.entities.push_back(coin);
	}

	Entity wall;
	wall.name = "Muro móvil";
	wall.x = 0;
	wall.y = -40;
	wall.width = 40;
	wall.height = 120;
	wall.color = 0xFF8C9BFF;
	wall.move_y = 160;
	wall.move_time = 1.5f;
	s.entities.push_back(wall);

	Entity enemy;
	enemy.name = "Enemigo";
	enemy.type = EntityType::CIRCLE;
	enemy.x = 300;
	enemy.y = 178;
	enemy.width = 44;
	enemy.color = 0xFFFF4D5E;
	enemy.chase_speed = 90;
	enemy.chase_range = 260;
	enemy.hazard = true;
	enemy.solid = false;
	s.entities.push_back(enemy);

	Entity goal;
	goal.name = "Meta";
	goal.x = 400;
	goal.y = 160;
	goal.width = 50;
	goal.height = 80;
	goal.color = 0xFF3DDC84;
	goal.goal = true;
	goal.solid = false;
	s.entities.push_back(goal);

	Entity title;
	title.name = "Título";
	title.type = EntityType::TEXT;
	title.x = 0;
	title.y = -160;
	title.text = p_name;
	title.text_size = 48;
	title.color = 0xFFFFFFFF;
	title.solid = false;
	s.entities.push_back(title);
	return s;
}

static Entity make3d(const char *p_name, EntityType p_type, float x, float y, float z, float w, float h, float d, uint32_t p_color) {
	Entity e;
	e.name = p_name;
	e.type = p_type;
	e.x = x;
	e.y = y;
	e.z = z;
	e.width = w;
	e.height = h;
	e.depth = d;
	e.color = p_color;
	e.player_speed = 5.0f;
	return e;
}

static Scene example_scene_3d() {
	Scene s;
	s.is_3d = true;
	s.camera_x = 0;
	s.camera_y = 6;
	s.camera_z = 12;

	Entity ground = make3d("Suelo", EntityType::CUBE, 0, -0.25f, 0, 24, 0.5f, 24, 0xFF4B5578);
	ground.roughness = 0.9f;
	ground.pattern = 1; // Checker.
	ground.pattern_scale = 2.0f;
	s.entities.push_back(ground);

	Entity player = make3d("Jugador", EntityType::CAPSULE, 0, 1, 4, 1, 2, 1, 0xFF6C7BFF);
	player.player = true;
	player.camera_follow = true;
	player.roughness = 0.35f;
	s.entities.push_back(player);

	const uint32_t box_colors[] = { 0xFFFF7896, 0xFF4DD6A0, 0xFF50B4FF };
	for (int i = 0; i < 3; i++) {
		Entity box = make3d(("Caja " + std::to_string(i + 1)).c_str(), EntityType::CUBE, -5.0f + i * 5.0f, 0.75f + i * 0.5f, -3.0f, 2, 1.5f + i, 2, box_colors[i]);
		box.y = (1.5f + i) * 0.5f;
		s.entities.push_back(box);
	}

	for (int i = 0; i < 5; i++) {
		Entity coin = make3d(("Moneda " + std::to_string(i + 1)).c_str(), EntityType::TORUS, -6.0f + i * 3.0f, 1.0f, 1.0f, 0.8f, 0.8f, 0.8f, 0xFFFFC53D);
		coin.rot_x = 90;
		coin.metallic = 1.0f;
		coin.roughness = 0.25f;
		coin.spin_speed = 90;
		coin.bob_amplitude = 0.2f;
		coin.bob_frequency = 0.6f;
		coin.collectible = true;
		coin.solid = false;
		s.entities.push_back(coin);
	}
	// A coin on top of the tallest box.
	Entity top_coin = make3d("Moneda alta", EntityType::TORUS, 5.0f, 4.3f, -3.0f, 0.8f, 0.8f, 0.8f, 0xFFFFC53D);
	top_coin.rot_x = 90;
	top_coin.metallic = 1.0f;
	top_coin.roughness = 0.25f;
	top_coin.spin_speed = 90;
	top_coin.collectible = true;
	top_coin.solid = false;
	s.entities.push_back(top_coin);

	// A moving platform that lifts the player to a tower with the goal on top.
	Entity tower = make3d("Torre", EntityType::CUBE, -8, 2, -8.5f, 3, 4, 3, 0xFFB0705A);
	tower.pattern = 2; // Bricks.
	tower.pattern_scale = 0.6f;
	s.entities.push_back(tower);
	Entity lift = make3d("Plataforma móvil", EntityType::CUBE, -8, 0.2f, -5.0f, 3, 0.4f, 3, 0xFFE8E8F0);
	lift.move_y = 3.8f;
	lift.move_time = 2.5f;
	lift.roughness = 0.4f;
	s.entities.push_back(lift);
	Entity goal = make3d("Meta", EntityType::CYLINDER, -8, 5.0f, -8.5f, 0.9f, 2.0f, 0.9f, 0xFF3DDC84);
	goal.emission = 0.8f;
	goal.spin_speed = 60;
	goal.goal = true;
	goal.solid = false;
	s.entities.push_back(goal);

	Entity pad = make3d("Trampolín", EntityType::CYLINDER, -9, 0.1f, 6, 2.2f, 0.2f, 2.2f, 0xFFFF8A3D);
	pad.bounce = 13.0f;
	pad.emission = 0.4f;
	s.entities.push_back(pad);

	Entity enemy = make3d("Enemigo", EntityType::SPHERE, 9, 0.6f, 6, 1.2f, 1.2f, 1.2f, 0xFFFF4D5E);
	enemy.chase_speed = 2.6f;
	enemy.chase_range = 6.0f;
	enemy.hazard = true;
	enemy.solid = false;
	enemy.roughness = 0.3f;
	s.entities.push_back(enemy);

	Entity orb = make3d("Orbe", EntityType::SPHERE, 10, 1.5f, -2, 1.5f, 1.5f, 1.5f, 0xFF8B3DFF);
	orb.emission = 1.5f;
	orb.bob_amplitude = 0.4f;
	orb.solid = false;
	s.entities.push_back(orb);

	Entity light = make3d("Luz", EntityType::LIGHT, 10, 2.5f, -2, 0.3f, 0.3f, 0.3f, 0xFFB07CFF);
	light.light_energy = 3.0f;
	light.light_range = 9.0f;
	light.solid = false;
	s.entities.push_back(light);
	return s;
}

bool Project::add_scene(bool p_3d, std::string &r_file) {
	Scene s;
	s.is_3d = p_3d;
	r_file = make_scene_file_name();
	s.name = r_file.substr(0, r_file.size() - 6);
	if (p_3d) {
		Entity ground = make3d("Suelo", EntityType::CUBE, 0, -0.25f, 0, 24, 0.5f, 24, 0xFF4B5578);
		ground.roughness = 0.9f;
		s.entities.push_back(ground);
		Entity player = make3d("Jugador", EntityType::CAPSULE, 0, 1, 4, 1, 2, 1, 0xFF6C7BFF);
		player.player = true;
		player.camera_follow = true;
		player.player_speed = 5.0f;
		s.entities.push_back(player);
		Entity goal = make3d("Meta", EntityType::CYLINDER, 0, 1.0f, -8, 0.9f, 2.0f, 0.9f, 0xFF3DDC84);
		goal.emission = 0.8f;
		goal.goal = true;
		goal.solid = false;
		s.entities.push_back(goal);
	} else {
		Entity ground;
		ground.name = "Suelo";
		ground.y = 220;
		ground.width = 900;
		ground.height = 40;
		ground.color = 0xFF3A3F66;
		s.entities.push_back(ground);
		Entity player;
		player.name = "Jugador";
		player.x = -300;
		player.y = 150;
		player.width = 64;
		player.height = 64;
		player.color = 0xFF6C7BFF;
		player.player = true;
		s.entities.push_back(player);
		Entity goal;
		goal.name = "Meta";
		goal.x = 380;
		goal.y = 160;
		goal.width = 50;
		goal.height = 80;
		goal.color = 0xFF3DDC84;
		goal.goal = true;
		goal.solid = false;
		s.entities.push_back(goal);
	}
	if (!s.save(get_scene_path(r_file))) {
		return false;
	}
	scenes.push_back(r_file);
	return save();
}

bool Project::create(const std::string &p_folder, const std::string &p_name, bool p_3d, std::string &r_error) {
	if (fs::exists(fs::join(p_folder, "project.ipde"))) {
		r_error = "Ya existe un proyecto en esa carpeta.";
		return false;
	}
	if (!fs::make_dirs(p_folder)) {
		r_error = "No se pudo crear la carpeta del proyecto.";
		return false;
	}
	Project p;
	p.name = p_name;
	p.path = p_folder;
	p.engine_version = IPDE_VERSION_NUMBER;
	if (!p.save()) {
		r_error = "No se pudo escribir project.ipde.";
		return false;
	}
	const Scene s = p_3d ? example_scene_3d() : example_scene_2d(p_name);
	if (!s.save(p.get_scene_file())) {
		r_error = "No se pudo escribir la escena.";
		return false;
	}
	return true;
}

} // namespace ipde
