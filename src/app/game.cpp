#include "app/game.h"

#include "app/scene3d.h"
#include "core/audio.h"
#include "core/fs.h"
#include "platform/platform.h"
#include "render/soft_renderer.h"
#include "render3d/renderer3d.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <stb/stb_image.h>

#include <algorithm>
#include <map>
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
	if (p_e.has_motion()) {
		const float f = SceneRenderer3D::motion_factor(p_e, p_time);
		r_x += p_e.move_x * f;
		r_y += p_e.move_y * f;
	}
}

ImTextureID SpriteCache::get(const std::string &p_relative) {
	if (p_relative.empty() || !renderer) {
		return ImTextureID_Invalid;
	}
	auto it = textures.find(p_relative);
	if (it != textures.end()) {
		return it->second;
	}
	ImTextureID id = ImTextureID_Invalid;
	std::string data;
	if (fs::read_file(fs::join(project_dir, p_relative), data)) {
		int w = 0, h = 0, n = 0;
		unsigned char *px = stbi_load_from_memory((const unsigned char *)data.data(), (int)data.size(), &w, &h, &n, 4);
		if (px) {
			id = renderer->create_texture(px, w, h);
			stbi_image_free(px);
		}
	}
	textures[p_relative] = id;
	return id;
}

void SpriteCache::clear() {
	for (auto &it : textures) {
		if (it.second != ImTextureID_Invalid && renderer) {
			renderer->destroy_texture(it.second);
		}
	}
	textures.clear();
}

void draw_scene(ImDrawList *p_dl, const Scene &p_scene, ImVec2 p_origin, float p_zoom, ImFont *p_font, double p_time, bool p_animate, SpriteCache *p_sprites) {
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
				const ImTextureID tex = p_sprites ? p_sprites->get(e.texture) : ImTextureID_Invalid;
				if (tex != ImTextureID_Invalid) {
					// Sprite: flipped horizontally with a negative width.
					const float u0 = e.flip_x ? 1.0f : 0.0f;
					p_dl->AddImageQuad(ImTextureRef(tex), corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), ImVec2(u0, 0), ImVec2(1 - u0, 0), ImVec2(1 - u0, 1), ImVec2(u0, 1), color);
				} else {
					p_dl->AddQuadFilled(corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh), color);
				}
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

static bool is_solid_2d(const Entity &p_e) {
	return p_e.visible && p_e.solid && !p_e.player && !p_e.collectible && !p_e.goal && p_e.chase_speed == 0.0f && p_e.type != EntityType::TEXT && p_e.type != EntityType::LIGHT && p_e.spin_speed == 0.0f;
}

// 3D: moving and bobbing objects are solid too (they carry the player).
static bool is_solid_3d(const Entity &p_e) {
	return p_e.visible && p_e.solid && !p_e.player && !p_e.collectible && !p_e.goal && p_e.chase_speed == 0.0f && p_e.type != EntityType::LIGHT && p_e.spin_speed == 0.0f;
}

struct GameStatus {
	int deaths = 0;
	double goal_time = 0; // When the goal was reached (0 = not yet).
	double level_time = 0; // Seconds it took.
	double death_flash = -10; // Last time the player died.
};

static void draw_banner(ImFont *p_font, const char *p_msg, const char *p_sub, int p_w, float p_y, ImU32 p_color) {
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	const float big = 44.0f;
	const ImVec2 ms = p_font->CalcTextSizeA(big, FLT_MAX, 0, p_msg);
	const ImVec2 ss = p_sub ? p_font->CalcTextSizeA(22.0f, FLT_MAX, 0, p_sub) : ImVec2(0, 0);
	const float bw = std::max(ms.x, ss.x);
	const ImVec2 mp(((float)p_w - bw) * 0.5f, p_y);
	const float bh = ms.y + (p_sub ? ss.y + 6 : 0);
	fg->AddRectFilled(ImVec2(mp.x - 24, mp.y - 14), ImVec2(mp.x + bw + 24, mp.y + bh + 14), IM_COL32(16, 17, 31, 210), 14);
	fg->AddText(p_font, big, ImVec2(((float)p_w - ms.x) * 0.5f, mp.y), p_color, p_msg);
	if (p_sub) {
		fg->AddText(p_font, 22.0f, ImVec2(((float)p_w - ss.x) * 0.5f, mp.y + ms.y + 6), IM_COL32(220, 222, 240, 255), p_sub);
	}
}

