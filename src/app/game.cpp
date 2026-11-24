#include "app/game.h"

#include "core/fs.h"
#include "platform/platform.h"
#include "render/soft_renderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

extern const uint8_t res_font_roboto[];
extern const size_t res_font_roboto_size;

namespace ipde {

static ImU32 to_imgui_color(uint32_t p_argb) {
	return IM_COL32((p_argb >> 16) & 0xFF, (p_argb >> 8) & 0xFF, p_argb & 0xFF, (p_argb >> 24) & 0xFF);
}

static void entity_animated_transform(const Entity &p_e, double p_time, bool p_animate, float &r_x, float &r_y, float &r_rotation) {
	r_x = p_e.x;
	r_y = p_e.y;
	r_rotation = p_e.rotation;
	if (!p_animate) {
		return;
	}
	r_rotation += (float)(p_e.spin_speed * p_time);
	if (p_e.bob_amplitude != 0.0f) {
		r_y += p_e.bob_amplitude * (float)std::sin(p_time * p_e.bob_frequency * 6.283185307);
	}
}

void draw_scene(ImDrawList *p_dl, const Scene &p_scene, ImVec2 p_origin, float p_zoom, ImFont *p_font, double p_time, bool p_animate) {
	for (const Entity &e : p_scene.entities) {
		if (!e.visible) {
			continue;
		}
		float x, y, rot;
		entity_animated_transform(e, p_time, p_animate, x, y, rot);
		const ImVec2 c(p_origin.x + x * p_zoom, p_origin.y + y * p_zoom);
		const ImU32 color = to_imgui_color(e.color);
		switch (e.type) {
			case EntityType::RECT: {
				const float hw = e.width * 0.5f * p_zoom;
				const float hh = e.height * 0.5f * p_zoom;
				const float r = rot * 0.0174532925f;
				const float cs = std::cos(r);
				const float sn = std::sin(r);
				auto corner = [&](float px, float py) { return ImVec2(c.x + px * cs - py * sn, c.y + px * sn + py * cs); };
				p_dl->AddQuadFilled(corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), color);
			} break;
			case EntityType::CIRCLE:
				p_dl->AddCircleFilled(c, std::max(1.0f, e.width * 0.5f * p_zoom), color);
				break;
			case EntityType::TEXT: {
				const float size = std::max(4.0f, e.text_size * p_zoom);
				const ImVec2 ts = p_font->CalcTextSizeA(size, FLT_MAX, 0.0f, e.text.c_str());
				p_dl->AddText(p_font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), color, e.text.c_str());
			} break;
		}
	}
}

// Axis-aligned bounds of an entity (rotation ignored), used for collisions.
static void entity_bounds(const Entity &p_e, float p_x, float p_y, float &r_x0, float &r_y0, float &r_x1, float &r_y1) {
	const float hw = p_e.width * 0.5f;
	const float hh = (p_e.type == EntityType::CIRCLE ? p_e.width : p_e.height) * 0.5f;
	r_x0 = p_x - hw;
	r_x1 = p_x + hw;
	r_y0 = p_y - hh;
	r_y1 = p_y + hh;
}

int run_game(const std::string &p_project_dir) {
	Project project;
	Scene scene;
	if (!Project::load(p_project_dir, project) || !Scene::load(project.get_scene_file(), scene)) {
		return 2;
	}
	if (!platform::create_window(project.name.c_str(), 1280, 720)) {
		return 1;
	}
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	ImFontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	ImFont *font = io.Fonts->AddFontFromMemoryTTF((void *)res_font_roboto, (int)res_font_roboto_size, 20.0f, &cfg);

	ImGui::GetStyle().AntiAliasedLinesUseTex = false;
	SoftRenderer renderer;
	const double start = platform::get_time();
	double last = start;
	std::vector<Entity> &ents = scene.entities;

	while (platform::pump_events(0.0)) {
		const double now = platform::get_time();
		const float dt = (float)std::min(0.05, now - last);
		last = now;

		int w, h;
		platform::get_window_size(w, h);
		if (w <= 0 || h <= 0) {
			platform::pump_events(0.05);
			continue;
		}
		io.DisplaySize = ImVec2((float)w, (float)h);
		io.DeltaTime = std::max(1e-4f, dt);
		ImGui::NewFrame();

		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			break;
		}

		// Player movement with simple collisions against solid shapes.
		float mx = 0, my = 0;
		if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
			mx -= 1;
		}
		if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
			mx += 1;
		}
		if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
			my -= 1;
		}
		if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
			my += 1;
		}
		const float len = std::sqrt(mx * mx + my * my);
		if (len > 0) {
			mx /= len;
			my /= len;
		}
		for (Entity &p : ents) {
			if (!p.player) {
				continue;
			}
			for (int axis = 0; axis < 2; axis++) {
				const float delta = (axis == 0 ? mx : my) * p.player_speed * dt;
				if (delta == 0) {
					continue;
				}
				(axis == 0 ? p.x : p.y) += delta;
				float px0, py0, px1, py1;
				entity_bounds(p, p.x, p.y, px0, py0, px1, py1);
				for (const Entity &o : ents) {
					if (&o == &p || o.player || o.type == EntityType::TEXT || !o.visible || o.spin_speed != 0 || o.bob_amplitude != 0) {
						continue;
					}
					float ox0, oy0, ox1, oy1;
					entity_bounds(o, o.x, o.y, ox0, oy0, ox1, oy1);
					if (px0 < ox1 && px1 > ox0 && py0 < oy1 && py1 > oy0) {
						if (axis == 0) {
							p.x += delta > 0 ? (ox0 - px1) : (ox1 - px0);
						} else {
							p.y += delta > 0 ? (oy0 - py1) : (oy1 - py0);
						}
						entity_bounds(p, p.x, p.y, px0, py0, px1, py1);
					}
				}
			}
		}

		// Camera: centered on the first entity with camera_follow, or on the world origin.
		ImVec2 origin((float)w * 0.5f, (float)h * 0.5f);
		for (const Entity &e : ents) {
			if (e.camera_follow) {
				origin = ImVec2((float)w * 0.5f - e.x, (float)h * 0.5f - e.y);
				break;
			}
		}

		ImDrawList *dl = ImGui::GetBackgroundDrawList();
		draw_scene(dl, scene, origin, 1.0f, font, now - start, true);
		ImGui::GetForegroundDrawList()->AddText(ImVec2(12, 10), IM_COL32(255, 255, 255, 110), "IPDE  ·  Esc para salir");

		ImGui::Render();
		renderer.resize(w, h);
		renderer.clear(scene.background);
		renderer.render(ImGui::GetDrawData());
		platform::present(renderer.get_pixels(), w, h);

		// Cap at ~60 FPS.
		const double frame_time = platform::get_time() - now;
		if (frame_time < 1.0 / 60.0) {
			platform::pump_events(1.0 / 60.0 - frame_time);
		}
	}

	renderer.shutdown();
	ImGui::DestroyContext();
	platform::destroy_window();
	return 0;
}

} // namespace ipde
