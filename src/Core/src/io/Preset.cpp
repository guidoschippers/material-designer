#include "md/io/Preset.h"

#include "md/core/Error.h"
#include "md/core/Version.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace md {

namespace {

[[noreturn]] void presetError(const std::string& message)
{
    throw Error(ErrorCode::InvalidPreset, "invalid preset: " + message);
}

/// Converts a JSON value to a ParamValue according to the parameter type.
std::optional<ParamValue> paramValueFromJson(const ParamDef& def, const json& j)
{
    switch (def.type) {
    case ParamType::Bool:
        if (j.is_boolean()) return ParamValue{j.get<bool>()};
        if (j.is_number_integer()) return ParamValue{j.get<int>() != 0};
        break;
    case ParamType::Int:
        if (j.is_number()) return ParamValue{static_cast<int>(std::lround(j.get<double>()))};
        break;
    case ParamType::Float:
        if (j.is_number()) return ParamValue{j.get<double>()};
        break;
    case ParamType::Enum:
        if (j.is_string()) return ParamValue{j.get<std::string>()};
        break;
    case ParamType::Color:
        if (j.is_string()) {
            if (auto c = colorFromHex(j.get<std::string>())) return ParamValue{*c};
        } else if (j.is_array() && j.size() == 3 && std::all_of(j.begin(), j.end(), [](const json& e) { return e.is_number(); })) {
            return ParamValue{Color{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}};
        }
        break;
    }
    return std::nullopt;
}

template <class T>
T readField(const json& obj, const char* key, T fallback, bool (json::*check)() const noexcept)
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (!((*it).*check)()) {
        presetError(std::string("field 'common.") + key + "' has the wrong type");
    }
    return it->get<T>();
}

CommonSettings commonFromJson(const json& j)
{
    if (!j.is_object()) {
        presetError("'common' must be an object");
    }
    CommonSettings c;
    c.size = readField<int>(j, "size", c.size, &json::is_number_integer);
    c.seed = readField<std::uint32_t>(j, "seed", c.seed, &json::is_number_unsigned);
    c.seamless = readField<bool>(j, "seamless", c.seamless, &json::is_boolean);
    c.wear = readField<bool>(j, "wear", c.wear, &json::is_boolean);
    c.wearAmount = readField<double>(j, "wear_amount", c.wearAmount, &json::is_number);
    c.applyWear = readField<bool>(j, "apply_wear", c.applyWear, &json::is_boolean);
    c.normalStrength = readField<double>(j, "normal_strength", c.normalStrength, &json::is_number);
    if (auto it = j.find("normal_convention"); it != j.end()) {
        const auto conv = it->is_string() ? parseNormalConvention(it->get<std::string>()) : std::nullopt;
        if (!conv) presetError("'common.normal_convention' must be \"directx\" or \"opengl\"");
        c.normalConvention = *conv;
    }
    if (auto it = j.find("maps"); it != j.end()) {
        if (!it->is_array()) presetError("'common.maps' must be an array of map ids");
        MapSet maps;
        for (const json& m : *it) {
            const auto map = m.is_string() ? parseMapId(m.get<std::string>()) : std::nullopt;
            if (!map) presetError("unknown map in 'common.maps': " + m.dump());
            maps.set(*map, true);
        }
        c.maps = maps;
    }
    return c;
}

} // namespace

MaterialSettings defaultSettings(const Material& material)
{
    MaterialSettings s;
    s.material = std::string(material.id());
    s.params = material.schema().defaults();
    return s;
}