static void draw_status(ImFont *p_font, const GameStatus &p_st, double p_now, int p_w, int p_h, bool p_has_next) {
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	if (p_st.deaths > 0) {
		char buf[48];
		snprintf(buf, sizeof(buf), "Caídas: %d", p_st.deaths);
		fg->AddText(p_font, 22.0f, ImVec2(14, (float)p_h - 36), IM_COL32(255, 255, 255, 200), buf);
	}
	if (p_now - p_st.death_flash < 0.35) {
		const int a = (int)(140 * (1.0 - (p_now - p_st.death_flash) / 0.35));
		fg->AddRectFilled(ImVec2(0, 0), ImVec2((float)p_w, (float)p_h), IM_COL32(255, 40, 60, a));
	}
	if (p_st.goal_time > 0) {
		char sub[128];
		if (p_has_next) {
			snprintf(sub, sizeof(sub), "Tiempo: %.1f s   ·   Caídas: %d   ·   Siguiente nivel...", p_st.level_time, p_st.deaths);
		} else {
			snprintf(sub, sizeof(sub), "Tiempo: %.1f s   ·   Caídas: %d   ·   Esc para salir", p_st.level_time, p_st.deaths);
		}
		draw_banner(p_font, p_has_next ? "¡Nivel completado!" : "¡Juego completado!", sub, p_w, (float)p_h * 0.38f, IM_COL32(61, 220, 132, 255));
	}
}

static void draw_hud(ImFont *p_font, int p_score, int p_total, double p_time_all_collected, double p_now, int p_w) {
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	fg->AddText(ImVec2(12, 10), IM_COL32(255, 255, 255, 140), "IPDE  ·  Esc para salir  ·  F3: estadísticas");
	if (p_total > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Monedas: %d / %d", p_score, p_total);
		const float size = 28.0f;
		const ImVec2 ts = p_font->CalcTextSizeA(size, FLT_MAX, 0, buf);
		const ImVec2 pos((float)p_w - ts.x - 24, 16);
		fg->AddRectFilled(ImVec2(pos.x - 14, pos.y - 8), ImVec2(pos.x + ts.x + 14, pos.y + ts.y + 8), IM_COL32(16, 17, 31, 170), 10);
		fg->AddText(p_font, size, pos, IM_COL32(255, 197, 61, 255), buf);
		if (p_score == p_total && p_time_all_collected > 0 && p_now - p_time_all_collected < 4.0) {
			draw_banner(p_font, "¡Has recogido todas las monedas!", nullptr, p_w, 90, IM_COL32(255, 255, 255, 255));
		}
	}
}

/* Audio */

struct GameAudio {
	std::string project_dir;
	bool effects = true;
	std::map<std::string, audio::Sound> cache;

	audio::Sound load(const std::string &p_relative) {
		auto it = cache.find(p_relative);
		if (it != cache.end()) {
			return it->second;
		}
		audio::Sound s = audio::load_wav(fs::join(project_dir, p_relative));
		cache[p_relative] = s;
		return s;
	}

	// Plays the entity's own sound if it has one, otherwise the built-in effect.
	void play(const Entity *p_entity, const audio::Sound &p_builtin, float p_pitch = 1.0f) {
		if (p_entity && !p_entity->sound.empty()) {
			audio::play(load(p_entity->sound));
		} else if (effects && p_builtin) {
			audio::play(p_builtin, 1.0f, p_pitch);
		}
	}
};

