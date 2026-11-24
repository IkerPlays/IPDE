#include "app/app.h"

#include "core/fs.h"
#include "core/json.h"
#include "platform/platform.h"
#include "version.h"

#include <imgui_internal.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb/stb_image.h>

#include <algorithm>
#include <cstdarg>

extern const uint8_t res_font_roboto[];
extern const size_t res_font_roboto_size;
extern const uint8_t res_logo_tile[];
extern const size_t res_logo_tile_size;
extern const uint8_t res_logo_wordmark[];
extern const size_t res_logo_wordmark_size;

namespace ipde {

/* Settings */

static std::string settings_path() {
	return fs::join(platform::get_app_data_dir(), "settings.json");
}

void Settings::load() {
	std::string text;
	if (!fs::read_file(settings_path(), text)) {
		return;
	}
	const Json j = Json::parse(text);
	auto_update = j["auto_update"].as_bool(true);
	include_devbuilds = j["include_devbuilds"].as_bool(false);
	ui_scale = (float)j["ui_scale"].as_number(1.0);
	last_run_version = j["last_run_version"].as_string();
	skipped_tag = j["skipped_tag"].as_string();
	last_auto_tag = j["last_auto_tag"].as_string();
	last_auto_time = (int64_t)j["last_auto_time"].as_number(0);
	projects.clear();
	for (const Json &p : j["projects"].get_items()) {
		RecentProject rp;
		rp.name = p["name"].as_string();
		rp.path = p["path"].as_string();
		rp.last_opened = (int64_t)p["last_opened"].as_number(0);
		if (!rp.path.empty()) {
			projects.push_back(rp);
		}
	}
}

void Settings::save() const {
	Json j = Json::object();
	j["auto_update"] = auto_update;
	j["include_devbuilds"] = include_devbuilds;
	j["ui_scale"] = (double)ui_scale;
	j["last_run_version"] = last_run_version;
	j["skipped_tag"] = skipped_tag;
	j["last_auto_tag"] = last_auto_tag;
	j["last_auto_time"] = (double)last_auto_time;
	Json list = Json::array();
	for (const RecentProject &rp : projects) {
		Json p = Json::object();
		p["name"] = rp.name;
		p["path"] = rp.path;
		p["last_opened"] = (double)rp.last_opened;
		list.push_back(p);
	}
	j["projects"] = list;
	fs::write_file(settings_path(), j.dump(true));
}

void Settings::touch_project(const Project &p_project) {
	remove_project(p_project.path);
	RecentProject rp;
	rp.name = p_project.name;
	rp.path = p_project.path;
	rp.last_opened = fs::now_unix();
	projects.insert(projects.begin(), rp);
	save();
}

void Settings::remove_project(const std::string &p_path) {
	projects.erase(std::remove_if(projects.begin(), projects.end(), [&](const RecentProject &rp) { return rp.path == p_path; }), projects.end());
}

/* UI helpers */

namespace ui {

bool accent_button(const char *p_label, const ImVec2 &p_size) {
	ImGui::PushStyleColor(ImGuiCol_Button, theme::ACCENT);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(128, 142, 255, 255));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::ACCENT_2);
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
	const bool pressed = ImGui::Button(p_label, p_size);
	ImGui::PopStyleColor(4);
	return pressed;
}

void section_title(const char *p_text) {
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
	ImGui::TextUnformatted(p_text);
	ImGui::PopFont();
}

void dim_text(const char *p_fmt, ...) {
	va_list args;
	va_start(args, p_fmt);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
	ImGui::TextWrappedV(p_fmt, args);
	ImGui::PopStyleColor();
	va_end(args);
}

} // namespace ui

/* App */

App &App::get() {
	static App app;
	return app;
}

ImTextureID App::_load_image(const unsigned char *p_data, size_t p_size, ImVec2 &r_size) {
	int w = 0, h = 0, n = 0;
	unsigned char *pixels = stbi_load_from_memory(p_data, (int)p_size, &w, &h, &n, 4);
	if (!pixels) {
		return ImTextureID_Invalid;
	}
	const ImTextureID id = renderer.create_texture(pixels, w, h);
	stbi_image_free(pixels);
	r_size = ImVec2((float)w, (float)h);
	return id;
}

