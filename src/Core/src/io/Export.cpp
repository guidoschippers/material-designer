#include "md/io/Export.h"

#include "md/core/Error.h"
#include "md/core/Math.h"
#include "md/io/ImageIO.h"
#include "md/io/Preset.h"

#include <cmath>
#include <system_error>

namespace md {

namespace {

std::uint8_t to8(float v)
{
    return static_cast<std::uint8_t>(std::lround(saturate(v) * 255.0f));
}

std::uint16_t to16(float v)
{
    return static_cast<std::uint16_t>(std::lround(saturate(v) * 65535.0f));
}

} // namespace

EncodedMap encodeMap(const MaterialMaps& maps, MapType map)
{
    const ImageF& img = maps.get(map);
    EncodedMap e;
    e.width = img.width();
    e.height = img.height();
    const std::size_t pixels = static_cast<std::size_t>(e.width) * e.height;

    switch (map) {
    case MapType::Albedo:
        e.channels = 3;
        e.format = "png-rgb8-srgb";
        e.data8.resize(pixels * 3);
        for (std::size_t i = 0; i < pixels * 3; ++i) {
            e.data8[i] = to8(linearToSrgb(img.data()[i]));
        }
        break;
    case MapType::Normal:
        e.channels = 3;
        e.format = "png-rgb8-linear";
        e.data8.resize(pixels * 3);
        for (std::size_t i = 0; i < pixels * 3; ++i) {
            e.data8[i] = to8(img.data()[i] * 0.5f + 0.5f);
        }
        break;
    case MapType::Height:
        e.channels = 1;
        e.bitDepth = 16;
        e.format = "png-gray16-linear";
        e.data16.resize(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            e.data16[i] = to16(img.data()[i]);
        }
        break;
    case MapType::Roughness:
    case MapType::AmbientOcclusion:
    case MapType::Wear:
    case MapType::Opacity:
        e.channels = 1;
        e.format = "png-gray8-linear";
        e.data8.resize(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            e.data8[i] = to8(img.data()[i]);
        }
        break;
    }
    return e;
}

std::vector<std::uint8_t> toRgba8(const EncodedMap& e)
{
    const std::size_t pixels = static_cast<std::size_t>(e.width) * e.height;
    std::vector<std::uint8_t> out(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        std::uint8_t r, g, b;
        if (e.bitDepth == 16) {
            r = g = b = static_cast<std::uint8_t>((e.data16[i] + 128u) / 257u);
        } else if (e.channels == 1) {
            r = g = b = e.data8[i];
        } else {
            r = e.data8[i * 3];
            g = e.data8[i * 3 + 1];
            b = e.data8[i * 3 + 2];
        }
        out[i * 4] = r;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = 255;
    }
    return out;
}

std::string outputBaseName(const MaterialSettings& settings)
{
    return settings.material + "_" + std::to_string(settings.common.seed);
}

std::filesystem::path mapFilePath(const std::filesystem::path& dir, const MaterialSettings& settings, MapType map)
{
    return dir / (outputBaseName(settings) + "_" + std::string(mapId(map)) + ".png");
}

std::filesystem::path settingsFilePath(const std::filesystem::path& dir, const MaterialSettings& settings)
{
    return dir / (outputBaseName(settings) + "_settings.json");
}

ExportResult exportMaps(const MaterialMaps& maps, const MaterialSettings& settings, const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        throw Error(ErrorCode::Io, "cannot create output directory '" + dir.string() + "': " + ec.message());
    }

    ExportResult result;
    if (settings.common.maps.contains(MapType::Wear) && !settings.common.wear) {
        result.warnings.push_back("wear map requested but wear is disabled; the wear map is black");
    }
    // Only maps the material produces (e.g. Opacity exists for overlays only).
    for (MapType map : (settings.common.maps & maps.available).list()) {
        const EncodedMap e = encodeMap(maps, map);
        const auto path = mapFilePath(dir, settings, map);
        if (e.bitDepth == 16) {
            writePng16Gray(path, e.width, e.height, e.data16.data());
        } else {
            writePng8(path, e.width, e.height, e.channels, e.data8.data());
        }
        result.files.push_back({map, path, e.format});
    }
    result.settingsFile = settingsFilePath(dir, settings);
    savePreset(result.settingsFile, settings);
    return result;
}

} // namespace md