static GameAudio g_audio;

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
	GameStatus status;
	int ground = -1; // Entity the player stands on.
	double prev_time = 0;
	std::vector<Vec3> start_positions; // For enemies (reset when the player dies).

	// Clears the gameplay state for a new level (keeps the renderer and its caches).
	void reset() {
		player = -1;
		follow = -1;
		spawn = Vec3();
		velocity_y = 0.0f;
		on_ground = false;
		cam_yaw = 0.0f;
		cam_pitch = 22.0f;
		cam_distance = 7.0f;
		cam_initialized = false;
		score = 0;
		total = 0;
		all_collected_time = 0;
		status = GameStatus();
		ground = -1;
		prev_time = 0;
		start_positions.clear();
	}

	void die(double now) {
		Entity &p = scene.entities[player];
		p.x = spawn.x;
		p.y = spawn.y;
		p.z = spawn.z;
		velocity_y = 0;
		ground = -1;
		status.deaths++;
		status.death_flash = now;
		g_audio.play(nullptr, audio::sfx::hurt());
		for (size_t i = 0; i < scene.entities.size(); i++) {
			Entity &e = scene.entities[i];
			if (e.chase_speed != 0.0f) {
				e.x = start_positions[i].x;
				e.y = start_positions[i].y;
				e.z = start_positions[i].z;
			}
		}
	}

	void init() {
		for (const Entity &e : scene.entities) {
			start_positions.push_back(Vec3(e.x, e.y, e.z));
		}
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
		if (player >= 0 && status.goal_time == 0) {
			Entity &p = scene.entities[player];
			// Enemies walk towards the player when it is near.
			for (Entity &e : scene.entities) {
				if (e.chase_speed == 0.0f || !e.visible) {
					continue;
				}
				Vec3 d(p.x - e.x, 0, p.z - e.z);
				const float dist = d.length();
				if (dist < e.chase_range && dist > 0.05f) {
					d = d * (1.0f / dist);
					e.x += d.x * e.chase_speed * dt;
					e.z += d.z * e.chase_speed * dt;
					e.rot_y = std::atan2(d.x, d.z) / DEG2RAD;
				}
			}
			// Standing on a moving platform carries the player with it.
			if (ground >= 0 && on_ground) {
				const Entity &g = scene.entities[ground];
				const Vec3 delta = SceneRenderer3D::entity_position(g, now, true) - SceneRenderer3D::entity_position(g, prev_time, true);
				p.x += delta.x;
				p.y += delta.y;
				p.z += delta.z;
			}
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
				g_audio.play(nullptr, audio::sfx::jump());
			}
			velocity_y -= 20.0f * dt;
			velocity_y = std::max(velocity_y, -40.0f);

			// Move axis by axis and resolve collisions against static solids.
			Vec3 pos(p.x, p.y, p.z);
			const float moves[3] = { delta.x, velocity_y * dt, delta.z };
			on_ground = false;
			ground = -1;
			float bounce = 0.0f;
			const Entity *bounce_entity = nullptr;
			for (int axis = 0; axis < 3; axis++) {
				if (moves[axis] == 0) {
					continue;
				}
				(axis == 0 ? pos.x : (axis == 1 ? pos.y : pos.z)) += moves[axis];
				Box pb = entity_box(p, pos);
				for (size_t oi = 0; oi < scene.entities.size(); oi++) {
					const Entity &o = scene.entities[oi];
					if (&o == &p || !is_solid_3d(o)) {
						continue;
					}
					const Box ob = entity_box(o, SceneRenderer3D::entity_position(o, now, true));
					if (!pb.overlaps(ob)) {
						continue;
					}
					const float step = ob.max.y - pb.min.y;
					if (axis != 1 && step <= 0.35f && velocity_y <= 1.0f) {
						pos.y += step + 0.001f; // Small ledge: step up onto it.
					} else if (axis == 0) {
						pos.x += moves[0] > 0 ? (ob.min.x - pb.max.x) : (ob.max.x - pb.min.x);
					} else if (axis == 2) {
						pos.z += moves[2] > 0 ? (ob.min.z - pb.max.z) : (ob.max.z - pb.min.z);
					} else {
						if (moves[1] < 0) {
							pos.y += ob.max.y - pb.min.y;
							on_ground = true;
							ground = (int)oi;
							if (o.bounce > bounce) {
								bounce = o.bounce;
								bounce_entity = &o;
							}
						} else {
							pos.y += ob.min.y - pb.max.y;
						}
						velocity_y = 0;
					}
					pb = entity_box(p, pos);
				}
			}
			if (bounce > 0.0f) {
				velocity_y = bounce; // Jump pad.
				on_ground = false;
				g_audio.play(bounce_entity, audio::sfx::bounce());
				ground = -1;
			}
			p.x = pos.x;
			p.y = pos.y;
			p.z = pos.z;
			if (pos.y < -40.0f) {
				die(now);
				pos = spawn;
			}

			// Collectibles.
			const Box pb = entity_box(p, pos);
			for (Entity &o : scene.entities) {
				if (o.collectible && o.visible && pb.overlaps(entity_box(o, SceneRenderer3D::entity_position(o, now, true)))) {
					o.visible = false;
					score++;
					g_audio.play(&o, score == total ? audio::sfx::goal() : audio::sfx::coin());
					if (score == total) {
						all_collected_time = now;
					}
				}
			}
			// Hazards and the goal.
			for (const Entity &o : scene.entities) {
				if (!o.visible || (!o.hazard && !o.goal)) {
					continue;
				}
				if (pb.overlaps(entity_box(o, SceneRenderer3D::entity_position(o, now, true)))) {
					if (o.hazard) {
						die(now);
						break;
					}
					status.goal_time = now;
					status.level_time = now;
					g_audio.play(&o, audio::sfx::goal());
					break;
				}
			}
		}
		prev_time = now;

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

