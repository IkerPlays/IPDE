#pragma once

// Small image helpers: PNG writing (uncompressed deflate) and box downscaling.

#include <cstdint>
#include <string>
#include <vector>

namespace ipde::image {

// Writes 0xAARRGGBB pixels as an RGBA PNG.
bool write_png(const std::string &p_path, const uint32_t *p_argb, int p_width, int p_height);

// Resizes 0xAARRGGBB pixels to p_dst_w x p_dst_h averaging the source pixels (for thumbnails).
std::vector<uint32_t> downscale(const uint32_t *p_src, int p_src_w, int p_src_h, int p_src_stride, int p_dst_w, int p_dst_h);

} // namespace ipde::image
