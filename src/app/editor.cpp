// IPDE scene editor (2D and 3D).

#include "app/app.h"
#include "app/game.h"
#include "app/scene3d.h"
#include "core/audio.h"
#include "core/fs.h"
#include "core/image.h"
#include "platform/platform.h"
#include "render3d/renderer3d.h"
#include "version.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ipde {

struct EditorState {
	Scene scene;
	std::string scene_file; // Level being edited (file name inside the project).
	int selected = -1;
	bool dirty = false;
	// 2D view.
	ImVec2 pan = ImVec2(0, 0); // World position at the center of the viewport.
	float zoom = 1.0f;
	bool dragging = false;
	ImVec2 drag_offset;
	// 3D view (orbit camera).
	float yaw = 35.0f;
	float pitch = 28.0f;
	float distance = 16.0f;
	Vec3 target = Vec3(0, 0.5f, 0);
	bool drag3d = false;
	bool drag3d_vertical = false;
	Vec3 drag3d_offset;
	float drag3d_plane_y = 0.0f;
	float drag3d_start_mouse_y = 0.0f;
	float drag3d_start_y = 0.0f;
	// Move gizmo (axis arrows).
	int gizmo_axis = -1; // Axis being dragged (0 = X, 1 = Y, 2 = Z).
	int gizmo_mode = 0; // 0 = move, 1 = rotate, 2 = scale.
	Vec3 gizmo_start_pos;
	Vec3 gizmo_start_rot;
	Vec3 gizmo_start_size;
	ImVec2 gizmo_start_mouse;
	bool show_grid = true;
	bool preview_animation = false;
	double preview_start = 0.0;

	bool show_close_dialog = false;
	std::string last_export;
	ImVec2 view2d_pos, view2d_size; // 2D viewport rectangle on screen (for thumbnails).

	// Undo / redo: snapshots of the scene (JSON) taken when an edit finishes.
	std::vector<std::string> undo_stack;
	std::vector<std::string> redo_stack;
	std::string committed; // Scene as it was after the last finished edit.
};

static EditorState g_ed;
static Renderer3D g_r3d;
static SceneRenderer3D g_scene_r3d;
static ImTextureID g_view_tex = ImTextureID_Invalid;
static SpriteCache g_sprites; // 2D sprites shown in the viewport.

bool editor_has_unsaved_changes() {
	return g_ed.dirty;
}

// Loads one level into the editor (resets the selection and the undo history).
static void load_level(App &p_app, const std::string &p_file) {
	const EditorState old = g_ed;
	g_ed = EditorState();
	// Keep the view and options when switching levels.
	g_ed.yaw = old.yaw;
	g_ed.pitch = old.pitch;
	g_ed.distance = old.distance;
	g_ed.target = old.target;
	g_ed.zoom = old.zoom;
	g_ed.pan = old.pan;
	g_ed.show_grid = old.show_grid;
	g_ed.scene_file = p_file;
	if (!Scene::load(p_app.project.get_scene_path(p_file), g_ed.scene)) {
		g_ed.scene = Scene(); // New empty scene.
	}
	g_ed.committed = g_ed.scene.to_json().dump(false);
}

void editor_open(App &p_app) {
	g_ed = EditorState();
	meshes::clear_cache();
	g_scene_r3d = SceneRenderer3D();
	g_sprites.clear();
	g_sprites.renderer = &p_app.renderer;
	g_sprites.project_dir = p_app.project.path;
	load_level(p_app, p_app.project.scenes.empty() ? std::string("main.scene") : p_app.project.scenes[0]);
}

// Records a history step when the scene changed and no edit is in progress (mouse released, no widget active).
static void history_track() {
	if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || g_ed.drag3d || g_ed.dragging) {
		return;
	}
	std::string now = g_ed.scene.to_json().dump(false);
	if (now != g_ed.committed) {
		g_ed.undo_stack.push_back(std::move(g_ed.committed));
		if (g_ed.undo_stack.size() > 200) {
			g_ed.undo_stack.erase(g_ed.undo_stack.begin());
		}
		g_ed.redo_stack.clear();
		g_ed.committed = std::move(now);
	}
}

static void history_apply(std::vector<std::string> &r_from, std::vector<std::string> &r_to) {
	history_track(); // Commit pending edits first.
	if (r_from.empty()) {
		return;
	}
	r_to.push_back(g_ed.committed);
	g_ed.committed = r_from.back();
	r_from.pop_back();
	std::string err;
	const Json j = Json::parse(g_ed.committed, &err);
	g_ed.scene = Scene::from_json(j);
	if (g_ed.selected >= (int)g_ed.scene.entities.size()) {
		g_ed.selected = -1;
	}
	g_ed.dirty = true;
}

static void undo() {
	history_apply(g_ed.undo_stack, g_ed.redo_stack);
}

static void redo() {
	history_apply(g_ed.redo_stack, g_ed.undo_stack);
}

// Copies an external file into the project's "assets" folder and returns its path relative to the project.
static std::string import_asset(App &p_app, const std::string &p_title, const std::string &p_filter, const std::string &p_pattern) {
	const std::string src = platform::pick_file(p_title, p_filter, p_pattern);
	if (src.empty()) {
		return std::string();
	}
	const std::string root = p_app.project.path;
	if (src.size() > root.size() + 1 && src.compare(0, root.size(), root) == 0) {
		std::string rel = src.substr(root.size() + 1); // Already inside the project.
		std::replace(rel.begin(), rel.end(), '\\', '/');
		return rel;
	}
	const std::string dir = fs::join(root, "assets");
	fs::make_dirs(dir);
	const std::string name = fs::get_file_name(src);
	if (!fs::copy_file(src, fs::join(dir, name))) {
		p_app.toast("No se pudo copiar el archivo.", true);
		return std::string();
	}
	p_app.toast("Importado: assets/" + name);
	return "assets/" + name;
}

static uint32_t argb_from_float4(const float p_c[4]) {
	auto b = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); };
	return (b(p_c[3]) << 24) | (b(p_c[0]) << 16) | (b(p_c[1]) << 8) | b(p_c[2]);
}

static void float4_from_argb(uint32_t p_argb, float r_c[4]) {
	r_c[0] = ((p_argb >> 16) & 0xFF) / 255.0f;
	r_c[1] = ((p_argb >> 8) & 0xFF) / 255.0f;
	r_c[2] = (p_argb & 0xFF) / 255.0f;
	r_c[3] = ((p_argb >> 24) & 0xFF) / 255.0f;
}

static bool color_edit(const char *p_label, uint32_t *p_argb, bool p_alpha = false) {
	float c[4];
	float4_from_argb(*p_argb, c);
	const bool changed = p_alpha ? ImGui::ColorEdit4(p_label, c) : ImGui::ColorEdit3(p_label, c);
	if (changed) {
		if (!p_alpha) {
			c[3] = 1.0f;
		}
		*p_argb = argb_from_float4(c);
		g_ed.dirty = true;
	}
	return changed;
}

// Saves a 16:9 picture of the viewport as the project's thumbnail (shown in the dashboard).
static void save_thumbnail(App &p_app) {
	if (!p_app.project.scenes.empty() && g_ed.scene_file != p_app.project.scenes[0]) {
		return; // Only the first level represents the project.
	}
	const uint32_t *src = nullptr;
	int sw = 0, sh = 0, stride = 0, x0 = 0, y0 = 0;
	if (g_ed.scene.is_3d) {
		src = g_r3d.get_pixels();
		sw = g_r3d.get_width();
		sh = g_r3d.get_height();
		stride = sw;
	} else {
		src = p_app.renderer.get_pixels();
		stride = p_app.renderer.get_width();
		x0 = std::max(0, (int)g_ed.view2d_pos.x);
		y0 = std::max(0, (int)g_ed.view2d_pos.y);
		sw = std::min((int)g_ed.view2d_size.x, p_app.renderer.get_width() - x0);
		sh = std::min((int)g_ed.view2d_size.y, p_app.renderer.get_height() - y0);
	}
	if (!src || sw < 32 || sh < 32) {
		return;
	}
	// Center crop to 16:9.
	int cw = sw, ch = sw * 9 / 16;
	if (ch > sh) {
		ch = sh;
		cw = sh * 16 / 9;
	}
	x0 += (sw - cw) / 2;
	y0 += (sh - ch) / 2;
	const std::vector<uint32_t> small = image::downscale(src + (size_t)y0 * stride + x0, cw, ch, stride, 320, 180);
	image::write_png(fs::join(p_app.project.path, "thumbnail.png"), small.data(), 320, 180);
}

