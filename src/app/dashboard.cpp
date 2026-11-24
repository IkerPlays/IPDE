// IPDE dashboard: projects, versions, settings and about.

#include "app/app.h"
#include "core/fs.h"
#include "platform/platform.h"
#include "version.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

namespace ipde {

enum class Page {
	PROJECTS,
	VERSIONS,
	SETTINGS,
	ABOUT,
};

static Page g_page = Page::PROJECTS;
static char g_search[128] = "";
static char g_new_name[128] = "Mi juego";
static bool g_new_3d = true;
static char g_new_location[1024] = "";
static std::string g_new_error;
static char g_import_path[1024] = "";
static std::string g_import_error;
static bool g_show_devbuilds = true;
static int g_selected_release = -1;

static std::string format_date(int64_t p_unix) {
	if (p_unix <= 0) {
		return "nunca";
	}
	const int64_t diff = fs::now_unix() - p_unix;
	if (diff < 60) {
		return "hace un momento";
	}
	if (diff < 3600) {
		return "hace " + std::to_string(diff / 60) + " min";
	}
	if (diff < 86400) {
		return "hace " + std::to_string(diff / 3600) + " h";
	}
	if (diff < 86400 * 30) {
		return "hace " + std::to_string(diff / 86400) + " días";
	}
	time_t t = (time_t)p_unix;
	char buf[32];
	strftime(buf, sizeof(buf), "%d/%m/%Y", localtime(&t));
	return buf;
}

static ImU32 project_color(const std::string &p_name) {
	static const ImU32 colors[] = {
		IM_COL32(108, 123, 255, 255),
		IM_COL32(139, 61, 255, 255),
		IM_COL32(255, 197, 61, 255),
		IM_COL32(77, 214, 160, 255),
		IM_COL32(255, 120, 150, 255),
		IM_COL32(80, 180, 255, 255),
	};
	uint32_t h = 2166136261u;
	for (char c : p_name) {
		h = (h ^ (uint8_t)c) * 16777619u;
	}
	return colors[h % (sizeof(colors) / sizeof(colors[0]))];
}

static bool nav_item(const char *p_label, bool p_selected, float p_scale) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size(ImGui::GetContentRegionAvail().x, 40 * p_scale);
	const bool pressed = ImGui::InvisibleButton(p_label, size);
	const bool hovered = ImGui::IsItemHovered();
	if (p_selected || hovered) {
		dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), p_selected ? IM_COL32(108, 123, 255, 45) : IM_COL32(255, 255, 255, 12), 8 * p_scale);
	}
	if (p_selected) {
		dl->AddRectFilled(ImVec2(pos.x, pos.y + 9 * p_scale), ImVec2(pos.x + 4 * p_scale, pos.y + size.y - 9 * p_scale), theme::ACCENT, 2 * p_scale);
	}
	const float th = ImGui::GetTextLineHeight();
	dl->AddText(ImVec2(pos.x + 18 * p_scale, pos.y + (size.y - th) * 0.5f), p_selected ? theme::TEXT : theme::TEXT_DIM, p_label);
	if (hovered) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
	return pressed;
}

