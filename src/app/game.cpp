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
#include <cstdio>
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

/* Gamepad helpers */

static float pad_value(ImGuiKey p_key) {
	return ImGui::GetKeyData(p_key)->AnalogValue;
}

static bool pad_pressed(ImGuiKey p_key) {
	return ImGui::IsKeyPressed(p_key, false);
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
	bool game_over = false; // No lives left.
};

// Lives (hearts) and the level timer, top left under the help line.
static void draw_rules_hud(ImFont *p_font, const Scene &p_scene, int p_deaths, double p_time, bool p_frozen) {
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	float y = 40;
	if (p_scene.lives > 0) {
		const int left = std::max(0, p_scene.lives - p_deaths);
		for (int i = 0; i < p_scene.lives; i++) {
			const float cx = 26.0f + i * 30.0f, cy = y + 10;
			const ImU32 c = i < left ? IM_COL32(255, 77, 94, 255) : IM_COL32(255, 255, 255, 60);
			// Heart: two circles and a triangle.
			fg->AddCircleFilled(ImVec2(cx - 5, cy - 3), 6.5f, c, 16);
			fg->AddCircleFilled(ImVec2(cx + 5, cy - 3), 6.5f, c, 16);
			fg->AddTriangleFilled(ImVec2(cx - 11.3f, cy - 0.5f), ImVec2(cx + 11.3f, cy - 0.5f), ImVec2(cx, cy + 12), c);
		}
		y += 34;
	}
	if (p_scene.show_timer) {
		char buf[48];
		snprintf(buf, sizeof(buf), "%d:%04.1f", (int)(p_time / 60.0), std::fmod(p_time, 60.0));
		fg->AddText(p_font, 28.0f, ImVec2(15, y + 1), IM_COL32(0, 0, 0, 120), buf);
		fg->AddText(p_font, 28.0f, ImVec2(14, y), p_frozen ? IM_COL32(61, 220, 132, 255) : IM_COL32(255, 255, 255, 230), buf);
	}
}

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
	if (p_st.game_over) {
		draw_banner(p_font, "Fin del juego", "Te has quedado sin vidas   ·   R para intentarlo otra vez", p_w, (float)p_h * 0.38f, IM_COL32(255, 77, 94, 255));
		return;
	}
	if (p_st.goal_time > 0) {
		char sub[128];
		if (p_has_next) {
			snprintf(sub, sizeof(sub), "Tiempo: %.1f s   ·   Caídas: %d   ·   Siguiente nivel...", p_st.level_time, p_st.deaths);
		} else {
			snprintf(sub, sizeof(sub), "Tiempo: %.1f s   ·   Caídas: %d   ·   R para jugar otra vez", p_st.level_time, p_st.deaths);
		}
		draw_banner(p_font, p_has_next ? "¡Nivel completado!" : "¡Juego completado!", sub, p_w, (float)p_h * 0.38f, IM_COL32(61, 220, 132, 255));
	}
}

