// Tests that run against every registered material, so new materials are covered automatically.

#include "TestMaterial.h"

#include "md/core/Error.h"
#include "md/core/Parallel.h"
#include "md/core/Registry.h"
#include "md/io/Preset.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace md;

namespace {

MaterialSettings testSettings(const Material& m, int size, bool seamless, bool wear)
{
    MaterialSettings s = defaultSettings(m);
    s.common.size = size;
    s.common.seed = 4242;
    s.common.seamless = seamless;
    s.common.wear = wear;
    s.common.wearAmount = 0.8;
    // Exercise the optional features: put every float parameter slightly above its default.
    for (const ParamDef& d : m.schema().params()) {
        if (d.type == ParamType::Float) {
            s.params[d.key] = std::min(d.maxValue, d.defaultValue + 0.25 * (d.maxValue - d.minValue));
        } else if (d.type == ParamType::Bool) {
            s.params[d.key] = true;
        }
    }
    return s;
}

bool sameImage(const ImageF& a, const ImageF& b)
{
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() && a.data() == b.data();
}

/// Compares the pixel step across the wrap seam (last column -> first column) with the steps
/// between all neighbouring interior columns (or rows). For a seamless texture the seam is just
/// another column pair, so its mean step must lie within the interior distribution.
struct SeamStats {
    double seam = 0.0;     ///< mean |difference| across the seam
    double typical = 0.0;  ///< 98th percentile of the mean |difference| of interior column pairs
};

SeamStats seamStats(const ImageF& img, bool horizontal)
{
    const int n = img.width();
    const int ch = img.channels();
    auto px = [&](int along, int i, int c) { return horizontal ? img.at(along, i, c) : img.at(i, along, c); };
    auto pairMean = [&](int a, int b) {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < ch; ++c) {
                sum += std::abs(px(a, i, c) - px(b, i, c));
            }
        }
        return sum / (static_cast<double>(n) * ch);
    };
    std::vector<double> pairs;
    for (int a = 0; a + 1 < n; ++a) {
        pairs.push_back(pairMean(a, a + 1));
    }
    std::sort(pairs.begin(), pairs.end());
    SeamStats s;
    s.seam = pairMean(n - 1, 0);
    s.typical = pairs[static_cast<std::size_t>(0.98 * static_cast<double>(pairs.size() - 1))];
    return s;
}

bool isSeamless(const SeamStats& s)
{
    return s.seam <= s.typical * 1.25 + 1e-4;
}

} // namespace

TEST_CASE("At least one material is registered")
{
    REQUIRE_FALSE(MaterialRegistry::builtin().all().empty());
}

TEST_CASE("Every material has a valid schema")
{
    for (const auto& m : MaterialRegistry::builtin().all()) {
        INFO("material: " << m->id());
        CHECK_FALSE(m->id().empty());
        CHECK_FALSE(m->displayName().empty());
        CHECK_FALSE(m->description().empty());
        CHECK(m->reliefDepth() > 0.0f);
        const auto problems = m->schema().validate();
        for (const auto& p : problems) {
            UNSCOPED_INFO(p);
        }
        CHECK(problems.empty());
        for (const ParamDef& d : m->schema().params()) {
            if (d.type == ParamType::Int || d.type == ParamType::Float) {
                CHECK(d.defaultValue >= d.minValue);
                CHECK(d.defaultValue <= d.maxValue);
            }
        }
    }
}

TEST_CASE("Generation is deterministic and independent of the thread count")
{
    for (const auto& m : MaterialRegistry::builtin().all()) {
        INFO("material: " << m->id());
        const MaterialSettings s = testSettings(*m, 128, true, true);
        const MaterialMaps a = m->generate(s);
        setMaxThreads(1);
        const MaterialMaps b = m->generate(s);
        setMaxThreads(0);
        for (MapType map : kAllMaps) {
            INFO("map: " << mapId(map));
            CHECK(sameImage(a.get(map), b.get(map)));
        }

        MaterialSettings other = s;
        other.common.seed = 4243;
        CHECK_FALSE(sameImage(a.albedo, m->generate(other).albedo));
    }
}