static void draw_sidebar(App &p_app) {
	const float s = p_app.scale();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::SIDEBAR);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 18 * s));
	ImGui::BeginChild("Sidebar", ImVec2(230 * s, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

	// Brand.
	const float logo = 42 * s;
	ImGui::Image(ImTextureRef(p_app.logo_tile), ImVec2(logo, logo));
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
	ImGui::TextUnformatted("IPDE");
	ImGui::PopFont();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
	ImGui::TextUnformatted("IkersStudio");
	ImGui::PopStyleColor();
	ImGui::EndGroup();
	ImGui::Dummy(ImVec2(0, 14 * s));

	if (nav_item("Proyectos", g_page == Page::PROJECTS, s)) {
		g_page = Page::PROJECTS;
	}
	if (nav_item("Versiones", g_page == Page::VERSIONS, s)) {
		g_page = Page::VERSIONS;
	}
	if (nav_item("Ajustes", g_page == Page::SETTINGS, s)) {
		g_page = Page::SETTINGS;
	}
	if (nav_item("Acerca de", g_page == Page::ABOUT, s)) {
		g_page = Page::ABOUT;
	}

	// Version + update status at the bottom.
	const float bottom_h = 74 * s;
	ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - bottom_h - ImGui::GetStyle().WindowPadding.y));
	ImGui::Separator();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
	ImGui::Text("%s (%s)", IPDE_VERSION_DISPLAY, IPDE_VERSION_CHANNEL);
	ImGui::PopStyleColor();
	const Updater &up = p_app.updater;
	ImU32 dot = theme::TEXT_DIM;
	std::string label = "Sin comprobar";
	if (up.get_state() == Updater::State::CHECKING) {
		label = "Buscando versiones...";
	} else if (up.get_state() == Updater::State::DOWNLOADING) {
		label = "Actualizando... " + std::to_string((int)(up.get_progress() * 100)) + "%";
		dot = theme::ACCENT;
	} else if (up.get_state() == Updater::State::INSTALLED) {
		label = "Reinicia para actualizar";
		dot = theme::WARNING;
	} else if (up.get_state() == Updater::State::FAILED) {
		label = "Sin conexión";
		dot = theme::DANGER;
	} else if (up.has_checked()) {
		const Release *newest = up.get_newest_available();
		label = newest ? newest->display_name() + " disponible" : "Al día";
		dot = newest ? theme::WARNING : theme::SUCCESS;
	}
	const ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5 * s, p.y + ImGui::GetTextLineHeight() * 0.5f), 4 * s, dot);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16 * s);
	if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, 0))) {
		g_page = Page::VERSIONS;
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

static void open_new_project_popup(App &p_app) {
	if (g_new_location[0] == '\0') {
		const std::string def = fs::join(platform::get_documents_dir(), "IPDE Projects");
		strncpy(g_new_location, def.c_str(), sizeof(g_new_location) - 1);
	}
	g_new_error.clear();
	ImGui::OpenPopup("Nuevo proyecto");
}

static void draw_new_project_popup(App &p_app) {
	const float s = p_app.scale();
	ImGui::SetNextWindowSize(ImVec2(560 * s, 0));
	if (!ImGui::BeginPopupModal("Nuevo proyecto", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
		return;
	}
	ui::section_title("Nuevo proyecto");
	ui::dim_text("Crea un proyecto con una escena de ejemplo lista para ejecutar.");
	ImGui::Spacing();
	ImGui::TextUnformatted("Nombre");
	ImGui::SetNextItemWidth(-1);
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::InputText("##name", g_new_name, sizeof(g_new_name));
	ImGui::TextUnformatted("Tipo de juego");
	{
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		auto choice = [&](const char *p_title, const char *p_desc, bool p_value) {
			const bool selected = g_new_3d == p_value;
			ImGui::PushStyleColor(ImGuiCol_Button, selected ? IM_COL32(108, 123, 255, 70) : IM_COL32(38, 41, 70, 255));
			ImGui::PushStyleColor(ImGuiCol_Border, selected ? theme::ACCENT : theme::BORDER);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
			const std::string label = std::string(p_title) + "\n" + p_desc;
			if (ImGui::Button(label.c_str(), ImVec2(half, 58 * s))) {
				g_new_3d = p_value;
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
		};
		choice("3D", "Mundo 3D con jugador y saltos", true);
		ImGui::SameLine();
		choice("2D", "Escena 2D con formas y textos", false);
	}
	ImGui::TextUnformatted("Ubicación");
	ImGui::SetNextItemWidth(-110 * s);
	ImGui::InputText("##location", g_new_location, sizeof(g_new_location));
	ImGui::SameLine();
	if (ImGui::Button("Examinar...", ImVec2(-1, 0))) {
		const std::string picked = platform::pick_folder("Ubicación del proyecto");
		if (!picked.empty()) {
			strncpy(g_new_location, picked.c_str(), sizeof(g_new_location) - 1);
		}
	}
	std::string folder_name = g_new_name;
	for (char &c : folder_name) {
		if (strchr("\\/:*?\"<>|", c)) {
			c = '_';
		}
	}
	const std::string full = fs::join(g_new_location, folder_name);
	ui::dim_text("Se creará en: %s", full.c_str());
	if (!g_new_error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::DANGER);
		ImGui::TextWrapped("%s", g_new_error.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();
	const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Cancelar", ImVec2(bw, 0))) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	const bool valid = strlen(g_new_name) > 0 && strlen(g_new_location) > 0;
	ImGui::BeginDisabled(!valid);
	if (ui::accent_button("Crear y abrir", ImVec2(bw, 0)) || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
		std::string error;
		if (Project::create(full, g_new_name, g_new_3d, error)) {
			ImGui::CloseCurrentPopup();
			p_app.open_project(full);
		} else {
			g_new_error = error;
		}
	}
	ImGui::EndDisabled();
	ImGui::EndPopup();
}

static void draw_import_popup(App &p_app) {
	const float s = p_app.scale();
	ImGui::SetNextWindowSize(ImVec2(560 * s, 0));
	if (!ImGui::BeginPopupModal("Importar proyecto", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
		return;
	}
	ui::section_title("Importar proyecto");
	ui::dim_text("Elige la carpeta de un proyecto de IPDE (la que contiene project.ipde).");
	ImGui::Spacing();
	ImGui::SetNextItemWidth(-110 * s);
	ImGui::InputText("##import", g_import_path, sizeof(g_import_path));
	ImGui::SameLine();
	if (ImGui::Button("Examinar...", ImVec2(-1, 0))) {
		const std::string picked = platform::pick_folder("Carpeta del proyecto");
		if (!picked.empty()) {
			strncpy(g_import_path, picked.c_str(), sizeof(g_import_path) - 1);
		}
	}
	if (!g_import_error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::DANGER);
		ImGui::TextWrapped("%s", g_import_error.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();
	const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Cancelar", ImVec2(bw, 0))) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ui::accent_button("Importar", ImVec2(bw, 0))) {
		Project p;
		if (Project::load(g_import_path, p)) {
			p_app.settings.touch_project(p);
			ImGui::CloseCurrentPopup();
			p_app.toast("Proyecto \"" + p.name + "\" importado.");
		} else {
			g_import_error = "Esa carpeta no contiene un proyecto de IPDE (project.ipde).";
		}
	}
	ImGui::EndPopup();
}

static void draw_projects(App &p_app) {
	const float s = p_app.scale();
	ui::section_title("Proyectos");
	ImGui::SameLine();
	const float buttons_w = 334 * s;
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttons_w);
	if (ImGui::Button("Importar", ImVec2(110 * s, 0))) {
		g_import_error.clear();
		ImGui::OpenPopup("Importar proyecto");
	}
	ImGui::SameLine();
	if (ui::accent_button("+  Nuevo proyecto", ImVec2(buttons_w - 110 * s - ImGui::GetStyle().ItemSpacing.x, 0))) {
		open_new_project_popup(p_app);
	}
	draw_new_project_popup(p_app);
	draw_import_popup(p_app);

	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##search", "Buscar proyectos...", g_search, sizeof(g_search));
	ImGui::Spacing();

	std::vector<RecentProject> &projects = p_app.settings.projects;
	if (projects.empty()) {
		// Empty state.
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float logo = 150 * s;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, avail.y * 0.5f - 200 * s));
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - logo) * 0.5f);
		ImGui::Image(ImTextureRef(p_app.logo_tile), ImVec2(logo, logo));
		ImGui::Spacing();
		const char *title = "Bienvenido a IPDE";
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x) * 0.5f);
		ImGui::TextUnformatted(title);
		ImGui::PopFont();
		const char *sub = "Crea tu primer proyecto para empezar a hacer juegos.";
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(sub).x) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
		ImGui::TextUnformatted(sub);
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 220 * s) * 0.5f);
		if (ui::accent_button("+  Crear proyecto", ImVec2(220 * s, 44 * s))) {
			open_new_project_popup(p_app);
		}
		return;
	}

	// Project cards in a responsive grid.
	std::string filter = g_search;
	std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
	const float card_w_min = 300 * s;
	const float avail_w = ImGui::GetContentRegionAvail().x;
	const int columns = std::max(1, (int)((avail_w + 12 * s) / (card_w_min + 12 * s)));
	const float card_w = (avail_w - (columns - 1) * 12 * s) / columns;
	const float card_h = 118 * s;
	std::string to_open;
	std::string to_remove;
	int col = 0;
	ImGui::BeginChild("ProjectList", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	for (const RecentProject &rp : projects) {
		std::string lname = rp.name;
		std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
		if (!filter.empty() && lname.find(filter) == std::string::npos) {
			continue;
		}
		if (col > 0) {
			ImGui::SameLine(0, 12 * s);
		}
		ImGui::PushID(rp.path.c_str());
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton("card", ImVec2(card_w, card_h));
		const bool hovered = ImGui::IsItemHovered();
		const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(0);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(pos, ImVec2(pos.x + card_w, pos.y + card_h), hovered ? theme::PANEL_HOVER : theme::PANEL, 12 * s);
		dl->AddRect(pos, ImVec2(pos.x + card_w, pos.y + card_h), hovered ? theme::ACCENT : theme::BORDER, 12 * s, 0, 1.0f);
		// Avatar with the initial.
		const ImVec2 av(pos.x + 18 * s, pos.y + 18 * s);
		const float avs = 46 * s;
		dl->AddRectFilled(av, ImVec2(av.x + avs, av.y + avs), project_color(rp.name), 10 * s);
		const char initial[2] = { rp.name.empty() ? '?' : (char)toupper((unsigned char)rp.name[0]), 0 };
		ImFont *font = ImGui::GetFont();
		const float big = ImGui::GetStyle().FontSizeBase * 1.6f;
		const ImVec2 isz = font->CalcTextSizeA(big, FLT_MAX, 0, initial);
		dl->AddText(font, big, ImVec2(av.x + (avs - isz.x) * 0.5f, av.y + (avs - isz.y) * 0.5f), IM_COL32(255, 255, 255, 255), initial);
		// Texts.
		const float tx = av.x + avs + 14 * s;
		const float tw = pos.x + card_w - tx - 14 * s;
		dl->PushClipRect(ImVec2(tx, pos.y), ImVec2(tx + tw, pos.y + card_h), true);
		dl->AddText(font, ImGui::GetStyle().FontSizeBase * 1.15f, ImVec2(tx, av.y), theme::TEXT, rp.name.c_str());
		dl->AddText(ImVec2(tx, av.y + 26 * s), theme::TEXT_DIM, rp.path.c_str());
		dl->PopClipRect();
		const bool missing = !fs::exists(fs::join(rp.path, "project.ipde"));
		const std::string footer = missing ? "Carpeta no encontrada" : "Abierto " + format_date(rp.last_opened);
		dl->AddText(ImVec2(av.x, pos.y + card_h - 32 * s), missing ? theme::DANGER : theme::TEXT_DIM, footer.c_str());
		// Actions.
		ImGui::SetCursorScreenPos(ImVec2(pos.x + card_w - 96 * s, pos.y + card_h - 40 * s));
		if (!missing && ui::accent_button("Abrir", ImVec2(80 * s, 0))) {
			to_open = rp.path;
		}
		ImGui::SetCursorScreenPos(ImVec2(pos.x + card_w - 40 * s, pos.y + 12 * s));
		if (ImGui::Button("...", ImVec2(28 * s, 0))) {
			ImGui::OpenPopup("card_menu");
		}
		if (ImGui::BeginPopup("card_menu")) {
			if (ImGui::MenuItem("Abrir carpeta", nullptr, false, !missing)) {
				platform::open_folder(rp.path);
			}
			if (ImGui::MenuItem("Quitar de la lista")) {
				to_remove = rp.path;
			}
			ImGui::EndPopup();
		}
		if ((clicked && double_clicked) && !missing) {
			to_open = rp.path;
		}
		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + card_h));
		ImGui::Dummy(ImVec2(card_w, 0));
		if (col == 0) {
			// Keep rows aligned.
		}
		ImGui::PopID();
		col = (col + 1) % columns;
		if (col == 0) {
			ImGui::Dummy(ImVec2(0, 4 * s));
		}
	}
	ImGui::EndChild();

	if (!to_remove.empty()) {
		p_app.settings.remove_project(to_remove);
		p_app.settings.save();
	}
	if (!to_open.empty()) {
		p_app.open_project(to_open);
	}
}

