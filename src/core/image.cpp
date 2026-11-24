#include "core/image.h"

#include "core/fs.h"

#include <algorithm>

namespace ipde::image {

namespace {

uint32_t crc32(const unsigned char *p_data, size_t p_size, uint32_t p_crc = 0) {
	static uint32_t table[256];
	static bool ready = false;
	if (!ready) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) {
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			table[i] = c;
		}
		ready = true;
	}
	uint32_t c = p_crc ^ 0xFFFFFFFFu;
	for (size_t i = 0; i < p_size; i++) {
		c = table[(c ^ p_data[i]) & 0xFF] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}

void put32(std::string &r_out, uint32_t p_v) {
	r_out.push_back((char)(p_v >> 24));
	r_out.push_back((char)(p_v >> 16));
	r_out.push_back((char)(p_v >> 8));
	r_out.push_back((char)p_v);
}

void chunk(std::string &r_out, const char *p_type, const std::string &p_data) {
	put32(r_out, (uint32_t)p_data.size());
	std::string body(p_type, 4);
	body += p_data;
	r_out += body;
	put32(r_out, crc32((const unsigned char *)body.data(), body.size()));
}

} // namespace

bool write_png(const std::string &p_path, const uint32_t *p_argb, int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return false;
	}
	// Raw scanlines: filter byte 0 + RGBA.
	std::string raw;
	raw.reserve((size_t)(p_width * 4 + 1) * p_height);
	for (int y = 0; y < p_height; y++) {
		raw.push_back(0);
		for (int x = 0; x < p_width; x++) {
			const uint32_t c = p_argb[(size_t)y * p_width + x];
			raw.push_back((char)((c >> 16) & 0xFF));
			raw.push_back((char)((c >> 8) & 0xFF));
			raw.push_back((char)(c & 0xFF));
			raw.push_back((char)((c >> 24) & 0xFF));
		}
	}
	// zlib stream with stored (uncompressed) deflate blocks.
	std::string z;
	z.push_back((char)0x78);
	z.push_back((char)0x01);
	size_t pos = 0;
	do {
		const size_t len = std::min<size_t>(65535, raw.size() - pos);
		const bool last = pos + len >= raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back((char)(len & 0xFF));
		z.push_back((char)(len >> 8));
		z.push_back((char)(~len & 0xFF));
		z.push_back((char)((~len >> 8) & 0xFF));
		z.append(raw, pos, len);
		pos += len;
	} while (pos < raw.size());
	uint32_t a = 1, b = 0;
	for (unsigned char ch : raw) {
		a = (a + ch) % 65521;
		b = (b + a) % 65521;
	}
	put32(z, (b << 16) | a);

	std::string png("\x89PNG\r\n\x1a\n", 8);
	std::string ihdr;
	put32(ihdr, (uint32_t)p_width);
	put32(ihdr, (uint32_t)p_height);
	ihdr += std::string("\x08\x06\x00\x00\x00", 5); // 8-bit RGBA.
	chunk(png, "IHDR", ihdr);
	chunk(png, "IDAT", z);
	chunk(png, "IEND", std::string());
	return fs::write_file(p_path, png);
}

std::vector<uint32_t> downscale(const uint32_t *p_src, int p_src_w, int p_src_h, int p_src_stride, int p_dst_w, int p_dst_h) {
	std::vector<uint32_t> out((size_t)p_dst_w * p_dst_h);
	for (int y = 0; y < p_dst_h; y++) {
		const int sy0 = y * p_src_h / p_dst_h;
		const int sy1 = std::max(sy0 + 1, (y + 1) * p_src_h / p_dst_h);
		for (int x = 0; x < p_dst_w; x++) {
			const int sx0 = x * p_src_w / p_dst_w;
			const int sx1 = std::max(sx0 + 1, (x + 1) * p_src_w / p_dst_w);
			uint32_t r = 0, g = 0, bl = 0, n = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					const uint32_t c = p_src[(size_t)sy * p_src_stride + sx];
					r += (c >> 16) & 0xFF;
					g += (c >> 8) & 0xFF;
					bl += c & 0xFF;
					n++;
				}
			}
			n = std::max(1u, n);
			out[(size_t)y * p_dst_w + x] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (bl / n);
		}
	}
	return out;
}

} // namespace ipde::image