void normalizeSettings(const Material& material, MaterialSettings& settings, std::vector<std::string>& warnings)
{
    settings.material = std::string(material.id());
    CommonSettings& c = settings.common;
    if (!isValidSize(c.size)) {
        throw Error(ErrorCode::InvalidArgument, "invalid size " + std::to_string(c.size) + " (must be a power of two between " +
                                                    std::to_string(kMinSize) + " and " + std::to_string(kMaxSize) + ")");
    }
    auto clampField = [&](double& v, double lo, double hi, const char* name) {
        const double clamped = std::clamp(v, lo, hi);
        if (clamped != v) {
            warnings.push_back(std::string(name) + " clamped to [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
            v = clamped;
        }
    };
    clampField(c.wearAmount, 0.0, 1.0, "wear_amount");
    clampField(c.normalStrength, 0.0, 4.0, "normal_strength");

    const ParamSchema& schema = material.schema();
    ParamValues normalized;
    for (const auto& [key, value] : settings.params) {
        const ParamDef* def = schema.find(key);
        if (!def) {
            warnings.push_back("unknown parameter '" + key + "' for material '" + settings.material + "' ignored");
            continue;
        }
        ParamValue v = value;
        if (!sanitizeValue(*def, v, &warnings)) {
            warnings.push_back("parameter '" + key + "' has an invalid value; using the default");
            v = def->defaultParamValue();
        }
        normalized[key] = std::move(v);
    }
    for (const ParamDef& def : schema.params()) {
        normalized.try_emplace(def.key, def.defaultParamValue());
    }
    settings.params = std::move(normalized);
}

json paramValueToJson(const ParamValue& value)
{
    struct Visitor {
        json operator()(bool v) const { return v; }
        json operator()(int v) const { return v; }
        json operator()(double v) const { return v; }
        json operator()(const std::string& v) const { return v; }
        json operator()(Color c) const { return colorToHex(c); }
    };
    return std::visit(Visitor{}, value);
}

json commonToJson(const CommonSettings& c)
{
    json maps = json::array();
    for (MapType m : c.maps.list()) {
        maps.push_back(std::string(mapId(m)));
    }
    return json{
        {"size", c.size},
        {"seed", c.seed},
        {"seamless", c.seamless},
        {"wear", c.wear},
        {"wear_amount", c.wearAmount},
        {"apply_wear", c.applyWear},
        {"maps", maps},
        {"normal_convention", std::string(normalConventionId(c.normalConvention))},
        {"normal_strength", c.normalStrength},
    };
}

json settingsToJson(const MaterialSettings& s)
{
    json params = json::object();
    for (const auto& [key, value] : s.params) {
        params[key] = paramValueToJson(value);
    }
    return json{
        {"format", kPresetFormat},
        {"version", kPresetVersion},
        {"generator", "material-designer " + std::string(version())},
        {"material", s.material},
        {"common", commonToJson(s.common)},
        {"params", params},
    };
}

MaterialSettings settingsFromJson(const json& j, const MaterialRegistry& registry, const Material* material,
                                  std::vector<std::string>& warnings)
{
    if (!j.is_object()) {
        presetError("top level must be a JSON object");
    }
    if (auto it = j.find("format"); it != j.end() && *it != kPresetFormat) {
        presetError("unexpected format " + it->dump());
    }
    if (auto it = j.find("version"); it != j.end()) {
        if (!it->is_number_integer()) presetError("'version' must be an integer");
        if (it->get<int>() > kPresetVersion) {
            warnings.push_back("preset version " + std::to_string(it->get<int>()) + " is newer than supported (" +
                               std::to_string(kPresetVersion) + ")");
        }
    }

    const Material* target = material;
    if (auto it = j.find("material"); it != j.end()) {
        if (!it->is_string()) presetError("'material' must be a string");
        const std::string id = it->get<std::string>();
        if (material && material->id() != id) {
            presetError("preset is for material '" + id + "', not '" + std::string(material->id()) + "'");
        }
        target = registry.find(id);
        if (!target) {
            throw Error(ErrorCode::UnknownMaterial, "unknown material '" + id + "'");
        }
    }
    if (!target) {
        presetError("'material' is missing");
    }

    MaterialSettings s = defaultSettings(*target);
    if (auto it = j.find("common"); it != j.end()) {
        s.common = commonFromJson(*it);
    }
    if (auto it = j.find("params"); it != j.end()) {
        if (!it->is_object()) presetError("'params' must be an object");
        for (const auto& [key, value] : it->items()) {
            const ParamDef* def = target->schema().find(key);
            if (!def) {
                warnings.push_back("unknown parameter '" + key + "' ignored");
                continue;
            }
            auto v = paramValueFromJson(*def, value);
            if (!v) {
                warnings.push_back("parameter '" + key + "' has an invalid value " + value.dump() + "; using the default");
                continue;
            }
            s.params[key] = std::move(*v);
        }
    }
    normalizeSettings(*target, s, warnings);
    return s;
}

void savePreset(const std::filesystem::path& path, const MaterialSettings& settings)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw Error(ErrorCode::Io, "cannot open '" + path.string() + "' for writing");
    }
    file << settingsToJson(settings).dump(2) << "\n";
    if (!file) {
        throw Error(ErrorCode::Io, "failed to write '" + path.string() + "'");
    }
}

