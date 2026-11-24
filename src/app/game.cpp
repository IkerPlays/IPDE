#include "app/game.h"

#include "app/scene3d.h"
#include "core/fs.h"
#include "platform/platform.h"
#include "render/soft_renderer.h"
#include "render3d/renderer3d.h"

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
		if (!e.visible || entity_type_is_3d(e.type)) {
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
			default:
				break;
		}
	}
}

/* Shared helpers */

struct Box {
	Vec3 min, max;
	bool overlaps(const Box &o) const {
		return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
	}
};

// Axis-aligned box of an entity (rotation ignored; good enough for gameplay).
static Box entity_box(const Entity &p_e, const Vec3 &p_pos) {
	Vec3 half;
	if (entity_type_is_3d(p_e.type)) {
		if (p_e.type == EntityType::SPHERE) {
			half = Vec3(p_e.width, p_e.width, p_e.width) * 0.5f;
		} else {
			half = Vec3(p_e.width, std::max(p_e.height, 0.1f), p_e.depth) * 0.5f;
			// Rotated by 90 degrees around X (e.g. coins): swap Y and Z.
			if (std::fabs(std::fmod(std::fabs(p_e.rot_x), 180.0f) - 90.0f) < 1.0f) {
				std::swap(half.y, half.z);
			}
		}
	} else {
		half = Vec3(p_e.width * 0.5f, (p_e.type == EntityType::CIRCLE ? p_e.width : p_e.height) * 0.5f, 0.5f);
	}
	return { p_pos - half, p_pos + half };
}

static bool is_static_solid(const Entity &p_e) {
	return p_e.visible && p_e.solid && !p_e.player && !p_e.collectible && p_e.type != EntityType::TEXT && p_e.type != EntityType::LIGHT && p_e.spin_speed == 0.0f && p_e.bob_amplitude == 0.0f;
}

static void draw_hud(ImFont *p_font, int p_score, int p_total, double p_time_all_collected, double p_now, int p_w) {
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	fg->AddText(ImVec2(12, 10), IM_COL32(255, 255, 255, 140), "IPDE  ·  Esc para salir");
	if (p_total > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Monedas: %d / %d", p_score, p_total);
		const float size = 28.0f;
		const ImVec2 ts = p_font->CalcTextSizeA(size, FLT_MAX, 0, buf);
		const ImVec2 pos((float)p_w - ts.x - 24, 16);
		fg->AddRectFilled(ImVec2(pos.x - 14, pos.y - 8), ImVec2(pos.x + ts.x + 14, pos.y + ts.y + 8), IM_COL32(16, 17, 31, 170), 10);
		fg->AddText(p_font, size, pos, IM_COL32(255, 197, 61, 255), buf);
		if (p_score == p_total && p_time_all_collected > 0 && p_now - p_time_all_collected < 4.0) {
			const char *msg = "¡Has recogido todas las monedas!";
			const float big = 44.0f;
			const ImVec2 ms = p_font->CalcTextSizeA(big, FLT_MAX, 0, msg);
			const ImVec2 mp(((float)p_w - ms.x) * 0.5f, 90);
			fg->AddRectFilled(ImVec2(mp.x - 24, mp.y - 14), ImVec2(mp.x + ms.x + 24, mp.y + ms.y + 14), IM_COL32(16, 17, 31, 200), 14);
			fg->AddText(p_font, big, mp, IM_COL32(255, 255, 255, 255), msg);
		}
	}
}

/* 3D game */

struct Game3D {
	Scene scene;
	std::string project_dir;
	Renderer3D renderer;
	SceneRenderer3D scene_renderer;
	int player = -1;
	int follow = -1;
	Vec3 spawn;
	float velocity_y = 0.0f;
	bool on_ground = false;
	float cam_yaw = 0.0f; // Third-person camera angle around the player.
	float cam_pitch = 22.0f;
	float cam_distance = 7.0f;
	Vec3 cam_pos, cam_target;
	bool cam_initialized = false;
	int score = 0;
	int total = 0;
	double all_collected_time = 0;

