#pragma once

// Settings shared by all materials and the complete settings of one generation run.

#include "md/core/Parameter.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace md {

/// The texture maps a material produces.
enum class MapType { Albedo, Normal, Roughness, Height, AmbientOcclusion, Wear };

inline constexpr int kMapCount = 6;
inline constexpr std::array<MapType, kMapCount> kAllMaps = {
    MapType::Albedo, MapType::Normal, MapType::Roughness, MapType::Height, MapType::AmbientOcclusion, MapType::Wear};

/// Stable identifier used in file names, presets and the CLI: albedo, normal, roughness, height, ao, wear.
std::string_view mapId(MapType map);
/// Display name, e.g. "Ambient Occlusion".
std::string_view mapLabel(MapType map);
/// Parses a map id (case-insensitive; also accepts "ambient_occlusion").
std::optional<MapType> parseMapId(std::string_view id);

/// Set of maps (bit i = MapType i).
class MapSet {
public:
    MapSet() = default;
    static MapSet all() { MapSet s; s.bits_ = (1u << kMapCount) - 1u; return s; }

    bool contains(MapType m) const { return (bits_ >> static_cast<unsigned>(m)) & 1u; }
    void set(MapType m, bool on)
    {
        const unsigned bit = 1u << static_cast<unsigned>(m);
        bits_ = on ? (bits_ | bit) : (bits_ & ~bit);
    }
    bool empty() const { return bits_ == 0; }
    std::vector<MapType> list() const;

    bool operator==(const MapSet&) const = default;

private:
    unsigned bits_ = 0;
};

enum class NormalConvention { DirectX, OpenGL };

std::string_view normalConventionId(NormalConvention c);
std::optional<NormalConvention> parseNormalConvention(std::string_view id);

/// Valid output resolutions: powers of two in [kMinSize, kMaxSize].
inline constexpr int kMinSize = 64;
inline constexpr int kMaxSize = 8192;
bool isValidSize(int size);

/// Settings every material has.
struct CommonSettings {
    int size = 1024;                ///< Output resolution (square, power of two).
    std::uint32_t seed = 1;         ///< Random seed; same seed + settings = identical output.
    bool seamless = true;           ///< Tile perfectly on all edges.
    bool wear = false;              ///< Generate the wear mask (and wear effects).
    double wearAmount = 0.5;        ///< Overall wear intensity in [0, 1].
    bool applyWear = true;          ///< Apply the wear effects to the base maps (otherwise only the mask is produced).
    MapSet maps = MapSet::all();    ///< Maps to export.
    NormalConvention normalConvention = NormalConvention::DirectX;
    double normalStrength = 1.0;    ///< Multiplier on the material's natural relief, in [0, 4].

    bool operator==(const CommonSettings&) const = default;
};

/// Everything needed to reproduce one texture set.
struct MaterialSettings {
    std::string material;   ///< Material id, e.g. "asphalt".
    CommonSettings common;
    ParamValues params;     ///< Material-specific parameter values.
};

} // namespace md