TEST_CASE("Seamless output tiles on all edges")
{
    for (const auto& m : MaterialRegistry::builtin().all()) {
        for (bool wear : {false, true}) {
            const MaterialSettings settings = testSettings(*m, 256, true, wear);
            const MaterialMaps maps = m->generate(settings);
            const TileAxes tiles = m->tileAxes(ParamReader(m->schema(), settings.params));
            for (MapType map : kAllMaps) {
                for (bool horizontal : {true, false}) {
                    if (!(horizontal ? tiles.u : tiles.v)) {
                        continue; // overlays may tile on one axis only
                    }
                    INFO("material: " << m->id() << ", map: " << mapId(map) << ", wear: " << wear
                                      << (horizontal ? ", left/right" : ", top/bottom"));
                    const SeamStats s = seamStats(maps.get(map), horizontal);
                    INFO("seam step " << s.seam << " vs typical interior step " << s.typical);
                    CHECK(isSeamless(s));
                }
            }
        }
    }
}

TEST_CASE("Overlays produce a meaningful opacity map; base materials are opaque")
{
    for (const auto& m : MaterialRegistry::builtin().all()) {
        INFO("material: " << m->id());
        const MaterialMaps maps = m->generate(testSettings(*m, 128, true, true));
        const auto [lo, hi] = std::minmax_element(maps.opacity.data().begin(), maps.opacity.data().end());
        CHECK(maps.available.contains(MapType::Opacity) == m->isOverlay());
        if (m->isOverlay()) {
            CHECK(*lo < 0.05f); // transparent somewhere
            CHECK(*hi > 0.8f);  // (nearly) opaque somewhere
        } else {
            CHECK(*lo == 1.0f);
        }
    }
}

TEST_CASE("A continuous road marking tiles vertically")
{
    const Material* m = MaterialRegistry::builtin().find("road_marking");
    REQUIRE(m);
    MaterialSettings s = testSettings(*m, 256, true, true);
    s.params["shape"] = std::string("continuous");
    const TileAxes tiles = m->tileAxes(ParamReader(m->schema(), s.params));
    REQUIRE(tiles.v);
    REQUIRE_FALSE(tiles.u);
    const MaterialMaps maps = m->generate(s);
    for (MapType map : kAllMaps) {
        INFO("map: " << mapId(map));
        CHECK(isSeamless(seamStats(maps.get(map), false)));
    }
}

TEST_CASE("The seam check detects non-tiling output")
{
    // Sanity check of the metric itself on a smooth test material.
    const mdtest::TestMaterial m;
    MaterialSettings s = defaultSettings(m);
    s.common.size = 128;
    s.common.seamless = true;
    CHECK(isSeamless(seamStats(m.generate(s).height, true)));
    s.common.seamless = false;
    CHECK_FALSE(isSeamless(seamStats(m.generate(s).height, true)));
}

TEST_CASE("All maps have the requested size and valid ranges")
{
    for (const auto& m : MaterialRegistry::builtin().all()) {
        INFO("material: " << m->id());
        GenerateOptions options;
        options.sizeOverride = 64;
        const MaterialMaps maps = m->generate(testSettings(*m, 1024, false, true), options);
        REQUIRE(maps.size == 64);
        for (MapType map : kAllMaps) {
            const ImageF& img = maps.get(map);
            CHECK(img.width() == 64);
            CHECK(img.height() == 64);
            const float lo = map == MapType::Normal ? -1.0f : 0.0f;
            bool inRange = true;
            for (float v : img.data()) {
                inRange = inRange && std::isfinite(v) && v >= lo - 1e-5f && v <= 1.0f + 1e-5f;
            }
            CHECK(inRange);
        }
    }
}

TEST_CASE("Generation can be cancelled")
{
    const Material& m = *MaterialRegistry::builtin().all().front();
    std::stop_source source;
    source.request_stop();
    GenerateOptions options;
    options.stop = source.get_token();
    REQUIRE_THROWS_AS(m.generate(defaultSettings(m), options), Cancelled);
}
