#pragma once

// JSON presets: serialisation of MaterialSettings, plus schema descriptions for `describe`.
//
// Preset format (all fields except "material" are optional; missing values use defaults):
// {
//   "format": "material-designer-preset", "version": 1,
//   "material": "asphalt",
//   "common": { "size": 1024, "seed": 1, "seamless": true, "wear": false, "wear_amount": 0.5,
//               "apply_wear": true, "maps": ["albedo", ...], "normal_convention": "directx",
//               "normal_strength": 1.0 },
//   "params": { "<key>": <bool | number | "option id" | "#rrggbb">, ... }
// }

#include "md/core/Material.h"
#include "md/core/Registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace md {

inline constexpr int kPresetVersion = 1;
inline constexpr const char* kPresetFormat = "material-designer-preset";

/// Settings with every value at its default.
MaterialSettings defaultSettings(const Material& material);

/// Makes settings consistent with the material schema: fills missing parameters with defaults,
/// clamps out-of-range values and removes unknown keys. Problems are appended to `warnings`.
/// Throws md::Error(InvalidArgument) for unrecoverable problems (e.g. an invalid size).
void normalizeSettings(const Material& material, MaterialSettings& settings, std::vector<std::string>& warnings);

nlohmann::json paramValueToJson(const ParamValue& value);
nlohmann::json commonToJson(const CommonSettings& common);
nlohmann::json settingsToJson(const MaterialSettings& settings);

/// Reads settings from preset JSON. When `material` is non-null the preset must be for that
/// material (or not name one). Throws md::Error(InvalidPreset / UnknownMaterial).
MaterialSettings settingsFromJson(const nlohmann::json& json, const MaterialRegistry& registry,
                                  const Material* material, std::vector<std::string>& warnings);

void savePreset(const std::filesystem::path& path, const MaterialSettings& settings);
MaterialSettings loadPreset(const std::filesystem::path& path, const MaterialRegistry& registry,
                            const Material* material, std::vector<std::string>& warnings);

/// Full machine-readable description of a material (common settings + parameter schema).
nlohmann::json describeMaterial(const Material& material);
nlohmann::json paramDefToJson(const ParamDef& def);

} // namespace md