static bool save(App &p_app) {
	save_thumbnail(p_app);
	if (g_ed.scene.save(p_app.project.get_scene_path(g_ed.scene_file))) {
		g_ed.dirty = false;
		p_app.toast("Escena guardada.");
		return true;
	}
	p_app.toast("No se pudo guardar la escena.", true);
	return false;
}

static void run_project(App &p_app) {
	save(p_app);
	if (!platform::create_process(platform::get_executable_path(), { "--run", p_app.project.path, "--level", g_ed.scene_file })) {
		p_app.toast("No se pudo ejecutar el proyecto.", true);
	}
}

static void export_windows(App &p_app) {
	save(p_app);
	std::string name = p_app.project.name;
	for (char &c : name) {
		if (strchr("\\/:*?\"<>|", c)) {
			c = '_';
		}
	}
	const std::string out_dir = fs::join(fs::join(fs::join(p_app.project.path, "export"), "windows"), name);
	const std::string data_dir = fs::join(out_dir, "data");
	fs::remove_path(out_dir);
	bool ok = fs::make_dirs(data_dir);
#ifdef _WIN32
	const std::string exe_name = name + ".exe";
#else
	const std::string exe_name = name;
#endif
	ok = ok && fs::copy_file(platform::get_executable_path(), fs::join(out_dir, exe_name));
	ok = ok && fs::copy_file(p_app.project.get_file(), fs::join(data_dir, "project.ipde"));
	// Every level and the files they use (textures, models, sounds, music), keeping their relative paths.
	std::vector<std::string> files;
	for (const std::string &level : p_app.project.scenes) {
		Scene sc;
		if (!Scene::load(p_app.project.get_scene_path(level), sc)) {
			ok = false;
			continue;
		}
		files.push_back(level);
		if (!sc.music.empty()) {
			files.push_back(sc.music);
		}
		for (const Entity &e : sc.entities) {
			for (const std::string *f : { &e.texture, &e.model, &e.sound }) {
				if (!f->empty()) {
					files.push_back(*f);
				}
			}
		}
	}
	std::sort(files.begin(), files.end());
	files.erase(std::unique(files.begin(), files.end()), files.end());
	int missing = 0;
	for (const std::string &f : files) {
		const std::string src = fs::join(p_app.project.path, f);
		const std::string dst = fs::join(data_dir, f);
		fs::make_dirs(fs::get_dir(dst));
		if (!fs::copy_file(src, dst)) {
			missing++;
		}
	}
	if (ok && missing > 0) {
		p_app.toast(std::to_string(missing) + " archivo(s) usados por la escena no se encontraron.", true);
	}
	if (ok) {
		g_ed.last_export = out_dir;
		p_app.toast("Juego exportado para Windows en: " + out_dir);
		platform::open_folder(out_dir);
	} else {
		p_app.toast("No se pudo exportar el juego.", true);
	}
}

static Camera3D editor_camera() {
	Camera3D cam;
	const float yaw = g_ed.yaw * DEG2RAD;
	const float pitch = g_ed.pitch * DEG2RAD;
	cam.target = g_ed.target;
	cam.position = g_ed.target + Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * g_ed.distance;
	cam.fov = g_ed.scene.camera_fov;
	return cam;
}

static void add_entity(EntityType p_type) {
	Entity e;
	e.type = p_type;
	e.name = entity_type_name(p_type);
	if (entity_type_is_3d(p_type)) {
		e.x = g_ed.target.x;
		e.z = g_ed.target.z;
		e.width = e.height = e.depth = 1.0f;
		e.y = 0.5f;
		e.player_speed = 5.0f;
		switch (p_type) {
			case EntityType::PLANE:
				e.width = e.depth = 6.0f;
				e.y = 0.0f;
				e.color = 0xFF5A6478;
				break;
			case EntityType::SPHERE:
				e.color = 0xFF4DD6A0;
				break;
			case EntityType::CYLINDER:
				e.color = 0xFF50B4FF;
				break;
			case EntityType::CONE:
				e.color = 0xFFFF7896;
				break;
			case EntityType::TORUS:
				e.color = 0xFFFFC53D;
				e.rot_x = 90;
				break;
			case EntityType::CAPSULE:
				e.height = 2.0f;
				e.y = 1.0f;
				break;
			case EntityType::LIGHT:
				e.y = 2.5f;
				e.color = 0xFFFFE0B0;
				e.solid = false;
				break;
			case EntityType::MODEL:
				e.color = 0xFFDDDDDD;
				break;
			default:
				break;
		}
	} else {
		e.x = g_ed.pan.x;
		e.y = g_ed.pan.y;
		if (p_type == EntityType::CIRCLE) {
			e.color = 0xFF4DD6A0;
		} else if (p_type == EntityType::TEXT) {
			e.color = 0xFFFFFFFF;
			e.solid = false;
		}
	}
	g_ed.scene.entities.push_back(e);
	g_ed.selected = (int)g_ed.scene.entities.size() - 1;
	g_ed.dirty = true;
}

static void delete_selected() {
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		g_ed.scene.entities.erase(g_ed.scene.entities.begin() + g_ed.selected);
		g_ed.selected = std::min(g_ed.selected, (int)g_ed.scene.entities.size() - 1);
		g_ed.dirty = true;
	}
}

static std::vector<Entity> g_clipboard;

static void copy_selected() {
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		g_clipboard = { g_ed.scene.entities[g_ed.selected] };
	}
}

static void paste_clipboard() {
	for (Entity copy : g_clipboard) {
		if (entity_type_is_3d(copy.type) != g_ed.scene.is_3d && copy.type != EntityType::TEXT) {
			continue; // 3D objects can't go into 2D scenes and vice versa.
		}
		if (entity_type_is_3d(copy.type)) {
			copy.x += 1.0f;
			copy.z += 1.0f;
		} else {
			copy.x += 24;
			copy.y += 24;
		}
		g_clipboard = { copy }; // Pasting again keeps offsetting.
		g_ed.scene.entities.push_back(copy);
		g_ed.selected = (int)g_ed.scene.entities.size() - 1;
		g_ed.dirty = true;
	}
}

static void duplicate_selected() {
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		Entity copy = g_ed.scene.entities[g_ed.selected];
		copy.name += " (copia)";
		if (entity_type_is_3d(copy.type)) {
			copy.x += 1.0f;
			copy.z += 1.0f;
		} else {
			copy.x += 24;
			copy.y += 24;
		}
		g_ed.scene.entities.push_back(copy);
		g_ed.selected = (int)g_ed.scene.entities.size() - 1;
		g_ed.dirty = true;
	}
}

static void focus_selected() {
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		const Entity &e = g_ed.scene.entities[g_ed.selected];
		if (g_ed.scene.is_3d) {
			g_ed.target = Vec3(e.x, e.y, e.z);
			g_ed.distance = std::max(3.0f, std::max(e.width, std::max(e.height, e.depth)) * 3.0f);
		} else {
			g_ed.pan = ImVec2(e.x, e.y);
		}
	}
}

static std::string level_label(const Project &p_project, size_t p_index) {
	std::string f = p_project.scenes[p_index];
	if (f.size() > 6 && f.compare(f.size() - 6, 6, ".scene") == 0) {
		f.resize(f.size() - 6);
	}
	return std::to_string(p_index + 1) + ". " + f;
}