MaterialSettings loadPreset(const std::filesystem::path& path, const MaterialRegistry& registry, const Material* material,
                            std::vector<std::string>& warnings)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw Error(ErrorCode::Io, "cannot open preset '" + path.string() + "'");
    }
    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        presetError("'" + path.string() + "' is not valid JSON: " + e.what());
    }
    return settingsFromJson(j, registry, material, warnings);
}

json paramDefToJson(const ParamDef& d)
{
    json j{
        {"key", d.key},
        {"label", d.label},
        {"group", d.group},
        {"type", std::string(paramTypeName(d.type))},
        {"tooltip", d.tooltip},
        {"default", paramValueToJson(d.defaultParamValue())},
    };
    switch (d.type) {
    case ParamType::Int:
        j["min"] = static_cast<int>(d.minValue);
        j["max"] = static_cast<int>(d.maxValue);
        break;
    case ParamType::Float:
        j["min"] = d.minValue;
        j["max"] = d.maxValue;
        break;
    case ParamType::Enum: {
        json opts = json::array();
        for (const EnumOption& o : d.options) {
            opts.push_back({{"id", o.id}, {"label", o.label}});
        }
        j["options"] = opts;
        break;
    }
    case ParamType::Bool:
    case ParamType::Color:
        break;
    }
    j["wear_only"] = d.wearOnly;
    if (!d.enabledBy.empty()) {
        j["enabled_by"] = d.enabledBy;
    }
    return j;
}

json describeMaterial(const Material& material)
{
    const TileAxes tiles = material.tileAxes(ParamReader(material.schema(), material.schema().defaults()));
    json mapList = json::array();
    for (MapType m : material.outputMaps().list()) {
        mapList.push_back({{"id", std::string(mapId(m))}, {"label", std::string(mapLabel(m))}});
    }
    json params = json::array();
    for (const ParamDef& d : material.schema().params()) {
        params.push_back(paramDefToJson(d));
    }
    const CommonSettings defaults;
    json sizes = json::array();
    for (int s = kMinSize; s <= kMaxSize; s *= 2) {
        sizes.push_back(s);
    }
    return json{
        {"id", std::string(material.id())},
        {"name", std::string(material.displayName())},
        {"description", std::string(material.description())},
        {"overlay", material.isOverlay()},
        {"tiling", {{"u", tiles.u}, {"v", tiles.v}}},
        {"maps", mapList},
        {"common",
         {
             {"size", {{"type", "int"}, {"default", defaults.size}, {"allowed", sizes}, {"tooltip", "Output resolution (square)."}}},
             {"seed", {{"type", "uint32"}, {"default", defaults.seed}, {"tooltip", "Random seed; same seed + settings = identical output."}}},
             {"seamless", {{"type", "bool"}, {"default", defaults.seamless}, {"tooltip", "Texture tiles perfectly on all edges."}}},
             {"wear", {{"type", "bool"}, {"default", defaults.wear}, {"tooltip", "Generate the wear mask and wear effects."}}},
             {"wear_amount", {{"type", "float"}, {"default", defaults.wearAmount}, {"min", 0.0}, {"max", 1.0}, {"tooltip", "Overall wear intensity."}}},
             {"apply_wear", {{"type", "bool"}, {"default", defaults.applyWear}, {"tooltip", "Apply wear to the base maps (otherwise only the wear mask is produced)."}}},
             {"maps", {{"type", "map list"}, {"default", "all"}, {"tooltip", "Maps to export."}}},
             {"normal_convention", {{"type", "enum"}, {"default", "directx"}, {"options", {"directx", "opengl"}}, {"tooltip", "Green channel orientation of the normal map."}}},
             {"normal_strength", {{"type", "float"}, {"default", defaults.normalStrength}, {"min", 0.0}, {"max", 4.0}, {"tooltip", "Multiplier on the natural relief for the normal map."}}},
         }},
        {"parameters", params},
    };
}

} // namespace md