// Bilinear upscale of an 0xAARRGGBB image (used for the dynamic resolution of 3D games).
static void upscale(const uint32_t *p_src, int p_sw, int p_sh, std::vector<uint32_t> &r_dst, int p_dw, int p_dh) {
	r_dst.resize((size_t)p_dw * p_dh);
	if (p_sw == p_dw && p_sh == p_dh) {
		std::copy(p_src, p_src + r_dst.size(), r_dst.begin());
		return;
	}
	const int fx_step = (int)(((int64_t)p_sw << 16) / p_dw);
	const int fy_step = (int)(((int64_t)p_sh << 16) / p_dh);
	for (int y = 0; y < p_dh; y++) {
		int fy = y * fy_step + fy_step / 2 - 32768;
		fy = std::max(0, std::min(fy, (p_sh - 1) << 16));
		const int y0 = fy >> 16;
		const int y1 = std::min(y0 + 1, p_sh - 1);
		const uint32_t wy = (uint32_t)(fy & 0xFFFF) >> 8;
		const uint32_t *r0 = p_src + (size_t)y0 * p_sw;
		const uint32_t *r1 = p_src + (size_t)y1 * p_sw;
		uint32_t *out = r_dst.data() + (size_t)y * p_dw;
		for (int x = 0; x < p_dw; x++) {
			int fx = x * fx_step + fx_step / 2 - 32768;
			fx = std::max(0, std::min(fx, (p_sw - 1) << 16));
			const int x0 = fx >> 16;
			const int x1 = std::min(x0 + 1, p_sw - 1);
			const uint32_t wx = (uint32_t)(fx & 0xFFFF) >> 8;
			const uint32_t a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
			// Two channels at a time (R/B and G).
			const uint32_t rb_top = ((a & 0xFF00FF) * (256 - wx) + (b & 0xFF00FF) * wx) >> 8 & 0xFF00FF;
			const uint32_t rb_bot = ((c & 0xFF00FF) * (256 - wx) + (d & 0xFF00FF) * wx) >> 8 & 0xFF00FF;
			const uint32_t g_top = ((a & 0xFF00) * (256 - wx) + (b & 0xFF00) * wx) >> 8 & 0xFF00;
			const uint32_t g_bot = ((c & 0xFF00) * (256 - wx) + (d & 0xFF00) * wx) >> 8 & 0xFF00;
			const uint32_t rb = (rb_top * (256 - wy) + rb_bot * wy) >> 8 & 0xFF00FF;
			const uint32_t g = (g_top * (256 - wy) + g_bot * wy) >> 8 & 0xFF00;
			out[x] = 0xFF000000 | rb | g;
		}
	}
}