// Level selector: switch, add, reorder and delete levels.
static void draw_levels(App &p_app) {
	const float s = p_app.scale();
	Project &project = p_app.project;
	ImGui::TextUnformatted("NIVELES");
	ImGui::Separator();
	size_t current = 0;
	for (size_t i = 0; i < project.scenes.size(); i++) {
		if (project.scenes[i] == g_ed.scene_file) {
			current = i;
		}
	}
	ImGui::SetNextItemWidth(-76 * s);
	if (ImGui::BeginCombo("##level", level_label(project, current).c_str())) {
		for (size_t i = 0; i < project.scenes.size(); i++) {
			if (ImGui::Selectable(level_label(project, i).c_str(), i == current) && i != current) {
				if (g_ed.dirty) {
					save(p_app); // Switching levels keeps the changes.
				}
				load_level(p_app, project.scenes[i]);
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Los niveles se juegan en este orden: al tocar la Meta se pasa al siguiente.");
	}
	ImGui::SameLine();
	if (ImGui::Button("+", ImVec2(30 * s, 0))) {
		if (g_ed.dirty) {
			save(p_app);
		}
		std::string file;
		if (project.add_scene(g_ed.scene.is_3d, file)) {
			load_level(p_app, file);
			p_app.toast("Nivel creado: " + file);
		} else {
			p_app.toast("No se pudo crear el nivel.", true);
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Nuevo nivel");
	}
	ImGui::SameLine();
	if (ImGui::Button("...", ImVec2(-1, 0))) {
		ImGui::OpenPopup("LevelMenu");
	}
	if (ImGui::BeginPopup("LevelMenu")) {
		if (ImGui::MenuItem("Mover antes", nullptr, false, current > 0)) {
			std::swap(project.scenes[current], project.scenes[current - 1]);
			project.save();
		}
		if (ImGui::MenuItem("Mover después", nullptr, false, current + 1 < project.scenes.size())) {
			std::swap(project.scenes[current], project.scenes[current + 1]);
			project.save();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quitar nivel del juego", nullptr, false, project.scenes.size() > 1)) {
			// The file stays in the project folder (nothing is deleted from disk).
			project.scenes.erase(project.scenes.begin() + (long)current);
			project.save();
			g_ed.dirty = false;
			load_level(p_app, project.scenes[std::min(current, project.scenes.size() - 1)]);
			p_app.toast("Nivel quitado (el archivo sigue en la carpeta del proyecto).");
		}
		ImGui::EndPopup();
	}
	ImGui::Spacing();
}

static void draw_scene_panel(App &p_app, float p_width) {
	const float s = p_app.scale();
	ImGui::BeginChild("ScenePanel", ImVec2(p_width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	draw_levels(p_app);
	ImGui::TextUnformatted(g_ed.scene.is_3d ? "ESCENA 3D" : "ESCENA 2D");
	ImGui::Separator();
	if (ui::accent_button("+  Añadir", ImVec2(-1, 0))) {
		ImGui::OpenPopup("AddMenu");
	}
	if (ImGui::BeginPopup("AddMenu")) {
		if (g_ed.scene.is_3d) {
			const EntityType types[] = { EntityType::CUBE, EntityType::SPHERE, EntityType::PLANE, EntityType::CYLINDER, EntityType::CONE, EntityType::TORUS, EntityType::CAPSULE, EntityType::MODEL, EntityType::LIGHT };
			for (EntityType t : types) {
				if (t == EntityType::MODEL || t == EntityType::LIGHT) {
					ImGui::Separator();
				}
				if (ImGui::MenuItem(t == EntityType::MODEL ? "Modelo (.obj)" : (t == EntityType::LIGHT ? "Luz puntual" : entity_type_name(t)))) {
					add_entity(t);
				}
			}
		} else {
			const EntityType types[] = { EntityType::RECT, EntityType::CIRCLE, EntityType::TEXT };
			for (EntityType t : types) {
				if (ImGui::MenuItem(entity_type_name(t))) {
					add_entity(t);
				}
			}
		}
		ImGui::EndPopup();
	}
	ImGui::Spacing();
	ImGui::BeginChild("EntityList", ImVec2(0, -48 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	for (int i = 0; i < (int)g_ed.scene.entities.size(); i++) {
		const Entity &e = g_ed.scene.entities[i];
		ImGui::PushID(i);
		std::string prefix;
		if (e.player) {
			prefix = "[J] ";
		} else if (e.collectible) {
			prefix = "[+] ";
		} else if (e.goal) {
			prefix = "[M] ";
		} else if (e.chase_speed != 0.0f) {
			prefix = "[E] ";
		} else if (e.hazard) {
			prefix = "[!] ";
		}
		const std::string label = prefix + e.name + "##e";
		if (!e.visible) {
			ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
		}
		if (ImGui::Selectable(label.c_str(), g_ed.selected == i)) {
			g_ed.selected = i;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
			g_ed.selected = i;
			focus_selected();
		}
		if (!e.visible) {
			ImGui::PopStyleColor();
		}
		const char *type_name = entity_type_name(e.type);
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(type_name).x);
		ImGui::TextDisabled("%s", type_name);
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::Separator();
	ImGui::BeginDisabled(g_ed.selected < 0);
	if (ImGui::Button("Duplicar", ImVec2((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 0))) {
		duplicate_selected();
	}
	ImGui::SameLine();
	if (ImGui::Button("Eliminar", ImVec2(-1, 0))) {
		delete_selected();
	}
	ImGui::EndDisabled();
	ImGui::EndChild();
}

static void draw_viewport_3d(App &p_app, float p_width) {
	const float s = p_app.scale();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("Viewport3D", ImVec2(p_width, 0), ImGuiChildFlags_Borders);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size = ImGui::GetContentRegionAvail();
	const int w = std::max(16, (int)size.x);
	const int h = std::max(16, (int)size.y);
	ImGui::InvisibleButton("canvas3d", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	ImGuiIO &io = ImGui::GetIO();
	const float mx = io.MousePos.x - pos.x;
	const float my = io.MousePos.y - pos.y;

	// Camera controls: right drag orbits, middle drag (or Shift + right) pans, wheel zooms.
	if (hovered && io.MouseWheel != 0.0f) {
		g_ed.distance = std::clamp(g_ed.distance * (io.MouseWheel > 0 ? 0.88f : 1.0f / 0.88f), 0.5f, 400.0f);
	}
	const bool panning = ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Right)));
	if (panning) {
		const Camera3D cam = editor_camera();
		const Vec3 fwd = (cam.target - cam.position).normalized();
		const Vec3 right = cross(fwd, Vec3(0, 1, 0)).normalized();
		const Vec3 up = cross(right, fwd);
		const float k = g_ed.distance * 0.0016f;
		g_ed.target = g_ed.target - right * (io.MouseDelta.x * k) + up * (io.MouseDelta.y * k);
	} else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
		g_ed.yaw -= io.MouseDelta.x * 0.35f;
		g_ed.pitch = std::clamp(g_ed.pitch + io.MouseDelta.y * 0.35f, -89.0f, 89.0f);
	}

	// Render the scene.
	g_r3d.resize(w, h);
	const Camera3D cam = editor_camera();
	const double t = g_ed.preview_animation ? platform::get_time() - g_ed.preview_start : 0.0;
	g_r3d.begin(cam, SceneRenderer3D::make_environment(g_ed.scene), SceneRenderer3D::make_lights(g_ed.scene, t, g_ed.preview_animation));
	g_scene_r3d.draw(g_r3d, g_ed.scene, p_app.project.path, t, g_ed.preview_animation, true);
	if (g_ed.show_grid) {
		for (int i = -20; i <= 20; i++) {
			const uint32_t c = i == 0 ? 0x00000000 : (i % 5 == 0 ? 0x40FFFFFF : 0x22FFFFFF);
			if (c) {
				g_r3d.draw_line(Vec3((float)i, 0.002f, -20), Vec3((float)i, 0.002f, 20), c);
				g_r3d.draw_line(Vec3(-20, 0.002f, (float)i), Vec3(20, 0.002f, (float)i), c);
			}
		}
		g_r3d.draw_line(Vec3(-20, 0.003f, 0), Vec3(20, 0.003f, 0), 0xC0FF5A5A);
		g_r3d.draw_line(Vec3(0, 0.003f, -20), Vec3(0, 0.003f, 20), 0xC05A8CFF);
		g_r3d.draw_line(Vec3(0, 0, 0), Vec3(0, 2, 0), 0xC05AFF8C);
	}
	// Selection: bounding box and axes.
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		const Entity &e = g_ed.scene.entities[g_ed.selected];
		if (entity_type_is_3d(e.type)) {
			const Mat4 m = SceneRenderer3D::entity_transform(e, t, g_ed.preview_animation);
			auto mesh = g_scene_r3d.get_mesh(e, p_app.project.path);
			const Vec3 mn = mesh->aabb_min, mxv = mesh->aabb_max;
			Vec3 c[8];
			for (int i = 0; i < 8; i++) {
				c[i] = m.transform_point(Vec3(i & 1 ? mxv.x : mn.x, i & 2 ? mxv.y : mn.y, i & 4 ? mxv.z : mn.z));
			}
			const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
			for (const auto &ed : edges) {
				g_r3d.draw_line(c[ed[0]], c[ed[1]], 0xFF8C9BFF, false);
			}
			const Vec3 p(e.x, e.y, e.z);
			if (e.has_motion()) { // Path of a moving platform and its end position.
				const Vec3 end = p + Vec3(e.move_x, e.move_y, e.move_z);
				g_r3d.draw_line(p, end, 0xFFFFC53D, false);
				const Mat4 me = Mat4::translate(Vec3(e.move_x, e.move_y, e.move_z)) * SceneRenderer3D::entity_transform(e, 0.0, false);
				Vec3 ce[8];
				for (int i = 0; i < 8; i++) {
					ce[i] = me.transform_point(Vec3(i & 1 ? mxv.x : mn.x, i & 2 ? mxv.y : mn.y, i & 4 ? mxv.z : mn.z));
				}
				for (const auto &ed : edges) {
					g_r3d.draw_line(ce[ed[0]], ce[ed[1]], 0x90FFC53D, true);
				}
			}
			if (e.chase_speed != 0.0f) { // Enemy vision range.
				for (int i = 0; i < 48; i++) {
					const float a0 = i / 48.0f * 2 * PI, a1 = (i + 1) / 48.0f * 2 * PI;
					g_r3d.draw_line(p + Vec3(std::cos(a0), 0, std::sin(a0)) * e.chase_range, p + Vec3(std::cos(a1), 0, std::sin(a1)) * e.chase_range, 0x80FF4D5E);
				}
			}
			if (e.type == EntityType::LIGHT) {
				for (int i = 0; i < 48; i++) {
					const float a0 = i / 48.0f * 2 * PI, a1 = (i + 1) / 48.0f * 2 * PI;
					g_r3d.draw_line(p + Vec3(std::cos(a0), 0, std::sin(a0)) * e.light_range, p + Vec3(std::cos(a1), 0, std::sin(a1)) * e.light_range, 0x80FFE0B0);
				}
			}
		}
	}
	g_r3d.end();

	// Gizmo geometry (screen space) for the selected 3D entity.
	bool gizmo_visible = false;
	ImVec2 gizmo_origin, gizmo_end[3];
	int gizmo_hover = -1;
	float gizmo_len = 1.0f;
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size() && entity_type_is_3d(g_ed.scene.entities[g_ed.selected].type)) {
		const Entity &e = g_ed.scene.entities[g_ed.selected];
		if (e.type == EntityType::LIGHT) {
			g_ed.gizmo_mode = 0; // Lights can only be moved.
		}
		const Vec3 p(e.x, e.y, e.z);
		// Constant size on screen: proportional to the distance to the camera.
		gizmo_len = std::max(0.05f, (p - cam.position).length() * 0.16f * std::tan(cam.fov * DEG2RAD * 0.5f) / std::tan(30.0f * DEG2RAD));
		float ox, oy;
		gizmo_visible = g_r3d.world_to_screen(p, ox, oy);
		gizmo_origin = ImVec2(ox, oy);
		const Vec3 axes[3] = { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) };
		float best = 8.0f * s;
		for (int k = 0; k < 3 && gizmo_visible; k++) {
			float ex, ey;
			gizmo_visible = g_r3d.world_to_screen(p + axes[k] * gizmo_len, ex, ey);
			gizmo_end[k] = ImVec2(ex, ey);
			// Distance from the mouse to the arrow.
			const float vx = ex - ox, vy = ey - oy;
			const float len2 = vx * vx + vy * vy;
			if (len2 < 1.0f) {
				continue;
			}
			const float tt = std::clamp(((mx - ox) * vx + (my - oy) * vy) / len2, 0.0f, 1.0f);
			const float dx = mx - (ox + vx * tt), dy = my - (oy + vy * tt);
			const float dist = std::sqrt(dx * dx + dy * dy);
			if (tt > 0.15f && dist < best) {
				best = dist;
				gizmo_hover = k;
			}
		}
		if (!gizmo_visible || !hovered) {
			gizmo_hover = -1;
		}
	}
	if (g_ed.gizmo_axis >= 0) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && gizmo_visible) {
			Entity &e = g_ed.scene.entities[g_ed.selected];
			const int k = g_ed.gizmo_axis;
			const float vx = gizmo_end[k].x - gizmo_origin.x, vy = gizmo_end[k].y - gizmo_origin.y;
			const float len2 = std::max(1.0f, vx * vx + vy * vy);
			const float along = ((mx - g_ed.gizmo_start_mouse.x) * vx + (my - g_ed.gizmo_start_mouse.y) * vy) / len2; // In arrow lengths.
			auto comp = [](Vec3 &r_v, int p_k) -> float & { return p_k == 0 ? r_v.x : (p_k == 1 ? r_v.y : r_v.z); };
			if (g_ed.gizmo_mode == 1) { // Rotate around the axis.
				Vec3 r = g_ed.gizmo_start_rot;
				float v = comp(r, k) + along * 90.0f;
				if (io.KeyCtrl) {
					v = std::round(v / 15.0f) * 15.0f;
				}
				float &target = k == 0 ? e.rot_x : (k == 1 ? e.rot_y : e.rotation);
				if (target != v) {
					target = v;
					g_ed.dirty = true;
				}
			} else if (g_ed.gizmo_mode == 2) { // Scale along the axis (spheres scale uniformly).
				Vec3 sz = g_ed.gizmo_start_size;
				const int axis = e.type == EntityType::SPHERE ? 0 : k;
				float v = std::max(0.05f, comp(sz, axis) * (1.0f + along));
				if (io.KeyCtrl) {
					v = std::max(0.25f, std::round(v * 4.0f) / 4.0f);
				}
				float &target = axis == 0 ? e.width : (axis == 1 ? e.height : e.depth);
				if (target != v) {
					target = v;
					g_ed.dirty = true;
				}
			} else {
				Vec3 sp = g_ed.gizmo_start_pos;
				float v = comp(sp, k) + along * gizmo_len;
				if (io.KeyCtrl) {
					v = std::round(v * 2.0f) / 2.0f;
				}
				float &target = k == 0 ? e.x : (k == 1 ? e.y : e.z);
				if (target != v) {
					target = v;
					g_ed.dirty = true;
				}
			}
		} else {
			g_ed.gizmo_axis = -1;
		}
	}

	// Selection and dragging (after rendering so the ID buffer is up to date).
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gizmo_hover >= 0) {
		const Entity &e = g_ed.scene.entities[g_ed.selected];
		g_ed.gizmo_axis = gizmo_hover;
		g_ed.gizmo_start_pos = Vec3(e.x, e.y, e.z);
		g_ed.gizmo_start_rot = Vec3(e.rot_x, e.rot_y, e.rotation);
		g_ed.gizmo_start_size = Vec3(e.width, e.height, e.depth);
		g_ed.gizmo_start_mouse = ImVec2(mx, my);
	} else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const uint32_t id = g_r3d.pick((int)mx, (int)my);
		const int prev = g_ed.selected;
		g_ed.selected = id ? (int)id - 1 : -1;
		g_ed.drag3d = false;
		if (g_ed.selected >= 0 && g_ed.selected == prev) {
			// Drag the already selected entity: on the ground plane, or vertically with Shift.
			const Entity &e = g_ed.scene.entities[g_ed.selected];
			g_ed.drag3d = true;
			g_ed.drag3d_vertical = io.KeyShift;
			g_ed.drag3d_plane_y = e.y;
			g_ed.drag3d_start_mouse_y = my;
			g_ed.drag3d_start_y = e.y;
			Vec3 o, d;
			g_r3d.screen_ray(mx, my, o, d);
			if (std::fabs(d.y) > 1e-4f) {
				const float tt = (g_ed.drag3d_plane_y - o.y) / d.y;
				const Vec3 hit = o + d * tt;
				g_ed.drag3d_offset = Vec3(e.x, e.y, e.z) - hit;
			}
		}
	}
	if (g_ed.drag3d) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && g_ed.selected >= 0) {
			Entity &e = g_ed.scene.entities[g_ed.selected];
			if (g_ed.drag3d_vertical) {
				const float ny = g_ed.drag3d_start_y - (my - g_ed.drag3d_start_mouse_y) * g_ed.distance * 0.0025f;
				if (ny != e.y) {
					e.y = io.KeyCtrl ? std::round(ny * 4.0f) / 4.0f : ny;
					g_ed.dirty = true;
				}
			} else {
				Vec3 o, d;
				g_r3d.screen_ray(mx, my, o, d);
				if (std::fabs(d.y) > 1e-4f) {
					const float tt = (g_ed.drag3d_plane_y - o.y) / d.y;
					if (tt > 0) {
						const Vec3 hit = o + d * tt + g_ed.drag3d_offset;
						float nx = hit.x, nz = hit.z;
						if (io.KeyCtrl) {
							nx = std::round(nx * 2.0f) / 2.0f;
							nz = std::round(nz * 2.0f) / 2.0f;
						}
						if (nx != e.x || nz != e.z) {
							e.x = nx;
							e.z = nz;
							g_ed.dirty = true;
						}
					}
				}
			}
		} else {
			g_ed.drag3d = false;
		}
	}

	g_view_tex = p_app.renderer.update_texture(g_view_tex, g_r3d.get_pixels(), g_r3d.get_width(), g_r3d.get_height());
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddImage(ImTextureRef(g_view_tex), pos, ImVec2(pos.x + w, pos.y + h));
	if (gizmo_visible) {
		const ImU32 colors[3] = { IM_COL32(255, 90, 90, 255), IM_COL32(90, 230, 130, 255), IM_COL32(90, 140, 255, 255) };
		const int active = g_ed.gizmo_axis >= 0 ? g_ed.gizmo_axis : gizmo_hover;
		const ImVec2 o(pos.x + gizmo_origin.x, pos.y + gizmo_origin.y);
		for (int k = 0; k < 3; k++) {
			const ImVec2 e(pos.x + gizmo_end[k].x, pos.y + gizmo_end[k].y);
			const ImU32 c = k == active ? IM_COL32(255, 220, 90, 255) : colors[k];
			const float th = (k == active ? 4.0f : 3.0f) * s;
			float dx = e.x - o.x, dy = e.y - o.y;
			const float len = std::sqrt(dx * dx + dy * dy);
			if (len < 2.0f) {
				continue;
			}
			dx /= len;
			dy /= len;
			const float head = 12.0f * s;
			const ImVec2 base(e.x - dx * head, e.y - dy * head);
			if (g_ed.gizmo_mode == 0) {
				dl->AddLine(o, base, c, th);
				dl->AddTriangleFilled(e, ImVec2(base.x - dy * head * 0.45f, base.y + dx * head * 0.45f), ImVec2(base.x + dy * head * 0.45f, base.y - dx * head * 0.45f), c);
			} else if (g_ed.gizmo_mode == 1) {
				dl->AddLine(o, e, c, th);
				dl->AddCircle(e, head * 0.55f, c, 16, th);
			} else {
				dl->AddLine(o, e, c, th);
				const float q = head * 0.42f;
				dl->AddRectFilled(ImVec2(e.x - q, e.y - q), ImVec2(e.x + q, e.y + q), c);
			}
		}
		dl->AddCircleFilled(o, 4.5f * s, IM_COL32(255, 255, 255, 230));
	}

	// Overlay: help and view options.
	char info[200];
	snprintf(info, sizeof(info), "%zu triángulos   ·   Clic: seleccionar   ·   W/E/R: mover, rotar, escalar · Ctrl: ajustar   ·   Botón dcho: girar   ·   Rueda: zoom   ·   F: enfocar", g_r3d.get_triangle_count());
	dl->AddText(ImVec2(pos.x + 11 * s, pos.y + size.y - 27 * s), IM_COL32(0, 0, 0, 120), info);
	dl->AddText(ImVec2(pos.x + 10 * s, pos.y + size.y - 28 * s), IM_COL32(255, 255, 255, 200), info);
	ImGui::SetCursorScreenPos(ImVec2(pos.x + 10 * s, pos.y + 10 * s));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(16, 17, 31, 190));
	if (ImGui::Checkbox("Cuadrícula", &g_ed.show_grid)) {
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Animar", &g_ed.preview_animation)) {
		g_ed.preview_start = platform::get_time();
	}
	{
		const char *modes[3] = { "Mover (W)", "Rotar (E)", "Escalar (R)" };
		for (int m = 0; m < 3; m++) {
			ImGui::SameLine(0, m == 0 ? 24 * s : 4 * s);
			const bool active = g_ed.gizmo_mode == m;
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, theme::ACCENT);
			}
			if (ImGui::Button(modes[m])) {
				g_ed.gizmo_mode = m;
			}
			if (active) {
				ImGui::PopStyleColor();
			}
		}
	}
	ImGui::PopStyleColor();
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

