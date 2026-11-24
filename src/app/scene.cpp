#include "app/scene.h"

#include "core/fs.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>

namespace ipde {

const char *entity_type_name(EntityType p_type) {
	switch (p_type) {
		case EntityType::RECT: return "Rectángulo";
		case EntityType::CIRCLE: return "Círculo";
		case EntityType::TEXT: return "Texto";
	}
	return "?";
}

static const char *type_id(EntityType p_type) {
	switch (p_type) {
		case EntityType::RECT: return "rect";
		case EntityType::CIRCLE: return "circle";
		case EntityType::TEXT: return "text";
	}
	return "rect";
}

static EntityType type_from_id(const std::string &p_id) {
	if (p_id == "circle") {
		return EntityType::CIRCLE;
	}
	if (p_id == "text") {
		return EntityType::TEXT;
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
	j["format"] = 1;
	j["name"] = name;
	j["background"] = color_to_hex(background);
	Json list = Json::array();
	for (const Entity &e : entities) {
		Json je = Json::object();
		je["name"] = e.name;
		je["type"] = type_id(e.type);
		je["x"] = (double)e.x;
		je["y"] = (double)e.y;
		je["width"] = (double)e.width;
		je["height"] = (double)e.height;
		je["rotation"] = (double)e.rotation;
		je["color"] = color_to_hex(e.color);
		je["text"] = e.text;
		je["text_size"] = (double)e.text_size;
		je["visible"] = e.visible;
		je["spin_speed"] = (double)e.spin_speed;
		je["bob_amplitude"] = (double)e.bob_amplitude;
		je["bob_frequency"] = (double)e.bob_frequency;
		je["player"] = e.player;
		je["player_speed"] = (double)e.player_speed;
		je["camera_follow"] = e.camera_follow;
		list.push_back(je);
	}
	j["entities"] = list;
	return j;
}

Scene Scene::from_json(const Json &p_json) {
	Scene s;
	s.name = p_json["name"].as_string("Main");
	s.background = color_from_hex(p_json["background"].as_string(), s.background);
	for (const Json &je : p_json["entities"].get_items()) {
		Entity e;
		e.name = je["name"].as_string(e.name);
		e.type = type_from_id(je["type"].as_string());
		e.x = (float)je["x"].as_number(e.x);
		e.y = (float)je["y"].as_number(e.y);
		e.width = (float)je["width"].as_number(e.width);
		e.height = (float)je["height"].as_number(e.height);
		e.rotation = (float)je["rotation"].as_number(e.rotation);
		e.color = color_from_hex(je["color"].as_string(), e.color);
		e.text = je["text"].as_string(e.text);
		e.text_size = (float)je["text_size"].as_number(e.text_size);
		e.visible = je["visible"].as_bool(true);
		e.spin_speed = (float)je["spin_speed"].as_number(0);
		e.bob_amplitude = (float)je["bob_amplitude"].as_number(0);
		e.bob_frequency = (float)je["bob_frequency"].as_number(0.5);
		e.player = je["player"].as_bool(false);
		e.player_speed = (float)je["player_speed"].as_number(300);
		e.camera_follow = je["camera_follow"].as_bool(false);
		s.entities.push_back(e);
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
	return fs::join(path, "main.scene");
}

bool Project::save() const {
	Json j = Json::object();
	j["format"] = 1;
	j["name"] = name;
	j["engine_version"] = engine_version.empty() ? std::string(IPDE_VERSION_NUMBER) : engine_version;
	j["main_scene"] = "main.scene";
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
	return true;
}

bool Project::create(const std::string &p_folder, const std::string &p_name, std::string &r_error) {
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

	// Example scene: a player square, a spinning coin and a title.
	Scene s;
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
	player.camera_follow = false;
	s.entities.push_back(player);

	Entity coin;
	coin.name = "Moneda";
	coin.type = EntityType::RECT;
	coin.x = 150;
	coin.y = 60;
	coin.width = 40;
	coin.height = 40;
	coin.rotation = 45;
	coin.color = 0xFFFFC53D;
	coin.spin_speed = 120;
	coin.bob_amplitude = 12;
	coin.bob_frequency = 0.8f;
	s.entities.push_back(coin);

	Entity title;
	title.name = "Título";
	title.type = EntityType::TEXT;
	title.x = 0;
	title.y = -160;
	title.text = p_name;
	title.text_size = 48;
	title.color = 0xFFFFFFFF;
	s.entities.push_back(title);

	if (!s.save(p.get_scene_file())) {
		r_error = "No se pudo escribir la escena.";
		return false;
	}
	return true;
}

} // namespace ipde