static void draw_versions(App &p_app) {
	const float s = p_app.scale();
	Updater &up = p_app.updater;
	ui::section_title("Versiones");
	ui::dim_text("IPDE se actualiza solo. Aquí puedes instalar cualquier versión: stables, devbuilds o versiones anteriores.");
	ImGui::Spacing();

	// Current version card.
	ImGui::BeginChild("Current", ImVec2(0, 96 * s), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::Image(ImTextureRef(p_app.logo_tile), ImVec2(60 * s, 60 * s));
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
	ImGui::Text("%s", IPDE_VERSION_DISPLAY);
	ImGui::PopFont();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
	ImGui::Text("Instalada - canal %s", IPDE_VERSION_CHANNEL);
	ImGui::Text("%s", up.get_status().c_str());
	ImGui::PopStyleColor();
	ImGui::EndGroup();
	ImGui::SameLine(ImGui::GetWindowWidth() - 210 * s);
	ImGui::BeginGroup();
	if (up.get_state() == Updater::State::INSTALLED) {
		if (ui::accent_button("Reiniciar ahora", ImVec2(190 * s, 0))) {
			up.restart_into_new_version();
		}
	} else {
		const Release *newest = up.get_newest_available();
		ImGui::BeginDisabled(up.is_busy());
		if (newest) {
			if (ui::accent_button(("Actualizar a v." + std::to_string(newest->major) + "." + std::to_string(newest->minor) + "." + std::to_string(newest->patch)).c_str(), ImVec2(190 * s, 0))) {
				up.install(*newest, false);
			}
		}
		if (ImGui::Button("Buscar versiones", ImVec2(190 * s, 0))) {
			up.check(false);
		}
		ImGui::EndDisabled();
	}
	ImGui::EndGroup();
	ImGui::EndChild();

	if (up.get_state() == Updater::State::DOWNLOADING) {
		ImGui::ProgressBar(up.get_progress(), ImVec2(-1, 0));
	}

	ImGui::Checkbox("Mostrar devbuilds", &g_show_devbuilds);
	ImGui::SameLine();
	ImGui::Checkbox("Actualizar automáticamente", &p_app.settings.auto_update);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		p_app.settings.save();
	}

	const std::vector<Release> &releases = up.get_releases();
	if (!up.has_checked()) {
		ui::dim_text(up.is_busy() ? "Cargando versiones..." : "Pulsa \"Buscar versiones\" para ver la lista.");
		return;
	}

	const float list_w = ImGui::GetContentRegionAvail().x * 0.55f;
	ImGui::BeginChild("ReleaseList", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
	if (ImGui::BeginTable("releases", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
		ImGui::TableSetupColumn("Versión", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthStretch, 0.9f);
		ImGui::TableSetupColumn("Estado", ImGuiTableColumnFlags_WidthStretch, 1.1f);
		ImGui::TableHeadersRow();
		const int64_t current = Updater::current_sort_key();
		for (int i = 0; i < (int)releases.size(); i++) {
			const Release &r = releases[i];
			if (!r.stable && !g_show_devbuilds && r.sort_key() != current) {
				continue;
			}
			if (r.asset_url.empty() && r.sort_key() != current) {
				continue; // Old versions without a Windows download (IPDE <= 0.2.x was based on Godot).
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (ImGui::Selectable(r.display_name().c_str(), g_selected_release == i, ImGuiSelectableFlags_SpanAllColumns)) {
				g_selected_release = i;
			}
			ImGui::TableNextColumn();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(r.stable ? theme::SUCCESS : theme::WARNING), "%s", r.stable ? "Stable" : "DevBuild");
			ImGui::TableNextColumn();
			if (r.sort_key() == current) {
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme::ACCENT), "Instalada");
			} else if (r.asset_url.empty()) {
				ImGui::TextDisabled("No disponible");
			} else {
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(r.sort_key() > current ? theme::SUCCESS : theme::TEXT_DIM), "%s", r.sort_key() > current ? "Más nueva" : "Anterior");
			}
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("ReleaseDetails", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	if (g_selected_release >= 0 && g_selected_release < (int)releases.size()) {
		const Release &r = releases[g_selected_release];
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
		ImGui::TextUnformatted(r.display_name().c_str());
		ImGui::PopFont();
		ui::dim_text("%s  ·  %s  ·  %s", r.tag.c_str(), r.stable ? "Stable" : "DevBuild", r.date.c_str());
		ImGui::Spacing();
		const int64_t current = Updater::current_sort_key();
		ImGui::BeginDisabled(up.is_busy() || r.asset_url.empty());
		std::string label = r.sort_key() == current ? "Reinstalar" : (r.sort_key() > current ? "Actualizar a esta versión" : "Instalar esta versión");
		if (ui::accent_button(label.c_str(), ImVec2(-1, 0))) {
			up.install(r, false);
		}
		ImGui::EndDisabled();
		if (r.asset_url.empty()) {
			ui::dim_text("Esta versión no tiene descarga para Windows.");
		}
		if (ImGui::Button("Ver en GitHub", ImVec2(-1, 0))) {
			platform::open_url(r.html_url);
		}
		ImGui::Separator();
		ImGui::TextWrapped("%s", r.body.empty() ? "Sin notas." : r.body.c_str());
	} else {
		ui::dim_text("Selecciona una versión para ver sus novedades.");
	}
	ImGui::EndChild();
}

static void draw_settings(App &p_app) {
	const float s = p_app.scale();
	ui::section_title("Ajustes");
	ImGui::Spacing();
	ImGui::BeginChild("SettingsCard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::SeparatorText("Actualizaciones");
	bool changed = false;
	changed |= ImGui::Checkbox("Instalar actualizaciones automáticamente", &p_app.settings.auto_update);
	ui::dim_text("IPDE descarga e instala las versiones nuevas sin preguntar. En el panel de proyectos se reinicia solo; en el editor se aplican al volver a abrir IPDE.");
	changed |= ImGui::Checkbox("Recibir también devbuilds", &p_app.settings.include_devbuilds);
	ui::dim_text("Las devbuilds traen lo último, pero pueden ser inestables. Si tienes una devbuild instalada, siempre recibes la versión más nueva.");
	ImGui::Spacing();
	ImGui::SeparatorText("Interfaz");
	float scale = p_app.settings.ui_scale;
	ImGui::SetNextItemWidth(260 * s);
	if (ImGui::SliderFloat("Tamaño de la interfaz", &scale, 0.75f, 1.75f, "%.2fx")) {
		p_app.settings.ui_scale = scale;
	}
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		p_app.apply_style();
		changed = true;
	}
	ImGui::Spacing();
	ImGui::SeparatorText("Datos");
	if (ImGui::Button("Abrir carpeta de datos de IPDE")) {
		platform::open_folder(platform::get_app_data_dir());
	}
	ImGui::EndChild();
	if (changed) {
		p_app.settings.save();
	}
}

static void draw_about(App &p_app) {
	const float s = p_app.scale();
	ui::section_title("Acerca de");
	ImGui::Spacing();
	ImGui::BeginChild("AboutCard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	const float w = 360 * s;
	const float h = w * p_app.logo_wordmark_size.y / std::max(1.0f, p_app.logo_wordmark_size.x);
	ImGui::Image(ImTextureRef(p_app.logo_wordmark), ImVec2(w, h));
	ImGui::Spacing();
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
	ImGui::Text("%s (%s)", IPDE_VERSION_DISPLAY, IPDE_VERSION_CHANNEL);
	ImGui::PopFont();
	ImGui::TextUnformatted("Motor de videojuegos de IkersStudio.");
	ImGui::Spacing();
	ImGui::SeparatorText("Créditos");
	ImGui::TextUnformatted("© IkersStudio. Licencia MIT.");
	ui::dim_text("Componentes de terceros: Dear ImGui (MIT), stb_image (dominio público), fuente Roboto (Apache 2.0).");
	ImGui::Spacing();
	if (ImGui::Button("Repositorio en GitHub")) {
		platform::open_url("https://github.com/" IPDE_REPOSITORY);
	}
	ImGui::EndChild();
}

void draw_dashboard(App &p_app) {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar(2);

	draw_sidebar(p_app);
	ImGui::SameLine(0, 0);

	const float s = p_app.scale();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::BG);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * s, 26 * s));
	ImGui::BeginChild("Content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::PANEL);
	switch (g_page) {
		case Page::PROJECTS: draw_projects(p_app); break;
		case Page::VERSIONS: draw_versions(p_app); break;
		case Page::SETTINGS: draw_settings(p_app); break;
		case Page::ABOUT: draw_about(p_app); break;
	}
	ImGui::PopStyleColor();
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::End();
}

} // namespace ipde