static bool drag_float(const char *p_label, float *p_value, float p_speed, float p_min = 0, float p_max = 0, const char *p_fmt = "%.1f") {
	const bool changed = ImGui::DragFloat(p_label, p_value, p_speed, p_min, p_max, p_fmt);
	if (changed) {
		g_ed.dirty = true;
	}
	return changed;
}

static bool checkbox(const char *p_label, bool *p_value) {
	const bool changed = ImGui::Checkbox(p_label, p_value);
	if (changed) {
		g_ed.dirty = true;
	}
	return changed;
}

static bool text_field(const char *p_label, std::string &r_value) {
	char buf[512];
	strncpy(buf, r_value.c_str(), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	if (ImGui::InputText(p_label, buf, sizeof(buf))) {
		r_value = buf;
		g_ed.dirty = true;
		return true;
	}
	return false;
}

// Plays a WAV from the project (preview in the editor).
static void preview_sound(App &p_app, const std::string &p_relative) {
	audio::init();
	audio::stop_all();
	audio::Sound s = audio::load_wav(fs::join(p_app.project.path, p_relative));
	if (!s) {
		p_app.toast("No se pudo leer el WAV (usa PCM 8/16/24 bits o float 32).", true);
		return;
	}
	audio::play(s);
}

// A WAV path field with "Importar", "Escuchar" and "Quitar" buttons.
static void sound_field(App &p_app, const char *p_label, std::string &r_path) {
	ImGui::PushID(p_label);
	text_field(p_label, r_path);
	if (ImGui::Button("Importar WAV...")) {
		const std::string rel = import_asset(p_app, "Importar sonido", "Sonidos WAV", "*.wav");
		if (!rel.empty()) {
			r_path = rel;
			g_ed.dirty = true;
		}
	}
	if (!r_path.empty()) {
		ImGui::SameLine();
		if (ImGui::Button("Escuchar")) {
			preview_sound(p_app, r_path);
		}
		ImGui::SameLine();
		if (ImGui::Button("Quitar")) {
			r_path.clear();
			g_ed.dirty = true;
		}
	}
	ImGui::PopID();
}

static void draw_audio_settings(App &p_app) {
	Scene &sc = g_ed.scene;
	ImGui::SeparatorText("Audio");
	checkbox("Efectos de sonido", &sc.sound_effects);
	ui::dim_text("Monedas, saltos, trampolines, caídas y meta.");
	sound_field(p_app, "Música (WAV)", sc.music);
	if (!sc.music.empty()) {
		ImGui::SliderFloat("Volumen música", &sc.music_volume, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemEdited()) {
			g_ed.dirty = true;
		}
	}
}

static void draw_environment(App &p_app) {
	Scene &sc = g_ed.scene;
	if (!sc.is_3d) {
		ImGui::SeparatorText("Escena");
		color_edit("Fondo", &sc.background);
		checkbox("Modo plataformas", &sc.platformer);
		ui::dim_text(sc.platformer ? "Vista lateral: el jugador cae por la gravedad y salta con Espacio, W o la flecha arriba." : "Vista cenital: el jugador se mueve libremente con WASD.");
		if (sc.platformer) {
			drag_float("Gravedad", &sc.gravity_2d, 10.0f, 0, 20000, "%.0f px/s²");
		}
		draw_audio_settings(p_app);
		ImGui::Spacing();
		ui::dim_text("Selecciona una entidad para editarla.");
		return;
	}
	Environment &env = sc.environment;
	ImGui::SeparatorText("Cielo");
	color_edit("Cielo", &env.sky_top);
	color_edit("Horizonte", &env.sky_horizon);
	color_edit("Suelo lejano", &env.ground);
	drag_float("Ambiente", &env.ambient, 0.01f, 0, 3, "%.2f");
	ImGui::SeparatorText("Sol");
	drag_float("Altura", &env.sun_pitch, 0.5f, 1, 90, "%.0f°");
	drag_float("Dirección", &env.sun_yaw, 0.5f, -360, 360, "%.0f°");
	color_edit("Color del sol", &env.sun_color);
	drag_float("Intensidad", &env.sun_energy, 0.01f, 0, 10, "%.2f");
	checkbox("Sombras", &env.shadows);
	ImGui::SeparatorText("Niebla");
	checkbox("Activar niebla", &env.fog);
	if (env.fog) {
		drag_float("Inicio", &env.fog_start, 0.5f, 0, 1000, "%.0f m");
		drag_float("Fin", &env.fog_end, 0.5f, 1, 2000, "%.0f m");
	}
	ImGui::SeparatorText("Cámara del juego");
	drag_float("Campo de visión", &sc.camera_fov, 0.2f, 20, 120, "%.0f°");
	ui::dim_text("Se usa si ninguna entidad tiene \"La cámara lo sigue\".");
	if (ImGui::Button("Usar la vista actual", ImVec2(-1, 0))) {
		const Camera3D cam = editor_camera();
		sc.camera_x = cam.position.x;
		sc.camera_y = cam.position.y;
		sc.camera_z = cam.position.z;
		sc.camera_target_x = cam.target.x;
		sc.camera_target_y = cam.target.y;
		sc.camera_target_z = cam.target.z;
		g_ed.dirty = true;
		p_app.toast("Cámara del juego actualizada.");
	}
	draw_audio_settings(p_app);
}

static void draw_inspector(App &p_app) {
	ImGui::BeginChild("Inspector", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PushItemWidth(-100 * p_app.scale());
	if (g_ed.selected < 0 || g_ed.selected >= (int)g_ed.scene.entities.size()) {
		ImGui::TextUnformatted(g_ed.scene.is_3d ? "ENTORNO" : "INSPECTOR");
		ImGui::Separator();
		draw_environment(p_app);
		ImGui::PopItemWidth();
		ImGui::EndChild();
		return;
	}
	ImGui::TextUnformatted("INSPECTOR");
	ImGui::Separator();
	Entity &e = g_ed.scene.entities[g_ed.selected];
	const bool is3d = entity_type_is_3d(e.type);
	text_field("Nombre", e.name);
	ImGui::TextDisabled("Tipo: %s", entity_type_name(e.type));
	checkbox("Visible", &e.visible);

	ImGui::SeparatorText("Transformación");
	if (is3d) {
		float p[3] = { e.x, e.y, e.z };
		if (ImGui::DragFloat3("Posición", p, 0.05f, 0, 0, "%.2f")) {
			e.x = p[0];
			e.y = p[1];
			e.z = p[2];
			g_ed.dirty = true;
		}
		if (e.type != EntityType::LIGHT) {
			float r[3] = { e.rot_x, e.rot_y, e.rotation };
			if (ImGui::DragFloat3("Rotación", r, 0.5f, 0, 0, "%.0f°")) {
				e.rot_x = r[0];
				e.rot_y = r[1];
				e.rotation = r[2];
				g_ed.dirty = true;
			}
			if (e.type == EntityType::SPHERE) {
				drag_float("Diámetro", &e.width, 0.02f, 0.01f, 1000, "%.2f");
			} else {
				float sz[3] = { e.width, e.height, e.depth };
				if (ImGui::DragFloat3("Tamaño", sz, 0.02f, 0.01f, 1000, "%.2f")) {
					e.width = std::max(0.01f, sz[0]);
					e.height = std::max(0.01f, sz[1]);
					e.depth = std::max(0.01f, sz[2]);
					g_ed.dirty = true;
				}
			}
		}
	} else {
		float posv[2] = { e.x, e.y };
		if (ImGui::DragFloat2("Posición", posv, 1.0f, 0, 0, "%.0f")) {
			e.x = posv[0];
			e.y = posv[1];
			g_ed.dirty = true;
		}
		if (e.type == EntityType::RECT) {
			float sz[2] = { e.width, e.height };
			if (ImGui::DragFloat2("Tamaño", sz, 1.0f, 1.0f, 10000.0f, "%.0f")) {
				e.width = std::max(1.0f, sz[0]);
				e.height = std::max(1.0f, sz[1]);
				g_ed.dirty = true;
			}
		} else if (e.type == EntityType::CIRCLE) {
			drag_float("Diámetro", &e.width, 1.0f, 1.0f, 10000.0f, "%.0f");
		} else {
			text_field("Texto", e.text);
			drag_float("Tamaño", &e.text_size, 0.5f, 4.0f, 400.0f, "%.0f");
		}
		if (e.type != EntityType::TEXT) {
			drag_float("Rotación", &e.rotation, 0.5f, 0, 0, "%.0f°");
		}
	}

	ImGui::SeparatorText(e.type == EntityType::LIGHT ? "Luz" : "Material");
	color_edit("Color", &e.color, !is3d);
	if (e.type == EntityType::LIGHT) {
		drag_float("Energía", &e.light_energy, 0.02f, 0, 50, "%.2f");
		drag_float("Alcance", &e.light_range, 0.05f, 0.1f, 200, "%.1f m");
	} else if (e.type == EntityType::RECT) {
		text_field("Imagen (PNG)", e.texture);
		if (ImGui::Button("Importar imagen...")) {
			const std::string rel = import_asset(p_app, "Importar imagen", "Imágenes PNG", "*.png");
			if (!rel.empty()) {
				e.texture = rel;
				e.color = 0xFFFFFFFF; // Show the image with its own colors.
				g_ed.dirty = true;
			}
		}
		if (!e.texture.empty()) {
			ImGui::SameLine();
			if (ImGui::Button("Quitar")) {
				e.texture.clear();
				g_ed.dirty = true;
			}
			checkbox("Voltear horizontalmente", &e.flip_x);
			if (g_sprites.get(e.texture) == ImTextureID_Invalid) {
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme::DANGER), "No se pudo cargar la imagen.");
			}
			ui::dim_text("El color tiñe la imagen (blanco = colores originales).");
		}
	} else if (is3d) {
		ImGui::SliderFloat("Rugosidad", &e.roughness, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemEdited()) {
			g_ed.dirty = true;
		}
		ImGui::SliderFloat("Metálico", &e.metallic, 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemEdited()) {
			g_ed.dirty = true;
		}
		drag_float("Brillo propio", &e.emission, 0.02f, 0, 20, "%.2f");
		{
			const char *patterns[] = { "Ninguno", "Cuadros", "Ladrillos", "Rayas", "Baldosas", "Ruido" };
			if (ImGui::Combo("Patrón", &e.pattern, patterns, IM_ARRAYSIZE(patterns))) {
				g_ed.dirty = true;
			}
			if (e.pattern != 0) {
				drag_float("Tamaño patrón", &e.pattern_scale, 0.01f, 0.05f, 100, "%.2f m");
			}
		}
		text_field("Textura (PNG)", e.texture);
		if (ImGui::Button("Importar textura...")) {
			const std::string rel = import_asset(p_app, "Importar textura", "Imágenes PNG", "*.png");
			if (!rel.empty()) {
				e.texture = rel;
				g_ed.dirty = true;
			}
		}
		if (!e.texture.empty()) {
			ImGui::SameLine();
			if (ImGui::Button("Quitar")) {
				e.texture.clear();
				g_ed.dirty = true;
			}
		}
		if (!e.texture.empty()) {
			drag_float("Escala UV", &e.uv_scale, 0.02f, 0.01f, 100, "%.2f");
		}
		if (e.type == EntityType::MODEL) {
			text_field("Modelo (.obj)", e.model);
			if (ImGui::Button("Importar modelo...")) {
				const std::string rel = import_asset(p_app, "Importar modelo", "Modelos OBJ", "*.obj");
				if (!rel.empty()) {
					e.model = rel;
					g_ed.dirty = true;
				}
			}
			std::string err;
			if (!e.model.empty() && !meshes::load_obj(fs::join(p_app.project.path, e.model), &err)) {
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme::DANGER), "No se pudo cargar el modelo.");
			}
			ui::dim_text("Ruta relativa a la carpeta del proyecto, por ejemplo: modelos/casa.obj");
		}
	}

	if (e.type != EntityType::LIGHT) {
		ImGui::SeparatorText("Comportamientos");
		drag_float("Girar", &e.spin_speed, 1.0f, -3600, 3600, "%.0f °/s");
		drag_float("Flotar", &e.bob_amplitude, is3d ? 0.01f : 0.5f, 0, 1000, is3d ? "%.2f m" : "%.0f px");
		if (e.bob_amplitude != 0.0f) {
			drag_float("Frecuencia", &e.bob_frequency, 0.01f, 0, 20, "%.2f Hz");
		}
		checkbox("Sólido", &e.solid);
		checkbox("Coleccionable", &e.collectible);
		checkbox("Peligroso (reinicia al jugador)", &e.hazard);
		checkbox("Meta (completa el nivel)", &e.goal);
		if (is3d || g_ed.scene.platformer) {
			drag_float("Trampolín", &e.bounce, 0.1f, 0, 100, e.bounce > 0 ? "%.1f m/s" : "No");
		}
		{
			bool moving = e.has_motion();
			if (ImGui::Checkbox("Plataforma móvil", &moving)) {
				if (moving) {
					if (is3d) {
						e.move_y = 3.0f;
					} else {
						e.move_x = 200.0f;
					}
				} else {
					e.move_x = e.move_y = e.move_z = 0.0f;
				}
				g_ed.dirty = true;
			}
			if (moving) {
				if (is3d) {
					float m[3] = { e.move_x, e.move_y, e.move_z };
					if (ImGui::DragFloat3("Recorrido", m, 0.05f, 0, 0, "%.2f")) {
						e.move_x = m[0];
						e.move_y = m[1];
						e.move_z = m[2];
						g_ed.dirty = true;
					}
				} else {
					float m[2] = { e.move_x, e.move_y };
					if (ImGui::DragFloat2("Recorrido", m, 1.0f, 0, 0, "%.0f")) {
						e.move_x = m[0];
						e.move_y = m[1];
						g_ed.dirty = true;
					}
				}
				drag_float("Duración", &e.move_time, 0.02f, 0.1f, 120, "%.2f s");
			}
		}
		{
			bool enemy = e.chase_speed != 0.0f;
			if (ImGui::Checkbox("Enemigo (persigue al jugador)", &enemy)) {
				e.chase_speed = enemy ? (is3d ? 2.5f : 120.0f) : 0.0f;
				if (enemy) {
					e.hazard = true;
					e.chase_range = is3d ? 8.0f : 300.0f;
				}
				g_ed.dirty = true;
			}
			if (enemy) {
				drag_float("Velocidad enemigo", &e.chase_speed, is3d ? 0.05f : 1.0f, 0.01f, 5000, is3d ? "%.1f m/s" : "%.0f px/s");
				drag_float("Distancia de visión", &e.chase_range, is3d ? 0.1f : 2.0f, 0, 100000, is3d ? "%.1f m" : "%.0f px");
			}
		}
		checkbox("Controlado por el jugador", &e.player);
		if (e.player) {
			drag_float("Velocidad", &e.player_speed, is3d ? 0.05f : 1.0f, 0, 5000, is3d ? "%.1f m/s" : "%.0f px/s");
			if (is3d) {
				checkbox("Puede saltar (Espacio)", &e.can_jump);
				if (e.can_jump) {
					drag_float("Fuerza de salto", &e.jump_force, 0.05f, 0, 100, "%.1f");
				}
			} else if (g_ed.scene.platformer) {
				drag_float("Fuerza de salto", &e.jump_force, 0.05f, 0, 100, "%.1f");
			}
			ui::dim_text(is3d ? "Se mueve con WASD o las flechas, salta con Espacio y choca con lo sólido." : "Se mueve con WASD o las flechas y choca con las formas sólidas.");
		}
		checkbox("La cámara lo sigue", &e.camera_follow);
		if (e.collectible || e.goal || e.bounce > 0.0f) {
			sound_field(p_app, "Sonido (WAV)", e.sound);
			ui::dim_text("Suena al recogerlo o tocarlo. Vacío: efecto integrado.");
		}
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();
}