static void draw_hud(ImFont *p_font, int p_score, int p_total, double p_time_all_collected, double p_now, int p_w) {
	ImDrawList *fg = ImGui::GetForegroundDrawList();
	fg->AddText(ImVec2(12, 10), IM_COL32(255, 255, 255, 140), "Esc: pausa  ·  R: reiniciar  ·  F11: pantalla completa");
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

/* Particles (bursts when collecting, dying, bouncing...) */

struct Particles {
	struct P {
		Vec3 pos, vel;
		float life, max_life, size, gravity;
		uint32_t color;
	};
	std::vector<P> list;
	uint32_t seed = 0x1234567u;

	float rnd() { // -1..1
		seed = seed * 1664525u + 1013904223u;
		return ((seed >> 8) & 0xFFFF) / 32767.5f - 1.0f;
	}

	// p_flat: 2D (no Z spread). Units are meters in 3D and pixels in 2D.
	void burst(const Vec3 &p_pos, uint32_t p_color, int p_count, float p_speed, float p_size, float p_life, float p_gravity, bool p_flat, float p_up = 0.0f) {
		for (int i = 0; i < p_count && list.size() < 2000; i++) {
			P p;
			p.pos = p_pos;
			Vec3 d(rnd(), rnd(), p_flat ? 0.0f : rnd());
			if (d.length_squared() < 1e-4f) {
				d = Vec3(0, 1, 0);
			}
			p.vel = d.normalized() * (p_speed * (0.4f + 0.6f * std::fabs(rnd()))) + Vec3(0, p_up, 0);
			p.max_life = p.life = p_life * (0.6f + 0.4f * std::fabs(rnd()));
			p.size = p_size * (0.6f + 0.4f * std::fabs(rnd()));
			p.gravity = p_gravity;
			p.color = p_color;
			list.push_back(p);
		}
	}

	void update(float p_dt) {
		for (P &p : list) {
			p.vel.y += p.gravity * p_dt;
			p.pos += p.vel * p_dt;
			p.life -= p_dt;
		}
		list.erase(std::remove_if(list.begin(), list.end(), [](const P &p) { return p.life <= 0.0f; }), list.end());
	}

	static uint32_t faded(const P &p) {
		const float k = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
		const uint32_t a = (uint32_t)(((p.color >> 24) & 0xFF) * std::min(1.0f, k * 1.5f));
		return (a << 24) | (p.color & 0xFFFFFF);
	}
};

static Particles g_fx;

static void fx3d_burst(const Vec3 &p_pos, uint32_t p_color, int p_count, float p_speed, float p_up = 0.0f) {
	g_fx.burst(p_pos, p_color | 0xFF000000, p_count, p_speed, 0.07f, 0.7f, -7.0f, false, p_up);
}

static void fx2d_burst(float p_x, float p_y, uint32_t p_color, int p_count, float p_speed, float p_up = 0.0f) {
	g_fx.burst(Vec3(p_x, p_y, 0), p_color | 0xFF000000, p_count, p_speed, 4.0f, 0.7f, 900.0f, true, p_up);
}

// Hides the doors once every coin has been collected. p_burst makes particles at a door.
template <typename Burst>
static void open_doors(std::vector<Entity> &r_entities, Burst p_burst) {
	for (Entity &d : r_entities) {
		if (d.door && d.visible) {
			d.visible = false;
			p_burst(d);
		}
	}
}

// Shows/hides the entities a switch points to.
static void toggle_targets(std::vector<Entity> &r_entities, const std::string &p_name) {
	for (Entity &e : r_entities) {
		if (e.name == p_name) {
			e.visible = !e.visible;
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
		fx3d_burst(Vec3(p.x, p.y, p.z), 0xFF4D5E, 36, 5.0f, 2.0f);
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
		if (scene.camera_mode == 1) {
			cam_pitch = 5.0f;
		}
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
			// Gamepad: left stick or D-pad.
			mx += pad_value(ImGuiKey_GamepadLStickRight) - pad_value(ImGuiKey_GamepadLStickLeft) + (ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft) ? 1.0f : 0.0f);
			mz += pad_value(ImGuiKey_GamepadLStickDown) - pad_value(ImGuiKey_GamepadLStickUp) + (ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) ? 1.0f : 0.0f);
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
			if (p.can_jump && on_ground && (ImGui::IsKeyPressed(ImGuiKey_Space, false) || pad_pressed(ImGuiKey_GamepadFaceDown))) {
				velocity_y = p.jump_force;
				on_ground = false;
				g_audio.play(nullptr, audio::sfx::jump());
				g_fx.burst(Vec3(p.x, p.y - p.height * 0.5f, p.z), 0xB0FFFFFF, 10, 1.6f, 0.06f, 0.4f, -1.0f, false);
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
				fx3d_burst(Vec3(pos.x, pos.y - p.height * 0.5f, pos.z), bounce_entity ? bounce_entity->color : 0xFFFFFF, 20, 3.0f, 3.0f);
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
					fx3d_burst(SceneRenderer3D::entity_position(o, now, true), o.color, 26, 3.5f, 1.5f);
					if (score == total) {
						all_collected_time = now;
						open_doors(scene.entities, [&](const Entity &d) { fx3d_burst(SceneRenderer3D::entity_position(d, now, true), d.color, 40, 4.0f, 1.0f); });
					}
				}
			}
			// Hazards, checkpoints, switches and the goal.
			for (Entity &o : scene.entities) {
				if (!o.visible || (!o.hazard && !o.goal && !o.checkpoint && o.switch_target.empty())) {
					continue;
				}
				// Slightly taller box so standing on a plate (switch, checkpoint) counts as touching it.
				Box touch = pb;
				touch.min.y -= 0.06f;
				if (touch.overlaps(entity_box(o, SceneRenderer3D::entity_position(o, now, true)))) {
					if (o.hazard) {
						die(now);
						break;
					}
					if (o.checkpoint || !o.switch_target.empty()) {
						if (!o.triggered) {
							o.triggered = true;
							const Vec3 op = SceneRenderer3D::entity_position(o, now, true);
							if (o.checkpoint) {
								spawn = Vec3(p.x, p.y + 0.2f, p.z);
								o.emission = std::max(o.emission, 1.2f); // Lights up when reached.
								fx3d_burst(op, 0x5AC8FF, 30, 3.0f, 3.0f);
								g_audio.play(&o, audio::sfx::coin(), 0.75f);
							} else {
								toggle_targets(scene.entities, o.switch_target);
								fx3d_burst(op, o.color, 20, 2.5f, 1.0f);
								g_audio.play(&o, audio::sfx::click());
							}
						}
						continue;
					}
					status.goal_time = now;
					status.level_time = now;
					g_audio.play(&o, audio::sfx::goal());
					fx3d_burst(SceneRenderer3D::entity_position(o, now, true), 0x3DDC84, 60, 6.0f, 5.0f);
					fx3d_burst(SceneRenderer3D::entity_position(o, now, true), 0xFFC53D, 40, 5.0f, 5.0f);
					break;
				}
			}
		}
		prev_time = now;

		// Camera.
		const int mode = follow >= 0 ? scene.camera_mode : 0;
		if (mode != 2) {
			// Gamepad: right stick turns the camera.
			cam_yaw -= (pad_value(ImGuiKey_GamepadRStickRight) - pad_value(ImGuiKey_GamepadRStickLeft)) * 140.0f * dt;
			const float plo = mode == 1 ? -80.0f : 5.0f, phi = mode == 1 ? 80.0f : 75.0f;
			cam_pitch = std::clamp(cam_pitch + (pad_value(ImGuiKey_GamepadRStickDown) - pad_value(ImGuiKey_GamepadRStickUp)) * 90.0f * dt, plo, phi);
			if (ImGui::IsKeyDown(ImGuiKey_Q)) {
				cam_yaw += 90.0f * dt;
			}
			if (ImGui::IsKeyDown(ImGuiKey_E)) {
				cam_yaw -= 90.0f * dt;
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
				cam_yaw -= ImGui::GetIO().MouseDelta.x * 0.3f;
				const float lo = mode == 1 ? -80.0f : 5.0f, hi = mode == 1 ? 80.0f : 75.0f;
				cam_pitch = std::clamp(cam_pitch + ImGui::GetIO().MouseDelta.y * 0.2f, lo, hi);
			}
		}
		cam_distance = std::clamp(cam_distance - ImGui::GetIO().MouseWheel * 0.8f, 2.5f, 30.0f);
		Vec3 desired_pos, desired_target;
		bool snap = false;
		if (follow >= 0) {
			const Entity &f = scene.entities[follow];
			const Vec3 fp(f.x, f.y, f.z);
			const float yaw = cam_yaw * DEG2RAD, pitch = cam_pitch * DEG2RAD;
			if (mode == 1) {
				// First person: at eye height, looking where the mouse points.
				desired_pos = fp + Vec3(0, f.height * 0.35f, 0);
				desired_target = desired_pos + Vec3(-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
				snap = true;
			} else if (mode == 2) {
				// Top-down: high above, slightly behind.
				desired_target = fp;
				desired_pos = fp + Vec3(0, cam_distance * 1.6f, cam_distance * 0.75f);
				cam_yaw = 0.0f;
			} else {
				desired_target = fp + Vec3(0, std::max(0.5f, f.height * 0.4f), 0);
				const Vec3 dir(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
				// Don't go through walls: stop the camera before the first solid between it and the player.
				float dist = cam_distance;
				for (const Entity &o : scene.entities) {
					if (&o == &f || !is_solid_3d(o) || o.has_motion()) {
						continue;
					}
					const Box b = entity_box(o, Vec3(o.x, o.y, o.z));
					float tmin = 0.0f, tmax = dist;
					bool hit = true;
					for (int k = 0; k < 3 && hit; k++) {
						const float o0 = k == 0 ? desired_target.x : (k == 1 ? desired_target.y : desired_target.z);
						const float d0 = k == 0 ? dir.x : (k == 1 ? dir.y : dir.z);
						const float lo = k == 0 ? b.min.x : (k == 1 ? b.min.y : b.min.z);
						const float hi = k == 0 ? b.max.x : (k == 1 ? b.max.y : b.max.z);
						if (std::fabs(d0) < 1e-6f) {
							hit = o0 >= lo && o0 <= hi;
						} else {
							float t1 = (lo - o0) / d0, t2 = (hi - o0) / d0;
							if (t1 > t2) {
								std::swap(t1, t2);
							}
							tmin = std::max(tmin, t1);
							tmax = std::min(tmax, t2);
							hit = tmin <= tmax;
						}
					}
					if (hit && tmin > 0.0f) {
						dist = std::min(dist, std::max(1.1f, tmin - 0.3f));
					}
				}
				desired_pos = desired_target + dir * dist;
			}
		} else {
			desired_pos = Vec3(scene.camera_x, scene.camera_y, scene.camera_z);
			desired_target = Vec3(scene.camera_target_x, scene.camera_target_y, scene.camera_target_z);
		}
		if (!cam_initialized || snap) {
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
		// In first person the player's own body would block the view.
		const bool hide_player = scene.camera_mode == 1 && follow >= 0 && follow == player;
		if (hide_player) {
			scene.entities[player].visible = false;
		}
		scene_renderer.draw(renderer, scene, project_dir, t, true, false);
		if (hide_player) {
			scene.entities[player].visible = true;
		}
		for (const Particles::P &p : g_fx.list) {
			renderer.draw_point(p.pos, p.size * (0.4f + 0.6f * p.life / p.max_life), Particles::faded(p));
		}
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

int bench_3d(const std::string &p_project_dir) {
	Project project;
	Scene scene;
	if (!Project::load(p_project_dir, project) || !Scene::load(project.get_scene_file(), scene) || !scene.is_3d) {
		fprintf(stderr, "Not a 3D project.\n");
		return 2;
	}
	Game3D g;
	g.scene = scene;
	g.project_dir = project.path;
	g.init();
	ImGui::CreateContext(); // update() reads the keyboard state.
	ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
	ImGui::GetIO().Fonts->Build();
	ImGui::NewFrame();
	g.update(0.016f, 0.0);
	const int frames = 30;
	g.render(1280, 720, 0.0); // Warm up (mesh caches).
	const double t0 = platform::get_time();
	for (int i = 0; i < frames; i++) {
		g.render(1280, 720, i * 0.016);
	}
	const double ms = (platform::get_time() - t0) * 1000.0 / frames;
	printf("%.2f ms/frame (%.0f FPS), %zu triangles\n", ms, 1000.0 / ms, g.renderer.get_triangle_count());
	ImGui::EndFrame();
	ImGui::DestroyContext();
	return 0;
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

	if (project.start_fullscreen) {
		platform::set_fullscreen(true);
	}
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
	bool checkpoint2d = false;
	float checkpoint2d_x = 0.0f, checkpoint2d_y = 0.0f;
	bool on_ground2d = false;
	int ground2d = -1;
	double prev_t2d = 0.0;
	SpriteCache sprites;
	sprites.renderer = &renderer;
	sprites.project_dir = project.path;

	bool paused = false;
	double pause_started = 0.0;

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
		g_fx.list.clear();
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
		checkpoint2d = false;
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

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || pad_pressed(ImGuiKey_GamepadStart)) {
			paused = !paused;
			if (paused) {
				pause_started = now;
			} else {
				start += now - pause_started; // The game clock doesn't run while paused.
			}
		}
		bool restart = !paused && (ImGui::IsKeyPressed(ImGuiKey_R, false) || pad_pressed(ImGuiKey_GamepadBack));
		bool quit = false;
		if (paused) {
			// Pause menu.
			ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(320, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 20));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(16, 17, 31, 235));
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(44, 47, 80, 255));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(108, 123, 255, 255));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 104, 230, 255));
			ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			const char *title = "Pausa";
			ImGui::PushFont(font, 34.0f);
			ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x) * 0.5f);
			ImGui::TextUnformatted(title);
			ImGui::PopFont();
			if (project.scenes.size() > 1) {
				char lv[64];
				snprintf(lv, sizeof(lv), "Nivel %zu de %zu", level + 1, project.scenes.size());
				ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(lv).x) * 0.5f);
				ImGui::TextDisabled("%s", lv);
			}
			ImGui::Spacing();
			if (ImGui::Button("Continuar", ImVec2(-1, 44))) {
				paused = false;
				start += now - pause_started;
			}
			if (ImGui::Button("Reiniciar nivel", ImVec2(-1, 44))) {
				restart = true;
			}
			if (ImGui::Button(platform::is_fullscreen() ? "Modo ventana" : "Pantalla completa", ImVec2(-1, 44))) {
				platform::set_fullscreen(!platform::is_fullscreen());
			}
			if (ImGui::Button("Salir", ImVec2(-1, 44))) {
				quit = true;
			}
			ImGui::End();
			ImGui::PopStyleColor(4);
			ImGui::PopStyleVar(4);
		}
		if (quit) {
			ImGui::EndFrame();
			break;
		}
		if (restart) {
			Scene again;
			if (Scene::load(project.get_scene_path(project.scenes[level]), again)) {
				paused = false;
				start_level(level, std::move(again));
				ImGui::EndFrame();
				continue;
			}
		}
		const double t = now - start;
		g_fx.update(paused ? 0.0f : dt);
		// "Nivel N" title for a moment when a level starts (only in games with several levels).
		auto draw_level_intro = [&]() {
			if (project.scenes.size() < 2 || t > 1.8) {
				return;
			}
			char title[32];
			snprintf(title, sizeof(title), "Nivel %zu", level + 1);
			const float a = (float)std::clamp((1.8 - t) / 0.5, 0.0, 1.0);
			const float size = 64.0f;
			const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, title);
			const ImVec2 p(((float)w - ts.x) * 0.5f, (float)h * 0.3f);
			ImDrawList *fg = ImGui::GetForegroundDrawList();
			fg->AddText(font, size, ImVec2(p.x + 3, p.y + 3), IM_COL32(0, 0, 0, (int)(120 * a)), title);
			fg->AddText(font, size, p, IM_COL32(255, 255, 255, (int)(255 * a)), title);
		};
		// Go to the next level a moment after reaching the goal.
		{
			const GameStatus &st = scene.is_3d ? game3d.status : status2d;
			if (!paused && st.goal_time > 0 && t - st.goal_time > 2.5 && next_level_index() < project.scenes.size()) {
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

		if (ImGui::IsKeyPressed(ImGuiKey_F11, false) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
			platform::set_fullscreen(!platform::is_fullscreen());
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
			show_stats = !show_stats;
		}
		if (scene.is_3d) {
			if (!paused && !game3d.status.game_over) {
				game3d.update(dt, t);
				if (scene.lives > 0 && game3d.status.deaths >= scene.lives) {
					game3d.status.game_over = true;
				}
			}
			draw_rules_hud(font, scene, game3d.status.deaths, game3d.status.goal_time > 0 ? game3d.status.level_time : t, game3d.status.goal_time > 0);
			draw_hud(font, game3d.score, game3d.total, game3d.all_collected_time, t, w);
			draw_status(font, game3d.status, t, w, h, level + 1 < project.scenes.size());
			draw_level_intro();
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
			if (!paused && !status2d.game_over) {
			// Player movement with simple collisions against solid shapes.
			// Top-down: WASD moves freely. Platformer: A/D walk, gravity pulls down, Space/W jumps.
			float mx = 0, my = 0;
			if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
				mx -= 1;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
				mx += 1;
			}
			mx += pad_value(ImGuiKey_GamepadLStickRight) - pad_value(ImGuiKey_GamepadLStickLeft) + (ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft) ? 1.0f : 0.0f);
			mx = std::clamp(mx, -1.0f, 1.0f);
			if (!scene.platformer) {
				if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
					my -= 1;
				}
				if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
					my += 1;
				}
				my += pad_value(ImGuiKey_GamepadLStickDown) - pad_value(ImGuiKey_GamepadLStickUp) + (ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp) ? 1.0f : 0.0f);
				const float len = std::sqrt(mx * mx + my * my);
				if (len > 1.0f) {
					mx /= len;
					my /= len;
				}
			}
			const bool jump_pressed = ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_W, false) || ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) || pad_pressed(ImGuiKey_GamepadFaceDown);
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
						g_fx.burst(Vec3(p.x, p.y + p.height * 0.5f, 0), 0xB0FFFFFF, 8, 120.0f, 3.0f, 0.35f, 300.0f, true);
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
											fx2d_burst(p.x, p.y + p.height * 0.5f, o.color, 18, 220.0f, -200.0f);
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
						fx2d_burst(ox, oy, o.color, 24, 260.0f, -150.0f);
						if (score2d == total2d) {
							all2d_time = t;
							open_doors(ents, [&](const Entity &d) {
								float dx, dy, dr;
								entity_animated_transform(d, t, true, dx, dy, dr);
								fx2d_burst(dx, dy, d.color, 40, 260.0f);
							});
						}
					}
				}
				// Hazards and the goal.
				bool died = false;
				for (Entity &o : ents) {
					if (!o.visible || (!o.hazard && !o.goal && !o.checkpoint && o.switch_target.empty()) || &o == &p) {
						continue;
					}
					float ox, oy, orot;
					entity_animated_transform(o, t, true, ox, oy, orot);
					Box touch = pb; // A bit taller so standing on a plate counts as touching it.
					touch.max.y += 2.0f;
					if (touch.overlaps(entity_box(o, Vec3(ox, oy, 0)))) {
						if (o.hazard) {
							died = true;
							break;
						}
						if (o.checkpoint || !o.switch_target.empty()) {
							if (!o.triggered) {
								o.triggered = true;
								if (o.checkpoint) {
									checkpoint2d = true;
									checkpoint2d_x = p.x;
									checkpoint2d_y = p.y;
									fx2d_burst(ox, oy, 0x5AC8FF, 30, 240.0f, -150.0f);
									g_audio.play(&o, audio::sfx::coin(), 0.75f);
								} else {
									toggle_targets(ents, o.switch_target);
									fx2d_burst(ox, oy, o.color, 20, 200.0f);
									g_audio.play(&o, audio::sfx::click());
								}
							}
							continue;
						}
						status2d.goal_time = t;
						status2d.level_time = t;
						g_audio.play(&o, audio::sfx::goal());
						fx2d_burst(ox, oy, 0x3DDC84, 50, 380.0f, -300.0f);
						fx2d_burst(ox, oy, 0xFFC53D, 30, 320.0f, -300.0f);
						break;
					}
				}
				if (died || fell) {
					vel2d_y = 0;
					on_ground2d = false;
					status2d.deaths++;
					status2d.death_flash = t;
					g_audio.play(nullptr, audio::sfx::hurt());
					fx2d_burst(p.x, std::min(p.y, 4000.0f), 0xFF4D5E, 30, 300.0f, -150.0f);
					for (size_t i = 0; i < ents.size(); i++) {
						if (ents[i].player || ents[i].chase_speed != 0.0f) {
							ents[i].x = initial2d[i].x;
							ents[i].y = initial2d[i].y;
						}
					}
					if (checkpoint2d) {
						p.x = checkpoint2d_x;
						p.y = checkpoint2d_y;
					}
					break;
				}
			}

			} // !paused
			// Camera: centered on the first entity with camera_follow, or on the world origin.
			ImVec2 origin((float)w * 0.5f, (float)h * 0.5f);
			for (const Entity &e : ents) {
				if (e.camera_follow) {
					origin = ImVec2((float)w * 0.5f - e.x, (float)h * 0.5f - e.y);
					break;
				}
			}
			draw_scene(ImGui::GetBackgroundDrawList(), scene, origin, 1.0f, font, t, true, &sprites);
			for (const Particles::P &fp : g_fx.list) {
				const uint32_t c = Particles::faded(fp);
				ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(origin.x + fp.pos.x, origin.y + fp.pos.y), fp.size * (0.4f + 0.6f * fp.life / fp.max_life), to_imgui_color(c), 8);
			}
			draw_hud(font, score2d, total2d, all2d_time, t, w);
			if (scene.lives > 0 && status2d.deaths >= scene.lives) {
				status2d.game_over = true;
			}
			draw_rules_hud(font, scene, status2d.deaths, status2d.goal_time > 0 ? status2d.level_time : t, status2d.goal_time > 0);
			draw_status(font, status2d, t, w, h, level + 1 < project.scenes.size());
			draw_level_intro();
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