int run_game(const std::string &p_project_dir, const std::string &p_start_level) {
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

	audio::init();
	g_audio.project_dir = project.path;

	SoftRenderer renderer;
	double start = platform::get_time();
	double last = start;

	Game3D game3d;
	game3d.project_dir = project.path;
	// 2D state.
	std::vector<Entity> &ents = scene.entities;
	int score2d = 0;
	int total2d = 0;
	double all2d_time = 0;
	GameStatus status2d;
	std::vector<Entity> initial2d;
	float vel2d_y = 0.0f; // Platformer mode.
	bool on_ground2d = false;
	int ground2d = -1;
	double prev_t2d = 0.0;
	SpriteCache sprites;
	sprites.renderer = &renderer;
	sprites.project_dir = project.path;

	// Levels: the project's scenes are played in order.
	size_t level = 0;
	std::string current_music;
	auto start_level = [&](size_t p_index, Scene &&p_scene) {
		scene = std::move(p_scene);
		level = p_index;
		start = platform::get_time();
		last = start;
		g_audio.effects = scene.sound_effects;
		if (scene.music != current_music) {
			audio::stop_all();
			current_music = scene.music;
			if (!scene.music.empty()) {
				audio::play(g_audio.load(scene.music), scene.music_volume, 1.0f, true);
			}
		}
		game3d.reset();
		if (scene.is_3d) {
			game3d.scene = scene;
			game3d.init();
		}
		score2d = 0;
		total2d = 0;
		all2d_time = 0;
		status2d = GameStatus();
		initial2d = ents;
		vel2d_y = 0.0f;
		on_ground2d = false;
		ground2d = -1;
		prev_t2d = 0.0;
		for (const Entity &e : ents) {
			if (e.collectible && e.visible) {
				total2d++;
			}
		}
	};
	auto next_level_index = [&]() -> size_t { return level + 1; };
	{
		size_t first_index = 0;
		Scene first = scene; // start_level moves into "scene", so pass a copy.
		for (size_t i = 0; i < project.scenes.size(); i++) {
			if (!p_start_level.empty() && project.scenes[i] == p_start_level && Scene::load(project.get_scene_path(project.scenes[i]), first)) {
				first_index = i;
				break;
			}
		}
		start_level(first_index, std::move(first));
	}

	// Dynamic resolution (3D): lowers the internal resolution when frames take too long.
	float render_scale = 1.0f;
	double frame_avg = 1.0 / 60.0;
	bool show_stats = false;
	std::vector<uint32_t> scaled;

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
		// Go to the next level a moment after reaching the goal.
		{
			const GameStatus &st = scene.is_3d ? game3d.status : status2d;
			if (st.goal_time > 0 && t - st.goal_time > 2.5 && next_level_index() < project.scenes.size()) {
				Scene next;
				size_t index = next_level_index();
				while (index < project.scenes.size() && !Scene::load(project.get_scene_path(project.scenes[index]), next)) {
					index++;
				}
				if (index < project.scenes.size()) {
					start_level(index, std::move(next));
					ImGui::EndFrame();
					continue;
				}
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
			show_stats = !show_stats;
		}
		if (scene.is_3d) {
			game3d.update(dt, t);
			draw_hud(font, game3d.score, game3d.total, game3d.all_collected_time, t, w);
			draw_status(font, game3d.status, t, w, h, level + 1 < project.scenes.size());
			if (show_stats) {
				char stats[128];
				snprintf(stats, sizeof(stats), "%.0f FPS  ·  %.1f ms  ·  %d%%  ·  %zu tri", 1.0 / std::max(1e-6, frame_avg), frame_avg * 1000.0, (int)std::lround(render_scale * 100), game3d.renderer.get_triangle_count());
				ImGui::GetForegroundDrawList()->AddText(font, 18.0f, ImVec2(12, 40), IM_COL32(255, 255, 255, 220), stats);
			}
			ImGui::Render();
			const int rw = std::max(16, (int)(w * render_scale));
			const int rh = std::max(16, (int)(h * render_scale));
			const double r0 = platform::get_time();
			game3d.render(rw, rh, t);
			const double frame_ms = platform::get_time() - r0;
			frame_avg = frame_avg * 0.9 + frame_ms * 0.1;
			// Aim for ~60 FPS; adjust the scale slowly to avoid flicker.
			if (frame_avg > 1.0 / 45.0 && render_scale > 0.5f) {
				render_scale = std::max(0.5f, render_scale - 0.02f);
			} else if (frame_avg < 1.0 / 90.0 && render_scale < 1.0f) {
				render_scale = std::min(1.0f, render_scale + 0.01f);
			}
			renderer.resize(w, h);
			upscale(game3d.renderer.get_pixels(), rw, rh, scaled, w, h);
			renderer.set_pixels(scaled.data());
			renderer.render(ImGui::GetDrawData());
			platform::present(renderer.get_pixels(), w, h);
		} else {
			// Player movement with simple collisions against solid shapes.
			// Top-down: WASD moves freely. Platformer: A/D walk, gravity pulls down, Space/W jumps.
			float mx = 0, my = 0;
			if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
				mx -= 1;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
				mx += 1;
			}
			if (!scene.platformer) {
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
			}
			const bool jump_pressed = ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_W, false) || ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
			for (Entity &p : ents) {
				if (!p.player || status2d.goal_time > 0) {
					continue;
				}
				// Enemies.
				for (Entity &e : ents) {
					if (e.chase_speed == 0.0f || !e.visible) {
						continue;
					}
					const float dx = p.x - e.x, dy = scene.platformer ? 0.0f : p.y - e.y;
					const float dist = std::sqrt(dx * dx + dy * dy);
					if (dist < e.chase_range && dist > 0.5f) {
						e.x += dx / dist * e.chase_speed * dt;
						e.y += dy / dist * e.chase_speed * dt;
					}
				}
				if (mx != 0) {
					p.flip_x = mx < 0; // Face where it walks.
				}
				float dy_move = my * p.player_speed * dt;
				if (scene.platformer) {
					// Ride moving platforms.
					if (on_ground2d && ground2d >= 0 && ground2d < (int)ents.size()) {
						float ax, ay, ar, bx, by, br;
						entity_animated_transform(ents[ground2d], t, true, ax, ay, ar);
						entity_animated_transform(ents[ground2d], prev_t2d, true, bx, by, br);
						p.x += ax - bx;
						p.y += ay - by;
					}
					if (on_ground2d && jump_pressed) {
						vel2d_y = -p.jump_force * 110.0f;
						on_ground2d = false;
						g_audio.play(nullptr, audio::sfx::jump());
					}
					vel2d_y = std::min(vel2d_y + scene.gravity_2d * dt, 2400.0f);
					dy_move = vel2d_y * dt;
				}
				// Moving solids that ran into the player push it out along the shortest way.
				{
					Box pb = entity_box(p, Vec3(p.x, p.y, 0));
					for (const Entity &o : ents) {
						if (&o == &p || !is_solid_2d(o) || (!o.has_motion() && o.bob_amplitude == 0.0f)) {
							continue;
						}
						float ox, oy, orot;
						entity_animated_transform(o, t, true, ox, oy, orot);
						const Box ob = entity_box(o, Vec3(ox, oy, 0));
						if (!pb.overlaps(ob)) {
							continue;
						}
						const float push[4] = { ob.min.x - pb.max.x, ob.max.x - pb.min.x, ob.min.y - pb.max.y, ob.max.y - pb.min.y };
						int best = 0;
						for (int k = 1; k < 4; k++) {
							if (k == 3 && scene.platformer && on_ground2d) {
								continue; // Standing on the floor: can't be pushed down, so go sideways.
							}
							if (std::fabs(push[k]) < std::fabs(push[best])) {
								best = k;
							}
						}
						(best < 2 ? p.x : p.y) += push[best];
						pb = entity_box(p, Vec3(p.x, p.y, 0));
					}
				}
				bool landed = false;
				int landed_on = -1;
				for (int axis = 0; axis < 2; axis++) {
					const float delta = axis == 0 ? mx * p.player_speed * dt : dy_move;
					if (delta == 0) {
						continue;
					}
					(axis == 0 ? p.x : p.y) += delta;
					Box pb = entity_box(p, Vec3(p.x, p.y, 0));
					for (size_t oi = 0; oi < ents.size(); oi++) {
						const Entity &o = ents[oi];
						if (&o == &p || !is_solid_2d(o)) {
							continue;
						}
						float ox, oy, orot;
						entity_animated_transform(o, t, true, ox, oy, orot);
						const Box ob = entity_box(o, Vec3(ox, oy, 0));
						if (pb.overlaps(ob)) {
							if (axis == 0) {
								p.x += delta > 0 ? (ob.min.x - pb.max.x) : (ob.max.x - pb.min.x);
							} else {
								p.y += delta > 0 ? (ob.min.y - pb.max.y) : (ob.max.y - pb.min.y);
								if (scene.platformer) {
									if (delta > 0) {
										landed = true;
										landed_on = (int)oi;
										if (o.bounce > 0.0f) {
											vel2d_y = -o.bounce * 110.0f; // Jump pad.
											landed = false;
											g_audio.play(&o, audio::sfx::bounce());
										} else {
											vel2d_y = 0;
										}
									} else {
										vel2d_y = 0; // Bumped the head.
									}
								}
							}
							pb = entity_box(p, Vec3(p.x, p.y, 0));
						}
					}
				}
				if (scene.platformer) {
					on_ground2d = landed;
					ground2d = landed_on;
				}
				prev_t2d = t;
				const bool fell = scene.platformer && p.y > 5000.0f; // Fell off the world.
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
						g_audio.play(&o, score2d == total2d ? audio::sfx::goal() : audio::sfx::coin());
						if (score2d == total2d) {
							all2d_time = t;
						}
					}
				}
				// Hazards and the goal.
				bool died = false;
				for (const Entity &o : ents) {
					if (!o.visible || (!o.hazard && !o.goal) || &o == &p) {
						continue;
					}
					float ox, oy, orot;
					entity_animated_transform(o, t, true, ox, oy, orot);
					if (pb.overlaps(entity_box(o, Vec3(ox, oy, 0)))) {
						if (o.hazard) {
							died = true;
							break;
						}
						status2d.goal_time = t;
						status2d.level_time = t;
						g_audio.play(&o, audio::sfx::goal());
						break;
					}
				}
				if (died || fell) {
					vel2d_y = 0;
					on_ground2d = false;
					status2d.deaths++;
					status2d.death_flash = t;
					g_audio.play(nullptr, audio::sfx::hurt());
					for (size_t i = 0; i < ents.size(); i++) {
						if (ents[i].player || ents[i].chase_speed != 0.0f) {
							ents[i].x = initial2d[i].x;
							ents[i].y = initial2d[i].y;
						}
					}
					break;
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
			draw_scene(ImGui::GetBackgroundDrawList(), scene, origin, 1.0f, font, t, true, &sprites);
			draw_hud(font, score2d, total2d, all2d_time, t, w);
			draw_status(font, status2d, t, w, h, level + 1 < project.scenes.size());
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

	audio::shutdown();
	sprites.clear();
	renderer.shutdown();
	ImGui::DestroyContext();
	platform::destroy_window();
	return 0;
}

} // namespace ipde
