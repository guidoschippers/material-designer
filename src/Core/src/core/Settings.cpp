#include "md/core/Settings.h"

#include <algorithm>
#include <cctype>

namespace md {

namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::string_view mapId(MapType map)
{
    switch (map) {
    case MapType::Albedo: return "albedo";
    case MapType::Normal: return "normal";
    case MapType::Roughness: return "roughness";
    case MapType::Height: return "height";
    case MapType::AmbientOcclusion: return "ao";
    case MapType::Wear: return "wear";
    case MapType::Opacity: return "opacity";
    }
    return "unknown";
}

std::string_view mapLabel(MapType map)
{
    switch (map) {
    case MapType::Albedo: return "Albedo";
    case MapType::Normal: return "Normal";
    case MapType::Roughness: return "Roughness";
    case MapType::Height: return "Height";
    case MapType::AmbientOcclusion: return "Ambient Occlusion";
    case MapType::Wear: return "Wear mask";
    case MapType::Opacity: return "Opacity";
    }
    return "Unknown";
}

std::optional<MapType> parseMapId(std::string_view id)
{
    for (MapType m : kAllMaps) {
        if (equalsIgnoreCase(id, mapId(m))) {
            return m;
        }
    }
    if (equalsIgnoreCase(id, "ambient_occlusion")) {
        return MapType::AmbientOcclusion;
    }
    return std::nullopt;
}

std::vector<MapType> MapSet::list() const
{
    std::vector<MapType> out;
    for (MapType m : kAllMaps) {
        if (contains(m)) {
            out.push_back(m);
        }
    }
    return out;
}

std::string_view normalConventionId(NormalConvention c)
{
    return c == NormalConvention::DirectX ? "directx" : "opengl";
}

std::optional<NormalConvention> parseNormalConvention(std::string_view id)
{
    if (equalsIgnoreCase(id, "directx") || equalsIgnoreCase(id, "dx")) return NormalConvention::DirectX;
    if (equalsIgnoreCase(id, "opengl") || equalsIgnoreCase(id, "gl")) return NormalConvention::OpenGL;
    return std::nullopt;
}

bool isValidSize(int size)
{
    return size >= kMinSize && size <= kMaxSize && (size & (size - 1)) == 0;
}

} // namespace md
