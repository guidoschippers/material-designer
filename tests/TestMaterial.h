#pragma once

// A tiny material used by the core unit tests (independent of the built-in materials).

#include "md/core/Material.h"
#include "md/noise/Noise.h"

namespace mdtest {

class TestMaterial final : public md::Material {
public:
    TestMaterial()
    {
        schema_.addFloat("scale", "Scale", 4.0, 1.0, 16.0).inGroup("Shape").tip("Noise frequency.");
        schema_.addInt("octaves", "Octaves", 3, 1, 8).inGroup("Shape").tip("Number of octaves.");
        schema_.addBool("invert", "Invert", false).inGroup("Shape").tip("Inverts the height.");
        schema_.addEnum("palette", "Palette", {{"red", "Red"}, {"blue", "Blue"}}, "red").inGroup("Colour").tip("Colour palette.");
        schema_.addColor("tint", "Tint", md::Color{0.5f, 0.25f, 1.0f}).inGroup("Colour").tip("Tint colour.");
        schema_.addFloat("scratches", "Scratches", 0.5, 0.0, 1.0).inGroup("Wear").tip("Scratch amount.").wear();
    }

    std::string_view id() const override { return "test"; }
    std::string_view displayName() const override { return "Test"; }
    std::string_view description() const override { return "Test material."; }
    const md::ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.01f; }

    void generateSurface(const md::GenerateContext& ctx, md::SurfaceMaps& out) const override
    {
        const auto& p = ctx.params();
        const int f = md::noise::frequency(p.getFloat("scale"));
        const auto period = md::noise::periodFor(f, f, ctx.seamless());
        const int octaves = p.getInt("octaves");
        const bool invert = p.getBool("invert");
        ctx.forEachPixel([&](int x, int y, float u, float v) {
            float h = md::noise::fbm01(u * f, v * f, period, ctx.seed(), octaves);
            if (invert) h = 1.0f - h;
            out.height.at(x, y) = h;
            out.albedo.at(x, y, 0) = h;
            out.albedo.at(x, y, 1) = h * 0.5f;
            out.albedo.at(x, y, 2) = 1.0f - h;
            out.roughness.at(x, y) = 0.5f;
            out.wear.at(x, y) = ctx.wearAmount() * h;
        });
    }

private:
    md::ParamSchema schema_;
};

} // namespace mdtest