void App::apply_style() {
	ui_scale_applied = platform::get_dpi_scale() * std::clamp(settings.ui_scale, 0.75f, 2.0f);
	ImGuiStyle &style = ImGui::GetStyle();
	style = ImGuiStyle();
	style.WindowRounding = 10.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 8.0f;
	style.PopupRounding = 10.0f;
	style.GrabRounding = 8.0f;
	style.TabRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	style.WindowPadding = ImVec2(16, 16);
	style.FramePadding = ImVec2(12, 7);
	style.ItemSpacing = ImVec2(10, 9);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.ScrollbarSize = 12.0f;
	style.WindowBorderSize = 0.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.SeparatorTextBorderSize = 1.0f;
	style.AntiAliasedLines = true;
	style.AntiAliasedFill = true;
	// The software renderer samples textures without bilinear filtering, which textured lines need.
	style.AntiAliasedLinesUseTex = false;

	ImVec4 *c = style.Colors;
	auto col = [](ImU32 p_c) { return ImGui::ColorConvertU32ToFloat4(p_c); };
	c[ImGuiCol_Text] = col(theme::TEXT);
	c[ImGuiCol_TextDisabled] = col(theme::TEXT_DIM);
	c[ImGuiCol_WindowBg] = col(theme::BG);
	c[ImGuiCol_ChildBg] = col(theme::PANEL);
	c[ImGuiCol_PopupBg] = col(IM_COL32(26, 28, 48, 250));
	c[ImGuiCol_Border] = col(theme::BORDER);
	c[ImGuiCol_FrameBg] = col(IM_COL32(38, 41, 70, 255));
	c[ImGuiCol_FrameBgHovered] = col(IM_COL32(46, 50, 86, 255));
	c[ImGuiCol_FrameBgActive] = col(IM_COL32(54, 58, 100, 255));
	c[ImGuiCol_TitleBg] = col(theme::SIDEBAR);
	c[ImGuiCol_TitleBgActive] = col(theme::SIDEBAR);
	c[ImGuiCol_MenuBarBg] = col(theme::SIDEBAR);
	c[ImGuiCol_ScrollbarBg] = col(IM_COL32(0, 0, 0, 0));
	c[ImGuiCol_ScrollbarGrab] = col(IM_COL32(60, 64, 104, 255));
	c[ImGuiCol_ScrollbarGrabHovered] = col(IM_COL32(80, 86, 140, 255));
	c[ImGuiCol_ScrollbarGrabActive] = col(theme::ACCENT);
	c[ImGuiCol_CheckMark] = col(theme::ACCENT);
	c[ImGuiCol_SliderGrab] = col(theme::ACCENT);
	c[ImGuiCol_SliderGrabActive] = col(theme::ACCENT_2);
	c[ImGuiCol_Button] = col(IM_COL32(44, 47, 80, 255));
	c[ImGuiCol_ButtonHovered] = col(IM_COL32(58, 62, 104, 255));
	c[ImGuiCol_ButtonActive] = col(IM_COL32(72, 78, 130, 255));
	c[ImGuiCol_Header] = col(IM_COL32(108, 123, 255, 60));
	c[ImGuiCol_HeaderHovered] = col(IM_COL32(108, 123, 255, 90));
	c[ImGuiCol_HeaderActive] = col(IM_COL32(108, 123, 255, 130));
	c[ImGuiCol_Separator] = col(theme::BORDER);
	c[ImGuiCol_Tab] = col(IM_COL32(32, 34, 58, 255));
	c[ImGuiCol_TabHovered] = col(IM_COL32(58, 62, 104, 255));
	c[ImGuiCol_TabSelected] = col(IM_COL32(52, 56, 98, 255));
	c[ImGuiCol_PlotHistogram] = col(theme::ACCENT);
	c[ImGuiCol_TextSelectedBg] = col(IM_COL32(108, 123, 255, 110));
	c[ImGuiCol_ModalWindowDimBg] = col(IM_COL32(5, 6, 14, 170));
	c[ImGuiCol_NavCursor] = col(theme::ACCENT);

	style.ScaleAllSizes(ui_scale_applied);
	style.FontSizeBase = font_size * ui_scale_applied;
}