static bool hit_test(const Entity &p_e, float p_wx, float p_wy) {
	if (p_e.type == EntityType::CIRCLE) {
		const float dx = p_wx - p_e.x;
		const float dy = p_wy - p_e.y;
		return dx * dx + dy * dy <= p_e.width * p_e.width * 0.25f;
	}
	float hw = p_e.width * 0.5f;
	float hh = p_e.height * 0.5f;
	if (p_e.type == EntityType::TEXT) {
		hw = std::max(20.0f, p_e.text_size * 0.3f * (float)p_e.text.size());
		hh = p_e.text_size * 0.6f;
	}
	// Rotate the point into the entity's local space.
	const float r = -p_e.rotation * 0.0174532925f;
	const float dx = p_wx - p_e.x;
	const float dy = p_wy - p_e.y;
	const float lx = dx * std::cos(r) - dy * std::sin(r);
	const float ly = dx * std::sin(r) + dy * std::cos(r);
	return std::fabs(lx) <= hw && std::fabs(ly) <= hh;
}

static void draw_toolbar(App &p_app) {
	const float s = p_app.scale();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::SIDEBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 9 * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::BeginChild("Toolbar", ImVec2(0, 54 * s), ImGuiChildFlags_AlwaysUseWindowPadding);
	if (ImGui::Button("<  Proyectos")) {
		if (g_ed.dirty) {
			g_ed.show_close_dialog = true;
		} else {
			p_app.close_project();
		}
	}
	ImGui::SameLine();
	ImGui::Image(ImTextureRef(p_app.logo_tile), ImVec2(30 * s, 30 * s));
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s%s", p_app.project.name.c_str(), g_ed.dirty ? " *" : "");

	const float right = 380 * s;
	ImGui::SameLine(ImGui::GetWindowWidth() - right - 200 * s);
	ImGui::BeginDisabled(g_ed.undo_stack.empty());
	if (ImGui::Button("Deshacer", ImVec2(92 * s, 0))) {
		undo();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("Ctrl+Z");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(g_ed.redo_stack.empty());
	if (ImGui::Button("Rehacer", ImVec2(92 * s, 0))) {
		redo();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("Ctrl+Y");
	}
	ImGui::SameLine(ImGui::GetWindowWidth() - right);
	if (ImGui::Button("Guardar", ImVec2(100 * s, 0))) {
		save(p_app);
	}
	ImGui::SameLine();
	if (ImGui::Button("Exportar", ImVec2(110 * s, 0))) {
		export_windows(p_app);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Exporta el juego para Windows (.exe + datos)");
	}
	ImGui::SameLine();
	if (ui::accent_button("Ejecutar  (F5)", ImVec2(-1, 0))) {
		run_project(p_app);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

static void draw_viewport_2d(App &p_app, float p_width) {
	const float s = p_app.scale();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("Viewport", ImVec2(p_width, 0), ImGuiChildFlags_Borders);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size = ImGui::GetContentRegionAvail();
	g_ed.view2d_pos = pos;
	g_ed.view2d_size = size;
	ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
	const ImVec2 origin(center.x - g_ed.pan.x * g_ed.zoom, center.y - g_ed.pan.y * g_ed.zoom);
	ImGuiIO &io = ImGui::GetIO();
	auto to_world = [&](ImVec2 p_screen) { return ImVec2((p_screen.x - origin.x) / g_ed.zoom, (p_screen.y - origin.y) / g_ed.zoom); };

	// Input: zoom around the mouse, pan with right/middle drag, select and move with left.
	if (hovered && io.MouseWheel != 0.0f) {
		const ImVec2 before = to_world(io.MousePos);
		g_ed.zoom = std::clamp(g_ed.zoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.1f, 8.0f);
		const ImVec2 new_origin_after(center.x - g_ed.pan.x * g_ed.zoom, center.y - g_ed.pan.y * g_ed.zoom);
		const ImVec2 after((io.MousePos.x - new_origin_after.x) / g_ed.zoom, (io.MousePos.y - new_origin_after.y) / g_ed.zoom);
		g_ed.pan.x += before.x - after.x;
		g_ed.pan.y += before.y - after.y;
	}
	if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
		g_ed.pan.x -= io.MouseDelta.x / g_ed.zoom;
		g_ed.pan.y -= io.MouseDelta.y / g_ed.zoom;
	}
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const ImVec2 w = to_world(io.MousePos);
		g_ed.selected = -1;
		for (int i = (int)g_ed.scene.entities.size() - 1; i >= 0; i--) {
			if (hit_test(g_ed.scene.entities[i], w.x, w.y)) {
				g_ed.selected = i;
				g_ed.dragging = true;
				g_ed.drag_offset = ImVec2(g_ed.scene.entities[i].x - w.x, g_ed.scene.entities[i].y - w.y);
				break;
			}
		}
	}
	if (g_ed.dragging) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && g_ed.selected >= 0) {
			const ImVec2 w = to_world(io.MousePos);
			Entity &e = g_ed.scene.entities[g_ed.selected];
			float nx = w.x + g_ed.drag_offset.x;
			float ny = w.y + g_ed.drag_offset.y;
			if (io.KeyCtrl) { // Snap to the grid.
				nx = std::round(nx / 16.0f) * 16.0f;
				ny = std::round(ny / 16.0f) * 16.0f;
			}
			if (nx != e.x || ny != e.y) {
				e.x = nx;
				e.y = ny;
				g_ed.dirty = true;
			}
		} else {
			g_ed.dragging = false;
		}
	}

	// Background and grid.
	dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
	const uint32_t bgc = g_ed.scene.background;
	dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32((bgc >> 16) & 0xFF, (bgc >> 8) & 0xFF, bgc & 0xFF, 255));
	float step = 64.0f * g_ed.zoom;
	while (step < 24.0f) {
		step *= 2.0f;
	}
	const ImU32 grid = IM_COL32(255, 255, 255, 18);
	for (float x = std::fmod(origin.x - pos.x, step); x < size.x; x += step) {
		dl->AddLine(ImVec2(pos.x + x, pos.y), ImVec2(pos.x + x, pos.y + size.y), grid);
	}
	for (float y = std::fmod(origin.y - pos.y, step); y < size.y; y += step) {
		dl->AddLine(ImVec2(pos.x, pos.y + y), ImVec2(pos.x + size.x, pos.y + y), grid);
	}
	dl->AddLine(ImVec2(origin.x, pos.y), ImVec2(origin.x, pos.y + size.y), IM_COL32(108, 123, 255, 70));
	dl->AddLine(ImVec2(pos.x, origin.y), ImVec2(pos.x + size.x, origin.y), IM_COL32(255, 107, 107, 70));
	// Game view frame (1280x720 centered on the origin, like the default camera).
	dl->AddRect(ImVec2(origin.x - 640 * g_ed.zoom, origin.y - 360 * g_ed.zoom), ImVec2(origin.x + 640 * g_ed.zoom, origin.y + 360 * g_ed.zoom), IM_COL32(255, 255, 255, 60), 0, 0, 1.5f);

	draw_scene(dl, g_ed.scene, origin, g_ed.zoom, p_app.font, 0.0, false, &g_sprites);

	// Selection outline.
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		const Entity &e = g_ed.scene.entities[g_ed.selected];
		const ImVec2 c(origin.x + e.x * g_ed.zoom, origin.y + e.y * g_ed.zoom);
		if (e.type == EntityType::CIRCLE) {
			dl->AddCircle(c, e.width * 0.5f * g_ed.zoom + 4, theme::ACCENT, 0, 2.0f);
		} else {
			float hw = e.width * 0.5f * g_ed.zoom + 4;
			float hh = e.height * 0.5f * g_ed.zoom + 4;
			if (e.type == EntityType::TEXT) {
				const ImVec2 ts = p_app.font->CalcTextSizeA(std::max(4.0f, e.text_size * g_ed.zoom), FLT_MAX, 0, e.text.c_str());
				hw = ts.x * 0.5f + 4;
				hh = ts.y * 0.5f + 4;
			}
			const float r = (e.type == EntityType::TEXT ? 0.0f : e.rotation) * 0.0174532925f;
			const float cs = std::cos(r), sn = std::sin(r);
			auto corner = [&](float px, float py) { return ImVec2(c.x + px * cs - py * sn, c.y + px * sn + py * cs); };
			dl->AddQuad(corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), theme::ACCENT, 2.0f);
		}
	}

	// Overlay hints.
	char info[96];
	snprintf(info, sizeof(info), "Zoom %d%%   ·   Rueda: zoom   ·   Botón derecho: mover vista   ·   Ctrl: ajustar", (int)std::lround(g_ed.zoom * 100));
	dl->AddText(ImVec2(pos.x + 12 * s, pos.y + size.y - 28 * s), IM_COL32(255, 255, 255, 120), info);
	dl->PopClipRect();
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

