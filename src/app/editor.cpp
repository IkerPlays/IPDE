// IPDE 2D scene editor.

#include "app/app.h"
#include "app/game.h"
#include "core/fs.h"
#include "platform/platform.h"
#include "version.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ipde {

struct EditorState {
	Scene scene;
	int selected = -1;
	bool dirty = false;
	ImVec2 pan = ImVec2(0, 0); // World position at the center of the viewport.
	float zoom = 1.0f;
	bool dragging = false;
	ImVec2 drag_offset;
	bool show_close_dialog = false;
	std::string last_export;
};

static EditorState g_ed;

bool editor_has_unsaved_changes() {
	return g_ed.dirty;
}

void editor_open(App &p_app) {
	g_ed = EditorState();
	if (!Scene::load(p_app.project.get_scene_file(), g_ed.scene)) {
		g_ed.scene = Scene(); // New empty scene.
	}
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

static bool save(App &p_app) {
	if (g_ed.scene.save(p_app.project.get_scene_file())) {
		g_ed.dirty = false;
		p_app.toast("Escena guardada.");
		return true;
	}
	p_app.toast("No se pudo guardar la escena.", true);
	return false;
}

static void run_project(App &p_app) {
	save(p_app);
	if (!platform::create_process(platform::get_executable_path(), { "--run", p_app.project.path })) {
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
	ok = ok && fs::copy_file(p_app.project.get_scene_file(), fs::join(data_dir, "main.scene"));
	if (ok) {
		g_ed.last_export = out_dir;
		p_app.toast("Juego exportado para Windows en: " + out_dir);
		platform::open_folder(out_dir);
	} else {
		p_app.toast("No se pudo exportar el juego.", true);
	}
}

static void add_entity(EntityType p_type) {
	Entity e;
	e.type = p_type;
	e.x = g_ed.pan.x;
	e.y = g_ed.pan.y;
	switch (p_type) {
		case EntityType::RECT:
			e.name = "Rectángulo";
			break;
		case EntityType::CIRCLE:
			e.name = "Círculo";
			e.color = 0xFF4DD6A0;
			break;
		case EntityType::TEXT:
			e.name = "Texto";
			e.color = 0xFFFFFFFF;
			break;
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

static void duplicate_selected() {
	if (g_ed.selected >= 0 && g_ed.selected < (int)g_ed.scene.entities.size()) {
		Entity copy = g_ed.scene.entities[g_ed.selected];
		copy.name += " (copia)";
		copy.x += 24;
		copy.y += 24;
		g_ed.scene.entities.push_back(copy);
		g_ed.selected = (int)g_ed.scene.entities.size() - 1;
		g_ed.dirty = true;
	}
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

static void draw_scene_panel(App &p_app, float p_width) {
	const float s = p_app.scale();
	ImGui::BeginChild("ScenePanel", ImVec2(p_width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::TextUnformatted("ESCENA");
	ImGui::Separator();
	const float bw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
	if (ImGui::Button("+ Rect", ImVec2(bw, 0))) {
		add_entity(EntityType::RECT);
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Círculo", ImVec2(bw, 0))) {
		add_entity(EntityType::CIRCLE);
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Texto", ImVec2(bw, 0))) {
		add_entity(EntityType::TEXT);
	}
	ImGui::Spacing();
	ImGui::BeginChild("EntityList", ImVec2(0, -84 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	for (int i = 0; i < (int)g_ed.scene.entities.size(); i++) {
		const Entity &e = g_ed.scene.entities[i];
		ImGui::PushID(i);
		const std::string label = std::string(e.player ? "[J] " : "") + e.name + "##e";
		if (ImGui::Selectable(label.c_str(), g_ed.selected == i)) {
			g_ed.selected = i;
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
	float bg[4];
	float4_from_argb(g_ed.scene.background, bg);
	if (ImGui::ColorEdit3("Fondo", bg, ImGuiColorEditFlags_NoInputs)) {
		bg[3] = 1.0f;
		g_ed.scene.background = argb_from_float4(bg);
		g_ed.dirty = true;
	}
	ImGui::EndChild();
}

static void draw_viewport(App &p_app, float p_width) {
	const float s = p_app.scale();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::BeginChild("Viewport", ImVec2(p_width, 0), ImGuiChildFlags_Borders);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size = ImGui::GetContentRegionAvail();
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

	draw_scene(dl, g_ed.scene, origin, g_ed.zoom, p_app.font, 0.0, false);

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

static bool drag_float(const char *p_label, float *p_value, float p_speed, float p_min = 0, float p_max = 0, const char *p_fmt = "%.1f") {
	const bool changed = ImGui::DragFloat(p_label, p_value, p_speed, p_min, p_max, p_fmt);
	if (changed) {
		g_ed.dirty = true;
	}
	return changed;
}

static void draw_inspector(App &p_app) {
	ImGui::BeginChild("Inspector", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::TextUnformatted("INSPECTOR");
	ImGui::Separator();
	if (g_ed.selected < 0 || g_ed.selected >= (int)g_ed.scene.entities.size()) {
		ui::dim_text("Selecciona una entidad en la escena o en el visor para editarla.");
		ImGui::EndChild();
		return;
	}
	Entity &e = g_ed.scene.entities[g_ed.selected];
	ImGui::PushItemWidth(-90 * p_app.scale());
	char name[128];
	strncpy(name, e.name.c_str(), sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	if (ImGui::InputText("Nombre", name, sizeof(name))) {
		e.name = name;
		g_ed.dirty = true;
	}
	ImGui::TextDisabled("Tipo: %s", entity_type_name(e.type));
	if (ImGui::Checkbox("Visible", &e.visible)) {
		g_ed.dirty = true;
	}

	ImGui::SeparatorText("Transformación");
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
		char text[256];
		strncpy(text, e.text.c_str(), sizeof(text) - 1);
		text[sizeof(text) - 1] = 0;
		if (ImGui::InputText("Texto", text, sizeof(text))) {
			e.text = text;
			g_ed.dirty = true;
		}
		drag_float("Tamaño", &e.text_size, 0.5f, 4.0f, 400.0f, "%.0f");
	}
	if (e.type != EntityType::TEXT) {
		drag_float("Rotación", &e.rotation, 0.5f, 0, 0, "%.0f°");
	}
	float col[4];
	float4_from_argb(e.color, col);
	if (ImGui::ColorEdit4("Color", col)) {
		e.color = argb_from_float4(col);
		g_ed.dirty = true;
	}

	ImGui::SeparatorText("Comportamientos");
	drag_float("Girar", &e.spin_speed, 1.0f, -3600, 3600, "%.0f °/s");
	drag_float("Flotar", &e.bob_amplitude, 0.5f, 0, 1000, "%.0f px");
	if (e.bob_amplitude != 0.0f) {
		drag_float("Frecuencia", &e.bob_frequency, 0.01f, 0, 20, "%.2f Hz");
	}
	if (ImGui::Checkbox("Controlado por el jugador", &e.player)) {
		g_ed.dirty = true;
	}
	if (e.player) {
		drag_float("Velocidad", &e.player_speed, 1.0f, 0, 5000, "%.0f px/s");
		ui::dim_text("Se mueve con WASD o las flechas y choca con las formas sólidas.");
	}
	if (ImGui::Checkbox("La cámara lo sigue", &e.camera_follow)) {
		g_ed.dirty = true;
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();
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
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
		duplicate_selected();
	}

	draw_toolbar(p_app);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 12 * s));
	ImGui::SetCursorPos(ImVec2(8 * s, ImGui::GetCursorPosY() + 8 * s));
	ImGui::BeginGroup();
	const float total = ImGui::GetContentRegionAvail().x - 8 * s;
	const float left = 260 * s;
	const float right = 320 * s;
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	ImGui::BeginChild("EditorBody", ImVec2(total, ImGui::GetContentRegionAvail().y - 8 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	draw_scene_panel(p_app, left);
	ImGui::SameLine();
	draw_viewport(p_app, std::max(200.0f, total - left - right - gap * 2));
	ImGui::SameLine();
	draw_inspector(p_app);
	ImGui::EndChild();
	ImGui::EndGroup();
	ImGui::PopStyleVar();

	draw_close_dialog(p_app);
	ImGui::End();
}

} // namespace ipde
