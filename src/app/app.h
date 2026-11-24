#pragma once

// IPDE application: shared state, settings, theme and notifications.

#include "app/scene.h"
#include "app/updater.h"
#include "render/soft_renderer.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace ipde {

struct RecentProject {
	std::string name;
	std::string path;
	int64_t last_opened = 0;
};

struct Settings {
	std::vector<RecentProject> projects;
	bool auto_update = true;
	bool include_devbuilds = false; // Stable installs also update to devbuilds.
	float ui_scale = 1.0f;
	std::string last_run_version;
	std::string skipped_tag;
	std::string last_auto_tag; // Loop guard for automatic updates.
	int64_t last_auto_time = 0;

	void load();
	void save() const;
	void touch_project(const Project &p_project);
	void remove_project(const std::string &p_path);
};

struct Toast {
	std::string text;
	double until = 0.0;
	bool error = false;
};

namespace theme {
// IPDE palette (from the logo).
constexpr ImU32 BG = IM_COL32(16, 17, 31, 255);
constexpr ImU32 SIDEBAR = IM_COL32(22, 24, 42, 255);
constexpr ImU32 PANEL = IM_COL32(28, 30, 52, 255);
constexpr ImU32 PANEL_HOVER = IM_COL32(36, 39, 68, 255);
constexpr ImU32 BORDER = IM_COL32(52, 56, 92, 255);
constexpr ImU32 ACCENT = IM_COL32(108, 123, 255, 255);
constexpr ImU32 ACCENT_2 = IM_COL32(139, 61, 255, 255);
constexpr ImU32 TEXT = IM_COL32(233, 235, 255, 255);
constexpr ImU32 TEXT_DIM = IM_COL32(150, 156, 200, 255);
constexpr ImU32 SUCCESS = IM_COL32(77, 255, 184, 255);
constexpr ImU32 WARNING = IM_COL32(255, 197, 61, 255);
constexpr ImU32 DANGER = IM_COL32(255, 107, 107, 255);
} // namespace theme

class App {
public:
	enum class Screen {
		DASHBOARD,
		EDITOR,
	};

	static App &get();

	SoftRenderer renderer;
	Settings settings;
	Updater updater;

	ImFont *font = nullptr;
	float font_size = 17.0f;
	ImTextureID logo_tile = ImTextureID_Invalid;
	ImVec2 logo_tile_size;
	ImTextureID logo_wordmark = ImTextureID_Invalid;
	ImVec2 logo_wordmark_size;

	Screen screen = Screen::DASHBOARD;
	Project project; // Open project (editor).
	bool quit_requested = false;

	void init();
	void frame(); // Draws the UI for this frame.
	void shutdown();

	void toast(const std::string &p_text, bool p_error = false);
	void open_project(const std::string &p_folder);
	void close_project();
	bool has_unsaved_changes() const;
	bool needs_redraw() const;

	void apply_style();
	float scale() const { return ui_scale_applied; }

private:
	std::vector<Toast> toasts;
	float ui_scale_applied = 1.0f;
	bool show_whats_new = false;
	std::string whats_new_title;
	std::string whats_new_text;

	void _draw_toasts();
	void _check_whats_new();
	ImTextureID _load_image(const unsigned char *p_data, size_t p_size, ImVec2 &r_size);
};

// UI pieces (dashboard.cpp / editor.cpp).
void draw_dashboard(App &p_app);
void draw_editor(App &p_app);
bool editor_has_unsaved_changes();
void editor_open(App &p_app);

// Helpers shared by the UI.
namespace ui {
bool accent_button(const char *p_label, const ImVec2 &p_size = ImVec2(0, 0));
void section_title(const char *p_text);
void dim_text(const char *p_fmt, ...);
} // namespace ui

} // namespace ipde