	void init() {
		for (size_t i = 0; i < scene.entities.size(); i++) {
			const Entity &e = scene.entities[i];
			if (e.player && player < 0) {
				player = (int)i;
			}
			if (e.camera_follow && follow < 0) {
				follow = (int)i;
			}
			if (e.collectible && e.visible) {
				total++;
			}
		}
		if (player >= 0) {
			const Entity &p = scene.entities[player];
			spawn = Vec3(p.x, p.y, p.z);
			cam_yaw = 0.0f;
		}
	}

	void update(float dt, double now) {
		if (player >= 0) {
			Entity &p = scene.entities[player];
			// Movement relative to the camera.
			float mx = 0, mz = 0;
			if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
				mx -= 1;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
				mx += 1;
			}
			if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
				mz -= 1;
			}
			if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
				mz += 1;
			}
			const float yaw = (follow >= 0 ? cam_yaw : std::atan2(scene.camera_x - scene.camera_target_x, scene.camera_z - scene.camera_target_z) / DEG2RAD) * DEG2RAD;
			const Vec3 forward(-std::sin(yaw), 0, -std::cos(yaw));
			const Vec3 right(std::cos(yaw), 0, -std::sin(yaw));
			Vec3 move = right * mx + forward * -mz;
			if (move.length_squared() > 0) {
				move = move.normalized();
				p.rot_y = std::atan2(move.x, move.z) / DEG2RAD;
			}
			const Vec3 delta = move * (p.player_speed * dt);

			// Gravity and jumping.
			if (p.can_jump && on_ground && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
				velocity_y = p.jump_force;
				on_ground = false;
			}
			velocity_y -= 20.0f * dt;
			velocity_y = std::max(velocity_y, -40.0f);

			// Move axis by axis and resolve collisions against static solids.
			Vec3 pos(p.x, p.y, p.z);
			const float moves[3] = { delta.x, velocity_y * dt, delta.z };
			on_ground = false;
			for (int axis = 0; axis < 3; axis++) {
				if (moves[axis] == 0) {
					continue;
				}
				(axis == 0 ? pos.x : (axis == 1 ? pos.y : pos.z)) += moves[axis];
				Box pb = entity_box(p, pos);
				for (const Entity &o : scene.entities) {
					if (&o == &p || !is_static_solid(o)) {
						continue;
					}
					const Box ob = entity_box(o, Vec3(o.x, o.y, o.z));
					if (!pb.overlaps(ob)) {
						continue;
					}
					if (axis == 0) {
						pos.x += moves[0] > 0 ? (ob.min.x - pb.max.x) : (ob.max.x - pb.min.x);
					} else if (axis == 2) {
						pos.z += moves[2] > 0 ? (ob.min.z - pb.max.z) : (ob.max.z - pb.min.z);
					} else {
						if (moves[1] < 0) {
							pos.y += ob.max.y - pb.min.y;
							on_ground = true;
						} else {
							pos.y += ob.min.y - pb.max.y;
						}
						velocity_y = 0;
					}
					pb = entity_box(p, pos);
				}
			}
			if (pos.y < -40.0f) {
				pos = spawn;
				velocity_y = 0;
			}
			p.x = pos.x;
			p.y = pos.y;
			p.z = pos.z;

			// Collectibles.
			const Box pb = entity_box(p, pos);
			for (Entity &o : scene.entities) {
				if (o.collectible && o.visible && pb.overlaps(entity_box(o, SceneRenderer3D::entity_position(o, now, true)))) {
					o.visible = false;
					score++;
					if (score == total) {
						all_collected_time = now;
					}
				}
			}
		}

		// Camera.
		if (ImGui::IsKeyDown(ImGuiKey_Q)) {
			cam_yaw += 90.0f * dt;
		}
		if (ImGui::IsKeyDown(ImGuiKey_E)) {
			cam_yaw -= 90.0f * dt;
		}
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			cam_yaw -= ImGui::GetIO().MouseDelta.x * 0.3f;
			cam_pitch = std::clamp(cam_pitch + ImGui::GetIO().MouseDelta.y * 0.2f, 5.0f, 75.0f);
		}
		cam_distance = std::clamp(cam_distance - ImGui::GetIO().MouseWheel * 0.8f, 2.5f, 30.0f);
		Vec3 desired_pos, desired_target;
		if (follow >= 0) {
			const Entity &f = scene.entities[follow];
			const Vec3 fp(f.x, f.y, f.z);
			const float yaw = cam_yaw * DEG2RAD, pitch = cam_pitch * DEG2RAD;
			desired_target = fp + Vec3(0, std::max(0.5f, f.height * 0.4f), 0);
			desired_pos = desired_target + Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * cam_distance;
		} else {
			desired_pos = Vec3(scene.camera_x, scene.camera_y, scene.camera_z);
			desired_target = Vec3(scene.camera_target_x, scene.camera_target_y, scene.camera_target_z);
		}
		if (!cam_initialized) {
			cam_pos = desired_pos;
			cam_target = desired_target;
			cam_initialized = true;
		} else {
			const float k = 1.0f - std::exp(-10.0f * dt);
			cam_pos = lerp(cam_pos, desired_pos, k);
			cam_target = lerp(cam_target, desired_target, k);
		}
	}

	void render(int w, int h, double t) {
		renderer.resize(w, h);
		Camera3D cam;
		cam.position = cam_pos;
		cam.target = cam_target;
		cam.fov = scene.camera_fov;
		renderer.begin(cam, SceneRenderer3D::make_environment(scene), SceneRenderer3D::make_lights(scene, t, true));
		scene_renderer.draw(renderer, scene, project_dir, t, true, false);
		renderer.end();
	}
};

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

	Game3D game3d;
	if (scene.is_3d) {
		game3d.scene = scene;
		game3d.project_dir = project.path;
		game3d.init();
	}
	// 2D state.
	std::vector<Entity> &ents = scene.entities;
	int score2d = 0;
	int total2d = 0;
	double all2d_time = 0;
	for (const Entity &e : ents) {
		if (e.collectible && e.visible) {
			total2d++;
		}
	}

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
		const double t = now - start;

		if (scene.is_3d) {
			game3d.update(dt, t);
			draw_hud(font, game3d.score, game3d.total, game3d.all_collected_time, t, w);
			ImGui::Render();
			game3d.render(w, h, t);
			renderer.resize(w, h);
			renderer.set_pixels(game3d.renderer.get_pixels());
			renderer.render(ImGui::GetDrawData());
			platform::present(renderer.get_pixels(), w, h);
		} else {
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
					Box pb = entity_box(p, Vec3(p.x, p.y, 0));
					for (const Entity &o : ents) {
						if (&o == &p || !is_static_solid(o)) {
							continue;
						}
						const Box ob = entity_box(o, Vec3(o.x, o.y, 0));
						if (pb.overlaps(ob)) {
							if (axis == 0) {
								p.x += delta > 0 ? (ob.min.x - pb.max.x) : (ob.max.x - pb.min.x);
							} else {
								p.y += delta > 0 ? (ob.min.y - pb.max.y) : (ob.max.y - pb.min.y);
							}
							pb = entity_box(p, Vec3(p.x, p.y, 0));
						}
					}
				}
				// Collectibles.
				const Box pb = entity_box(p, Vec3(p.x, p.y, 0));
				for (Entity &o : ents) {
					if (!o.collectible || !o.visible) {
						continue;
					}
					float ox, oy, orot;
					entity_animated_transform(o, t, true, ox, oy, orot);
					if (pb.overlaps(entity_box(o, Vec3(ox, oy, 0)))) {
						o.visible = false;
						score2d++;
						if (score2d == total2d) {
							all2d_time = t;
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
			draw_scene(ImGui::GetBackgroundDrawList(), scene, origin, 1.0f, font, t, true);
			draw_hud(font, score2d, total2d, all2d_time, t, w);
			ImGui::Render();
			renderer.resize(w, h);
			renderer.clear(scene.background);
			renderer.render(ImGui::GetDrawData());
			platform::present(renderer.get_pixels(), w, h);
		}

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
