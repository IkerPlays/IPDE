#pragma once

// IPDE software renderer: draws Dear ImGui draw lists into a CPU framebuffer.
// It doesn't depend on any GPU API, so the editor runs on every Windows PC.

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ipde {

struct SoftTexture {
	int width = 0;
	int height = 0;
	std::vector<uint32_t> pixels; // 0xAARRGGBB, straight alpha.
};

class SoftRenderer {
public:
	void resize(int p_width, int p_height);
	void clear(uint32_t p_color);
	// Copies a full frame (width * height 0xAARRGGBB pixels) into the framebuffer.
	void set_pixels(const uint32_t *p_pixels) { std::copy(p_pixels, p_pixels + framebuffer.size(), framebuffer.begin()); }
	void render(ImDrawData *p_draw_data);

	const uint32_t *get_pixels() const { return framebuffer.data(); }
	int get_width() const { return width; }
	int get_height() const { return height; }

	// Textures for images (e.g. the logo). p_rgba is width * height * 4 bytes (R, G, B, A).
	ImTextureID create_texture(const uint8_t *p_rgba, int p_width, int p_height);
	void destroy_texture(ImTextureID p_texture);
	// Creates or updates a texture from 0xAARRGGBB pixels (used to show the 3D viewport).
	ImTextureID update_texture(ImTextureID p_texture, const uint32_t *p_argb, int p_width, int p_height);

	void shutdown();

private:
	int width = 0;
	int height = 0;
	std::vector<uint32_t> framebuffer;

	void _update_texture(ImTextureData *p_tex);
	void _draw_triangle(const ImDrawVert &p_v0, const ImDrawVert &p_v1, const ImDrawVert &p_v2, const SoftTexture *p_tex, int p_clip_x0, int p_clip_y0, int p_clip_x1, int p_clip_y1);
	void _fill_rect(int p_x0, int p_y0, int p_x1, int p_y1, uint32_t p_color, const SoftTexture *p_tex, float p_u, float p_v);
};

} // namespace ipde
