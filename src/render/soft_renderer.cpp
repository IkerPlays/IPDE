#include "render/soft_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ipde {

static inline uint32_t blend(uint32_t p_dst, uint32_t p_r, uint32_t p_g, uint32_t p_b, uint32_t p_a) {
	if (p_a >= 255) {
		return 0xFF000000u | (p_r << 16) | (p_g << 8) | p_b;
	}
	const uint32_t ia = 255 - p_a;
	const uint32_t dr = (p_dst >> 16) & 0xFF;
	const uint32_t dg = (p_dst >> 8) & 0xFF;
	const uint32_t db = p_dst & 0xFF;
	const uint32_t r = (p_r * p_a + dr * ia + 127) / 255;
	const uint32_t g = (p_g * p_a + dg * ia + 127) / 255;
	const uint32_t b = (p_b * p_a + db * ia + 127) / 255;
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline uint32_t sample(const SoftTexture *p_tex, float p_u, float p_v) {
	if (!p_tex || p_tex->width == 0) {
		return 0xFFFFFFFFu;
	}
	int x = (int)(p_u * p_tex->width);
	int y = (int)(p_v * p_tex->height);
	x = x < 0 ? 0 : (x >= p_tex->width ? p_tex->width - 1 : x);
	y = y < 0 ? 0 : (y >= p_tex->height ? p_tex->height - 1 : y);
	return p_tex->pixels[(size_t)y * p_tex->width + x];
}

// Multiplies two 0xAARRGGBB colors channel by channel.
static inline void modulate(uint32_t p_tex, uint32_t p_r, uint32_t p_g, uint32_t p_b, uint32_t p_a, uint32_t &r_r, uint32_t &r_g, uint32_t &r_b, uint32_t &r_a) {
	r_a = (((p_tex >> 24) & 0xFF) * p_a + 127) / 255;
	r_r = (((p_tex >> 16) & 0xFF) * p_r + 127) / 255;
	r_g = (((p_tex >> 8) & 0xFF) * p_g + 127) / 255;
	r_b = ((p_tex & 0xFF) * p_b + 127) / 255;
}

void SoftRenderer::resize(int p_width, int p_height) {
	p_width = std::max(1, p_width);
	p_height = std::max(1, p_height);
	if (p_width == width && p_height == height) {
		return;
	}
	width = p_width;
	height = p_height;
	framebuffer.assign((size_t)width * height, 0xFF000000u);
}

void SoftRenderer::clear(uint32_t p_color) {
	std::fill(framebuffer.begin(), framebuffer.end(), p_color | 0xFF000000u);
}

ImTextureID SoftRenderer::create_texture(const uint8_t *p_rgba, int p_width, int p_height) {
	SoftTexture *tex = new SoftTexture();
	tex->width = p_width;
	tex->height = p_height;
	tex->pixels.resize((size_t)p_width * p_height);
	for (size_t i = 0; i < tex->pixels.size(); i++) {
		const uint8_t *px = p_rgba + i * 4;
		tex->pixels[i] = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
	}
	return (ImTextureID)(uintptr_t)tex;
}

ImTextureID SoftRenderer::update_texture(ImTextureID p_texture, const uint32_t *p_argb, int p_width, int p_height) {
	SoftTexture *tex = (SoftTexture *)(uintptr_t)p_texture;
	if (!tex) {
		tex = new SoftTexture();
	}
	tex->width = p_width;
	tex->height = p_height;
	tex->pixels.assign(p_argb, p_argb + (size_t)p_width * p_height);
	return (ImTextureID)(uintptr_t)tex;
}

void SoftRenderer::destroy_texture(ImTextureID p_texture) {
	delete (SoftTexture *)(uintptr_t)p_texture;
}

void SoftRenderer::_update_texture(ImTextureData *p_tex) {
	if (p_tex->Status == ImTextureStatus_WantDestroy) {
		destroy_texture(p_tex->GetTexID());
		p_tex->SetTexID(ImTextureID_Invalid);
		p_tex->SetStatus(ImTextureStatus_Destroyed);
		return;
	}
	if (p_tex->Status != ImTextureStatus_WantCreate && p_tex->Status != ImTextureStatus_WantUpdates) {
		return;
	}
	SoftTexture *tex = (SoftTexture *)(uintptr_t)p_tex->GetTexID();
	if (!tex || p_tex->Status == ImTextureStatus_WantCreate) {
		tex = new SoftTexture();
		p_tex->SetTexID((ImTextureID)(uintptr_t)tex);
	}
	// Simple approach: (re)convert the whole texture. Font atlases are small.
	tex->width = p_tex->Width;
	tex->height = p_tex->Height;
	tex->pixels.resize((size_t)tex->width * tex->height);
	const uint8_t *src = (const uint8_t *)p_tex->GetPixels();
	if (p_tex->Format == ImTextureFormat_Alpha8) {
		for (size_t i = 0; i < tex->pixels.size(); i++) {
			tex->pixels[i] = ((uint32_t)src[i] << 24) | 0x00FFFFFFu;
		}
	} else {
		for (size_t i = 0; i < tex->pixels.size(); i++) {
			const uint8_t *px = src + i * 4;
			tex->pixels[i] = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
		}
	}
	p_tex->SetStatus(ImTextureStatus_OK);
}

void SoftRenderer::_fill_rect(int p_x0, int p_y0, int p_x1, int p_y1, uint32_t p_color, const SoftTexture *p_tex, float p_u, float p_v) {
	// Fast path for solid axis-aligned rectangles (most of the UI).
	uint32_t r, g, b, a;
	modulate(sample(p_tex, p_u, p_v), (p_color >> 0) & 0xFF, (p_color >> 8) & 0xFF, (p_color >> 16) & 0xFF, (p_color >> 24) & 0xFF, r, g, b, a);
	if (a == 0) {
		return;
	}
	for (int y = p_y0; y < p_y1; y++) {
		uint32_t *row = framebuffer.data() + (size_t)y * width;
		for (int x = p_x0; x < p_x1; x++) {
			row[x] = blend(row[x], r, g, b, a);
		}
	}
}

void SoftRenderer::_draw_triangle(const ImDrawVert &p_v0, const ImDrawVert &p_v1, const ImDrawVert &p_v2, const SoftTexture *p_tex, int p_clip_x0, int p_clip_y0, int p_clip_x1, int p_clip_y1) {
	const ImDrawVert *v0 = &p_v0;
	const ImDrawVert *v1 = &p_v1;
	const ImDrawVert *v2 = &p_v2;
	float area = (v1->pos.x - v0->pos.x) * (v2->pos.y - v0->pos.y) - (v1->pos.y - v0->pos.y) * (v2->pos.x - v0->pos.x);
	if (std::fabs(area) < 1e-6f) {
		return;
	}
	if (area < 0) {
		std::swap(v1, v2);
		area = -area;
	}

	int min_x = (int)std::floor(std::min({ v0->pos.x, v1->pos.x, v2->pos.x }));
	int max_x = (int)std::ceil(std::max({ v0->pos.x, v1->pos.x, v2->pos.x }));
	int min_y = (int)std::floor(std::min({ v0->pos.y, v1->pos.y, v2->pos.y }));
	int max_y = (int)std::ceil(std::max({ v0->pos.y, v1->pos.y, v2->pos.y }));
	min_x = std::max(min_x, p_clip_x0);
	min_y = std::max(min_y, p_clip_y0);
	max_x = std::min(max_x, p_clip_x1);
	max_y = std::min(max_y, p_clip_y1);
	if (min_x >= max_x || min_y >= max_y) {
		return;
	}

	// Edge functions: w0 is opposite v0, etc. Positive inside (counter-clockwise in screen space after the swap).
	struct Edge {
		float a, b, c;
		bool top_left;
	};
	auto make_edge = [](const ImVec2 &p_p, const ImVec2 &p_q) {
		Edge e;
		e.a = p_p.y - p_q.y;
		e.b = p_q.x - p_p.x;
		e.c = p_p.x * p_q.y - p_p.y * p_q.x;
		e.top_left = (e.a > 0) || (e.a == 0 && e.b < 0);
		return e;
	};
	// With area > 0 computed as above, these edges are positive inside.
	Edge e0 = make_edge(v1->pos, v2->pos);
	Edge e1 = make_edge(v2->pos, v0->pos);
	Edge e2 = make_edge(v0->pos, v1->pos);
	// Ensure inside is positive.
	const float test = e0.a * v0->pos.x + e0.b * v0->pos.y + e0.c;
	if (test < 0) {
		e0 = { -e0.a, -e0.b, -e0.c, !e0.top_left };
		e1 = { -e1.a, -e1.b, -e1.c, !e1.top_left };
		e2 = { -e2.a, -e2.b, -e2.c, !e2.top_left };
	}
	const float inv_area = 1.0f / (e0.a * v0->pos.x + e0.b * v0->pos.y + e0.c);

	const bool same_color = v0->col == v1->col && v1->col == v2->col;
	const bool same_uv = v0->uv.x == v1->uv.x && v1->uv.x == v2->uv.x && v0->uv.y == v1->uv.y && v1->uv.y == v2->uv.y;

	auto ch = [](ImU32 c, int shift) { return (float)((c >> shift) & 0xFF); };
	const float c0[4] = { ch(v0->col, 0), ch(v0->col, 8), ch(v0->col, 16), ch(v0->col, 24) };
	const float c1[4] = { ch(v1->col, 0), ch(v1->col, 8), ch(v1->col, 16), ch(v1->col, 24) };
	const float c2[4] = { ch(v2->col, 0), ch(v2->col, 8), ch(v2->col, 16), ch(v2->col, 24) };

	uint32_t const_tex = same_uv ? sample(p_tex, v0->uv.x, v0->uv.y) : 0;

	for (int y = min_y; y < max_y; y++) {
		const float py = (float)y + 0.5f;
		float w0 = e0.a * ((float)min_x + 0.5f) + e0.b * py + e0.c;
		float w1 = e1.a * ((float)min_x + 0.5f) + e1.b * py + e1.c;
		float w2 = e2.a * ((float)min_x + 0.5f) + e2.b * py + e2.c;
		uint32_t *row = framebuffer.data() + (size_t)y * width;
		for (int x = min_x; x < max_x; x++, w0 += e0.a, w1 += e1.a, w2 += e2.a) {
			if (w0 < 0 || w1 < 0 || w2 < 0) {
				continue;
			}
			if ((w0 == 0 && !e0.top_left) || (w1 == 0 && !e1.top_left) || (w2 == 0 && !e2.top_left)) {
				continue;
			}
			const float b0 = w0 * inv_area;
			const float b1 = w1 * inv_area;
			const float b2 = w2 * inv_area;
			uint32_t cr, cg, cb, ca;
			if (same_color) {
				cr = v0->col & 0xFF;
				cg = (v0->col >> 8) & 0xFF;
				cb = (v0->col >> 16) & 0xFF;
				ca = (v0->col >> 24) & 0xFF;
			} else {
				cr = (uint32_t)(c0[0] * b0 + c1[0] * b1 + c2[0] * b2 + 0.5f);
				cg = (uint32_t)(c0[1] * b0 + c1[1] * b1 + c2[1] * b2 + 0.5f);
				cb = (uint32_t)(c0[2] * b0 + c1[2] * b1 + c2[2] * b2 + 0.5f);
				ca = (uint32_t)(c0[3] * b0 + c1[3] * b1 + c2[3] * b2 + 0.5f);
				cr = std::min(cr, 255u);
				cg = std::min(cg, 255u);
				cb = std::min(cb, 255u);
				ca = std::min(ca, 255u);
			}
			const uint32_t texel = same_uv ? const_tex : sample(p_tex, v0->uv.x * b0 + v1->uv.x * b1 + v2->uv.x * b2, v0->uv.y * b0 + v1->uv.y * b1 + v2->uv.y * b2);
			uint32_t r, g, b, a;
			modulate(texel, cr, cg, cb, ca, r, g, b, a);
			if (a) {
				row[x] = blend(row[x], r, g, b, a);
			}
		}
	}
}

void SoftRenderer::render(ImDrawData *p_draw_data) {
	if (p_draw_data->Textures) {
		for (ImTextureData *tex : *p_draw_data->Textures) {
			if (tex->Status != ImTextureStatus_OK) {
				_update_texture(tex);
			}
		}
	}

	const ImVec2 clip_off = p_draw_data->DisplayPos;
	for (int n = 0; n < p_draw_data->CmdListsCount; n++) {
		const ImDrawList *list = p_draw_data->CmdLists[n];
		const ImDrawVert *vtx = list->VtxBuffer.Data;
		const ImDrawIdx *idx = list->IdxBuffer.Data;
		for (const ImDrawCmd &cmd : list->CmdBuffer) {
			if (cmd.UserCallback) {
				if (cmd.UserCallback != ImDrawCallback_ResetRenderState) {
					cmd.UserCallback(list, &cmd);
				}
				continue;
			}
			const int cx0 = std::max(0, (int)(cmd.ClipRect.x - clip_off.x));
			const int cy0 = std::max(0, (int)(cmd.ClipRect.y - clip_off.y));
			const int cx1 = std::min(width, (int)std::ceil(cmd.ClipRect.z - clip_off.x));
			const int cy1 = std::min(height, (int)std::ceil(cmd.ClipRect.w - clip_off.y));
			if (cx0 >= cx1 || cy0 >= cy1) {
				continue;
			}
			const SoftTexture *tex = (const SoftTexture *)(uintptr_t)cmd.GetTexID();
			const ImDrawIdx *ip = idx + cmd.IdxOffset;
			const ImDrawVert *vp = vtx + cmd.VtxOffset;
			for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3) {
				const ImDrawVert &a = vp[ip[i]];
				const ImDrawVert &b = vp[ip[i + 1]];
				const ImDrawVert &c = vp[ip[i + 2]];
				// Fast path: two triangles forming an axis-aligned quad with a single color and texel.
				if (i + 5 < cmd.ElemCount) {
					const ImDrawVert &d = vp[ip[i + 3]];
					const ImDrawVert &e = vp[ip[i + 4]];
					const ImDrawVert &f = vp[ip[i + 5]];
					// ImGui's PrimRect: (a, b, c) and (a, c, d') with a=TL, b=TR, c=BR, d'=BL.
					if (&d == &a && &e == &c && a.col == b.col && b.col == c.col && c.col == f.col &&
							a.uv.x == c.uv.x && a.uv.y == c.uv.y && b.uv.x == a.uv.x && f.uv.x == a.uv.x && b.uv.y == a.uv.y && f.uv.y == a.uv.y &&
							a.pos.y == b.pos.y && b.pos.x == c.pos.x && c.pos.y == f.pos.y && f.pos.x == a.pos.x) {
						const float fx0 = std::min(a.pos.x, c.pos.x) - clip_off.x;
						const float fy0 = std::min(a.pos.y, c.pos.y) - clip_off.y;
						const float fx1 = std::max(a.pos.x, c.pos.x) - clip_off.x;
						const float fy1 = std::max(a.pos.y, c.pos.y) - clip_off.y;
						const int x0 = std::max(cx0, (int)std::ceil(fx0 - 0.5f));
						const int y0 = std::max(cy0, (int)std::ceil(fy0 - 0.5f));
						const int x1 = std::min(cx1, (int)std::ceil(fx1 - 0.5f));
						const int y1 = std::min(cy1, (int)std::ceil(fy1 - 0.5f));
						if (x0 < x1 && y0 < y1) {
							_fill_rect(x0, y0, x1, y1, a.col, tex, a.uv.x, a.uv.y);
						}
						i += 3;
						continue;
					}
				}
				if (clip_off.x != 0 || clip_off.y != 0) {
					ImDrawVert ta = a, tb = b, tc = c;
					ta.pos.x -= clip_off.x;
					ta.pos.y -= clip_off.y;
					tb.pos.x -= clip_off.x;
					tb.pos.y -= clip_off.y;
					tc.pos.x -= clip_off.x;
					tc.pos.y -= clip_off.y;
					_draw_triangle(ta, tb, tc, tex, cx0, cy0, cx1, cy1);
				} else {
					_draw_triangle(a, b, c, tex, cx0, cy0, cx1, cy1);
				}
			}
		}
	}
}

void SoftRenderer::shutdown() {
	for (ImTextureData *tex : ImGui::GetPlatformIO().Textures) {
		if (tex->RefCount == 1 && tex->GetTexID() != ImTextureID_Invalid) {
			destroy_texture(tex->GetTexID());
			tex->SetTexID(ImTextureID_Invalid);
			tex->SetStatus(ImTextureStatus_Destroyed);
		}
	}
}

} // namespace ipde
