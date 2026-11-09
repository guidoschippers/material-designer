#include "md/noise/Noise.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace md::noise;
using Catch::Matchers::WithinAbs;

namespace {

// Pseudo-random sample positions (deterministic).
float coord(int i, int k) { return toUnit(hash32(static_cast<std::uint32_t>(i * 7 + k))) * 20.0f - 5.0f; }

} // namespace

TEST_CASE("Hash and toUnit are deterministic and in range")
{
    REQUIRE(hash32(12345u) == hash32(12345u));
    REQUIRE(hash32(1u) != hash32(2u));
    for (std::uint32_t i = 0; i < 1000; ++i) {
        const float f = toUnit(hash32(i));
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
    }
}

TEST_CASE("Noise functions are periodic with their period")
{
    const Period p{4, 6};
    const std::uint32_t seed = 42;
    for (int i = 0; i < 200; ++i) {
        const float x = coord(i, 0), y = coord(i, 1);
        CHECK_THAT(value(x, y, p, seed), WithinAbs(value(x + 4, y, p, seed), 1e-4));
        CHECK_THAT(value(x, y, p, seed), WithinAbs(value(x, y - 6, p, seed), 1e-4));
        CHECK_THAT(perlin(x, y, p, seed), WithinAbs(perlin(x + 4, y + 6, p, seed), 1e-4));
        CHECK_THAT(fbm(x, y, p, seed, 4), WithinAbs(fbm(x - 8, y, p, seed, 4), 1e-4));
        CHECK_THAT(ridged(x, y, p, seed, 3), WithinAbs(ridged(x, y + 12, p, seed, 3), 1e-4));

        const auto a = voronoi(x, y, p, seed);
        const auto b = voronoi(x + 4, y + 6, p, seed);
        CHECK_THAT(a.f1, WithinAbs(b.f1, 1e-4));
        CHECK_THAT(a.f2, WithinAbs(b.f2, 1e-4));
        CHECK(a.id == b.id);

        const auto ea = voronoiEdge(x, y, p, seed);
        const auto eb = voronoiEdge(x - 4, y, p, seed);
        CHECK_THAT(ea.edge, WithinAbs(eb.edge, 1e-4));
        CHECK(ea.id == eb.id);
    }
}

TEST_CASE("Noise without period does not repeat at integer offsets")
{
    int differences = 0;
    for (int i = 0; i < 100; ++i) {
        const float x = coord(i, 2), y = coord(i, 3);
        if (std::abs(perlin(x, y, {}, 7) - perlin(x + 4, y, {}, 7)) > 1e-3f) {
            ++differences;
        }
    }
    REQUIRE(differences > 80);
}

TEST_CASE("Noise ranges are sane")
{
    for (int i = 0; i < 2000; ++i) {
        const float x = coord(i, 4), y = coord(i, 5);
        const float v = value(x, y, {}, 3);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        const float n = perlin(x, y, {}, 3);
        REQUIRE(n >= -1.01f);
        REQUIRE(n <= 1.01f);
        const float r = ridged(x, y, {}, 3, 4);
        REQUIRE(r >= 0.0f);
        REQUIRE(r <= 1.0f);
        const auto e = voronoiEdge(x, y, {}, 3);
        REQUIRE(e.edge >= -1e-4f);
        REQUIRE(e.edge < 1.0f); // with jitter <= 1 no point is a full cell away from a border
    }
}
