#pragma once

// Conversion of generated maps to their output encodings and writing them to disk.
// The GUI preview uses the same encoders, so what you see is what gets exported.

#include "md/core/Material.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace md {

/// A map converted to its file encoding.
struct EncodedMap {
    int width = 0;
    int height = 0;
    int channels = 0;               ///< 1 (grey) or 3 (RGB)
    int bitDepth = 8;               ///< 8 or 16
    std::vector<std::uint8_t> data8;  ///< bitDepth == 8
    std::vector<std::uint16_t> data16; ///< bitDepth == 16
    std::string format;             ///< e.g. "png-rgb8-srgb", "png-gray16-linear"
};

/// Albedo -> 8-bit sRGB RGB; normal -> 8-bit RGB; height -> 16-bit grey; others -> 8-bit grey (linear).
EncodedMap encodeMap(const MaterialMaps& maps, MapType map);

/// 8-bit RGBA pixels for display (16-bit maps are reduced to 8 bits).
std::vector<std::uint8_t> toRgba8(const EncodedMap& encoded);

/// "<material>_<seed>"
std::string outputBaseName(const MaterialSettings& settings);
/// "<dir>/<material>_<seed>_<map>.png"
std::filesystem::path mapFilePath(const std::filesystem::path& dir, const MaterialSettings& settings, MapType map);
/// "<dir>/<material>_<seed>_settings.json"
std::filesystem::path settingsFilePath(const std::filesystem::path& dir, const MaterialSettings& settings);

struct ExportedFile {
    MapType map;
    std::filesystem::path path;
    std::string format;
};

struct ExportResult {
    std::vector<ExportedFile> files;
    std::filesystem::path settingsFile;
    std::vector<std::string> warnings;
};

/// Writes the maps selected in settings.common.maps plus the settings JSON into `dir`
/// (created if needed). Throws md::Error(Io) on failure.
ExportResult exportMaps(const MaterialMaps& maps, const MaterialSettings& settings, const std::filesystem::path& dir);

} // namespace md
