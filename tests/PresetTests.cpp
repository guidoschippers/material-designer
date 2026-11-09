#include "TestMaterial.h"

#include "md/core/Error.h"
#include "md/io/Preset.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace md;
using nlohmann::json;

namespace {

MaterialRegistry makeRegistry()
{
    MaterialRegistry r;
    r.add(std::make_unique<mdtest::TestMaterial>());
    return r;
}

} // namespace

TEST_CASE("Settings round-trip through JSON")
{
    const MaterialRegistry registry = makeRegistry();
    const Material& m = *registry.find("test");

    MaterialSettings s = defaultSettings(m);
    s.common.size = 256;
    s.common.seed = 987654321u;
    s.common.seamless = false;
    s.common.wear = true;
    s.common.wearAmount = 0.25;
    s.common.maps = MapSet();
    s.common.maps.set(MapType::Albedo, true);
    s.common.maps.set(MapType::Height, true);
    s.common.normalConvention = NormalConvention::OpenGL;
    s.params["scale"] = 3.5;
    s.params["palette"] = std::string("blue");
    s.params["tint"] = Color{1.0f, 0.0f, 0.0f};

    std::vector<std::string> warnings;
    const MaterialSettings back = settingsFromJson(settingsToJson(s), registry, &m, warnings);
    REQUIRE(warnings.empty());
    REQUIRE(back.material == "test");
    REQUIRE(back.common == s.common);
    REQUIRE(back.params == s.params);

    const auto path = std::filesystem::temp_directory_path() / "md_preset_test.json";
    savePreset(path, s);
    const MaterialSettings loaded = loadPreset(path, registry, nullptr, warnings);
    REQUIRE(loaded.params == s.params);
    std::filesystem::remove(path);
}

TEST_CASE("Partial presets use defaults and report unknown keys")
{
    const MaterialRegistry registry = makeRegistry();
    std::vector<std::string> warnings;
    const json j = {{"material", "test"}, {"params", {{"scale", 99.0}, {"nope", 1}}}};
    const MaterialSettings s = settingsFromJson(j, registry, nullptr, warnings);
    REQUIRE(std::get<double>(s.params.at("scale")) == 16.0); // clamped
    REQUIRE(std::get<int>(s.params.at("octaves")) == 3);     // default
    REQUIRE(warnings.size() == 2);
}

TEST_CASE("Invalid presets are rejected")
{
    const MaterialRegistry registry = makeRegistry();
    std::vector<std::string> warnings;
    REQUIRE_THROWS_AS(settingsFromJson(json::array(), registry, nullptr, warnings), Error);
    REQUIRE_THROWS_AS(settingsFromJson(json{{"material", "unknown"}}, registry, nullptr, warnings), Error);
    REQUIRE_THROWS_AS(settingsFromJson(json{{"material", "test"}, {"common", {{"size", "big"}}}}, registry, nullptr, warnings), Error);
    REQUIRE_THROWS_AS(settingsFromJson(json{{"material", "test"}, {"common", {{"size", 1000}}}}, registry, nullptr, warnings), Error);
    REQUIRE_THROWS_AS(settingsFromJson(json{{"material", "test"}, {"common", {{"maps", {"albedo", "foo"}}}}}, registry, nullptr, warnings), Error);
}

TEST_CASE("describeMaterial lists every parameter")
{
    const mdtest::TestMaterial m;
    const json d = describeMaterial(m);
    REQUIRE(d["id"] == "test");
    REQUIRE(d["parameters"].size() == m.schema().params().size());
    REQUIRE(d["parameters"][0]["key"] == "scale");
    REQUIRE(d["parameters"][3]["options"].size() == 2);
    REQUIRE(d["common"].contains("seed"));
}