void App::init() {
	settings.load();
	updater.init();

	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr; // IPDE stores its own settings.
	ImFontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	font = io.Fonts->AddFontFromMemoryTTF((void *)res_font_roboto, (int)res_font_roboto_size, font_size, &cfg);
	apply_style();

	logo_tile = _load_image(res_logo_tile, res_logo_tile_size, logo_tile_size);
	logo_wordmark = _load_image(res_logo_wordmark, res_logo_wordmark_size, logo_wordmark_size);

	_check_whats_new();
	updater.check(true);
}

void App::shutdown() {
	settings.save();
}

void App::toast(const std::string &p_text, bool p_error) {
	Toast t;
	t.text = p_text;
	t.error = p_error;
	t.until = platform::get_time() + 6.0;
	toasts.push_back(t);
}

bool App::needs_redraw() const {
	return !toasts.empty() || updater.is_busy();
}

void App::_check_whats_new() {
	const std::string current = IPDE_VERSION_DISPLAY " (" IPDE_VERSION_CHANNEL ")";
	if (!settings.last_run_version.empty() && settings.last_run_version != current) {
		show_whats_new = true;
		whats_new_title = "¡IPDE se ha actualizado!";
		whats_new_text = "Ahora usas " + current + ".\n\nPuedes ver las novedades de cada versión en la sección Versiones.";
	}
	settings.last_run_version = current;
	settings.save();
}

void App::open_project(const std::string &p_folder) {
	Project p;
	if (!Project::load(p_folder, p)) {
		toast("No se pudo abrir el proyecto: falta project.ipde.", true);
		return;
	}
	project = p;
	settings.touch_project(project);
	screen = Screen::EDITOR;
	platform::set_window_title(project.name + " - IPDE");
	editor_open(*this);
}

void App::close_project() {
	screen = Screen::DASHBOARD;
	platform::set_window_title("IPDE");
}

bool App::has_unsaved_changes() const {
	return screen == Screen::EDITOR && editor_has_unsaved_changes();
}

void App::_draw_toasts() {
	const double now = platform::get_time();
	toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast &t) { return t.until < now; }), toasts.end());
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	float y = vp->Size.y - 20.0f * scale();
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
		const float wrap = 380.0f * scale();
		const ImVec2 text_size = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, wrap);
		const ImVec2 pad(16 * scale(), 12 * scale());
		const ImVec2 size(text_size.x + pad.x * 2 + 6 * scale(), text_size.y + pad.y * 2);
		const ImVec2 pos(vp->Size.x - size.x - 20 * scale(), y - size.y);
		dl->AddRectFilled(ImVec2(pos.x + 2, pos.y + 4), ImVec2(pos.x + size.x + 2, pos.y + size.y + 4), IM_COL32(0, 0, 0, 90), 10 * scale());
		dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(34, 37, 64, 250), 10 * scale());
		dl->AddRectFilled(pos, ImVec2(pos.x + 5 * scale(), pos.y + size.y), it->error ? theme::DANGER : theme::ACCENT, 10 * scale(), ImDrawFlags_RoundCornersLeft);
		dl->AddText(nullptr, 0.0f, ImVec2(pos.x + pad.x + 6 * scale(), pos.y + pad.y), theme::TEXT, it->text.c_str(), nullptr, wrap);
		y = pos.y - 10 * scale();
	}
}

void App::frame() {
	updater.update();

	if (screen == Screen::DASHBOARD) {
		draw_dashboard(*this);
	} else {
		draw_editor(*this);
	}

	if (show_whats_new) {
		ImGui::OpenPopup("¿Qué hay de nuevo?");
		show_whats_new = false;
	}
	ImGui::SetNextWindowSize(ImVec2(460 * scale(), 0));
	if (ImGui::BeginPopupModal("¿Qué hay de nuevo?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
		ui::section_title(whats_new_title.c_str());
		ImGui::Spacing();
		ImGui::TextWrapped("%s", whats_new_text.c_str());
		ImGui::Spacing();
		if (ui::accent_button("¡Genial!", ImVec2(-1, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	_draw_toasts();
}

} // namespace ipde
