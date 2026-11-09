#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace md {

/// Writes an 8-bit PNG (1 = grey, 3 = RGB, 4 = RGBA channels). Throws md::Error(Io) on failure.
void writePng8(const std::filesystem::path& path, int width, int height, int channels, const std::uint8_t* pixels);

/// Writes a 16-bit greyscale PNG. stb_image_write only supports 8-bit PNGs, so this uses a small
/// built-in encoder (PNG chunks + CRC) on top of stb's zlib compressor.
void writePng16Gray(const std::filesystem::path& path, int width, int height, const std::uint16_t* pixels);

/// Encodes a 16-bit greyscale PNG into memory (exposed for tests).
std::vector<std::uint8_t> encodePng16Gray(int width, int height, const std::uint16_t* pixels);

/// Writes raw bytes to a file. Throws md::Error(Io) on failure.
void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

} // namespace md