static void draw_close_dialog(App &p_app) {
	if (g_ed.show_close_dialog) {
		ImGui::OpenPopup("Cambios sin guardar");
		g_ed.show_close_dialog = false;
	}
	ImGui::SetNextWindowSize(ImVec2(440 * p_app.scale(), 0));
	if (ImGui::BeginPopupModal("Cambios sin guardar", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
		ui::section_title("¿Guardar los cambios?");
		ui::dim_text("La escena tiene cambios sin guardar.");
		ImGui::Spacing();
		const float bw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
		if (ImGui::Button("Cancelar", ImVec2(bw, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No guardar", ImVec2(bw, 0))) {
			g_ed.dirty = false;
			ImGui::CloseCurrentPopup();
			p_app.close_project();
		}
		ImGui::SameLine();
		if (ui::accent_button("Guardar", ImVec2(bw, 0))) {
			if (save(p_app)) {
				ImGui::CloseCurrentPopup();
				p_app.close_project();
			}
		}
		ImGui::EndPopup();
	}
}

void draw_editor(App &p_app) {
	const float s = p_app.scale();
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar(2);

	// Shortcuts.
	ImGuiIO &io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
		save(p_app);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
		run_project(p_app);
	}
	if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		delete_selected();
	}
	if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z, true)) {
		if (io.KeyShift) {
			redo();
		} else {
			undo();
		}
	}
	if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Y, true)) {
		redo();
	}
	if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
		copy_selected();
	}
	if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
		paste_clipboard();
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
		duplicate_selected();
	}
	if (!io.WantTextInput && !io.KeyCtrl && g_ed.scene.is_3d) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
			g_ed.gizmo_mode = 0;
		} else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
			g_ed.gizmo_mode = 1;
		} else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			g_ed.gizmo_mode = 2;
		}
	}
	if (!io.WantTextInput && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		focus_selected();
	}

	draw_toolbar(p_app);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 12 * s));
	ImGui::SetCursorPos(ImVec2(8 * s, ImGui::GetCursorPosY() + 8 * s));
	ImGui::BeginGroup();
	const float total = ImGui::GetContentRegionAvail().x - 8 * s;
	const float left = 250 * s;
	const float right = 330 * s;
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	ImGui::BeginChild("EditorBody", ImVec2(total, ImGui::GetContentRegionAvail().y - 8 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	draw_scene_panel(p_app, left);
	ImGui::SameLine();
	const float view_w = std::max(200.0f, total - left - right - gap * 2);
	if (g_ed.scene.is_3d) {
		draw_viewport_3d(p_app, view_w);
	} else {
		draw_viewport_2d(p_app, view_w);
	}
	ImGui::SameLine();
	draw_inspector(p_app);
	ImGui::EndChild();
	ImGui::EndGroup();
	ImGui::PopStyleVar();

	draw_close_dialog(p_app);
	ImGui::End();
	history_track();
}

} // namespace ipde
