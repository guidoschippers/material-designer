#include "Commands.h"

#include "Output.h"

#include "md/core/Error.h"
#include "md/core/Registry.h"
#include "md/core/Version.h"
#include "md/io/Export.h"
#include "md/io/Preset.h"

#include <chrono>
#include <iostream>
#include <sstream>

using nlohmann::json;

namespace cli {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

const md::Material& findMaterial(const std::string& id)
{
    const md::Material* m = md::MaterialRegistry::builtin().find(id);
    if (!m) {
        std::string valid;
        for (const auto& mat : md::MaterialRegistry::builtin().all()) {
            valid += (valid.empty() ? "" : ", ") + std::string(mat->id());
        }
        throw md::Error(md::ErrorCode::UnknownMaterial, "unknown material '" + id + "' (available: " + valid + ")");
    }
    return *m;
}

md::MapSet parseMaps(const std::string& text)
{
    if (text == "all") {
        return md::MapSet::all();
    }
    md::MapSet set;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        const auto map = md::parseMapId(item);
        if (!map) {
            throw md::Error(md::ErrorCode::InvalidArgument,
                            "unknown map '" + item + "' (valid: albedo, normal, roughness, height, ao, wear, all)");
        }
        set.set(*map, true);
    }
    if (set.empty()) {
        throw md::Error(md::ErrorCode::InvalidArgument, "--maps selects no maps");
    }
    return set;
}

/// Builds the settings: defaults <- preset <- --set <- explicit flags.
md::MaterialSettings buildSettings(const md::Material& material, const GenerateArgs& a, std::vector<std::string>& warnings)
{
    md::MaterialSettings s = a.preset.empty()
                                 ? md::defaultSettings(material)
                                 : md::loadPreset(pathFromUtf8(a.preset), md::MaterialRegistry::builtin(), &material, warnings);

    for (const std::string& item : a.sets) {
        const auto eq = item.find('=');
        if (eq == std::string::npos || eq == 0) {
            throw md::Error(md::ErrorCode::InvalidArgument, "--set expects key=value, got '" + item + "'");
        }
        const std::string key = item.substr(0, eq);
        const md::ParamDef* def = material.schema().find(key);
        if (!def) {
            throw md::Error(md::ErrorCode::InvalidArgument, "unknown parameter '" + key + "' for material '" +
                                                                std::string(material.id()) +
                                                                "' (see: material-designer-cli describe " +
                                                                std::string(material.id()) + ")");
        }
        std::string error;
        auto value = md::parseParamValue(*def, item.substr(eq + 1), &error);
        if (!value) {
            throw md::Error(md::ErrorCode::InvalidArgument, error);
        }
        s.params[key] = std::move(*value);
    }

    md::CommonSettings& c = s.common;
    if (a.seed) {
        if (*a.seed > 0xffffffffULL) throw md::Error(md::ErrorCode::InvalidArgument, "--seed must be in [0, 4294967295]");
        c.seed = static_cast<std::uint32_t>(*a.seed);
    }
    if (a.size) c.size = *a.size;
    if (a.seamless && a.noSeamless) throw md::Error(md::ErrorCode::InvalidArgument, "--seamless and --no-seamless conflict");
    if (a.seamless) c.seamless = true;
    if (a.noSeamless) c.seamless = false;
    if (a.wear && a.noWear) throw md::Error(md::ErrorCode::InvalidArgument, "--wear and --no-wear conflict");
    if (a.wear) {
        if (*a.wear < 0.0 || *a.wear > 1.0) throw md::Error(md::ErrorCode::InvalidArgument, "--wear must be in [0, 1]");
        c.wear = true;
        c.wearAmount = *a.wear;
    }
    if (a.noWear) c.wear = false;
    if (a.maskOnly) c.applyWear = false;
    if (!a.maps.empty()) c.maps = parseMaps(a.maps);
    if (!a.normal.empty()) {
        const auto conv = md::parseNormalConvention(a.normal);
        if (!conv) throw md::Error(md::ErrorCode::InvalidArgument, "--normal must be directx or opengl");
        c.normalConvention = *conv;
    }
    if (a.normalStrength) c.normalStrength = *a.normalStrength;

    md::normalizeSettings(material, s, warnings);
    return s;
}

} // namespace

int runList(bool /*json*/)
{
    json list = json::array();
    for (const auto& m : md::MaterialRegistry::builtin().all()) {
        list.push_back({{"id", std::string(m->id())},
                        {"name", std::string(m->displayName())},
                        {"description", std::string(m->description())},
                        {"parameter_count", m->schema().params().size()}});
    }
    printJson(json{{"version", std::string(md::version())}, {"materials", list}});
    return 0;
}

int runDescribe(const std::string& material)
{
    printJson(md::describeMaterial(findMaterial(material)));
    return 0;
}

int runGenerate(const GenerateArgs& args, bool json)
{
    const auto start = Clock::now();
    const md::Material& material = findMaterial(args.material);

    std::vector<std::string> warnings;
    const md::MaterialSettings settings = buildSettings(material, args, warnings);

    const auto genStart = Clock::now();
    const md::MaterialMaps maps = material.generate(settings);
    const double generateMs = msSince(genStart);

    const auto writeStart = Clock::now();
    const md::ExportResult result = md::exportMaps(maps, settings, pathFromUtf8(args.outDir));
    const double writeMs = msSince(writeStart);
    warnings.insert(warnings.end(), result.warnings.begin(), result.warnings.end());

    if (json) {
        nlohmann::json files = nlohmann::json::array();
        for (const auto& f : result.files) {
            files.push_back({{"map", std::string(md::mapId(f.map))}, {"path", pathToUtf8(f.path)}, {"format", f.format}});
        }
        printJson({
            {"ok", true},
            {"material", settings.material},
            {"seed", settings.common.seed},
            {"size", settings.common.size},
            {"seamless", settings.common.seamless},
            {"wear", settings.common.wear},
            {"files", files},
            {"settings_file", pathToUtf8(result.settingsFile)},
            {"timings_ms", {{"generate", generateMs}, {"write", writeMs}, {"total", msSince(start)}}},
            {"warnings", warnings},
        });
    } else {
        for (const std::string& w : warnings) {
            std::cerr << "warning: " << w << "\n";
        }
        for (const auto& f : result.files) {
            std::cout << pathToUtf8(f.path) << "\n";
        }
        std::cout << pathToUtf8(result.settingsFile) << "\n";
        std::cout << "Generated " << settings.material << " (seed " << settings.common.seed << ", " << settings.common.size
                  << "x" << settings.common.size << ") in " << static_cast<int>(msSince(start)) << " ms\n";
    }
    return 0;
}

} // namespace cli
