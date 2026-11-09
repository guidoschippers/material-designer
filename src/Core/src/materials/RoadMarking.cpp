#include "materials/RoadMarking.h"

#include "materials/Features.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

enum Layer : std::uint32_t { kEdge = 201, kSpray, kStones, kWearMask, kChips, kDirt, kTone };

/// Signed distance to a vertical rounded rectangle centred at the origin (negative inside).
float roundedBox(float x, float y, float halfW, float halfH, float radius)
{
    const float qx = std::abs(x) - halfW + radius;
    const float qy = std::abs(y) - halfH + radius;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

} // namespace

RoadMarkingMaterial::RoadMarkingMaterial()
{
    auto& s = schema_;
    s.addEnum("shape", "Shape", {{"dash", "Dash"}, {"continuous", "Continuous line"}}, "dash")
        .inGroup("Shape")
        .tip("A single dash (decal) or a continuous line that tiles vertically.");
    s.addFloat("line_width", "Width", 0.22, 0.05, 0.8)
        .inGroup("Shape")
        .tip("Width of the marking as a fraction of the texture width.");
    s.addFloat("dash_length", "Dash length", 0.6, 0.1, 0.95)
        .inGroup("Shape")
        .tip("Length of a dash as a fraction of the texture height (dash shape only).");
    s.addFloat("corner_radius", "Corner rounding", 0.5, 0.0, 1.0)
        .inGroup("Shape")
        .tip("Rounding of the dash corners: 0 = square, 1 = fully rounded ends.");
    s.addFloat("edge_softness", "Overspray", 0.2, 0.0, 1.0)
        .inGroup("Shape")
        .tip("Soft, speckled paint edges from spraying.");

    s.addColor("paint_color", "Paint colour", Color{0.94f, 0.93f, 0.90f})
        .inGroup("Paint")
        .tip("Paint colour (sRGB). White by default; use yellow for temporary markings.");
    s.addFloat("paint_coverage", "Coverage", 0.92, 0.0, 1.0)
        .inGroup("Paint")
        .tip("Opacity of the paint layer.");
    s.addFloat("paint_thickness", "Thickness", 0.4, 0.0, 1.0)
        .inGroup("Paint")
        .tip("Thickness of the paint film (height and normal map).");

    s.addFloat("surface_grain", "Surface grain", 0.6, 0.0, 1.0)
        .inGroup("Surface")
        .tip("How much the stone texture below shows through: gaps between stones stay unpainted.");
    s.addFloat("grain_size", "Grain size", 0.5, 0.0, 1.0)
        .inGroup("Surface")
        .tip("Size of the stones below. Use the same value as the asphalt's stone size.");

    s.addFloat("paint_wear", "Worn paint", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Paint worn away by traffic in patches, starting on the stone tops.");
    s.addFloat("chips", "Chips", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Small flakes of paint chipped off.");
    s.addFloat("dirt", "Dirt", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Grey road dirt on the paint.");
}

TileAxes RoadMarkingMaterial::tileAxes(const ParamReader& params) const
{
    return params.getEnum("shape") == "continuous" ? TileAxes{false, true} : TileAxes{false, false};
}

void RoadMarkingMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const ParamReader& r = ctx.params();
    const bool continuous = r.getEnum("shape") == "continuous";
    const float halfW = 0.5f * r.getFloat("line_width");
    const float halfL = 0.5f * r.getFloat("dash_length");
    const float radius = r.getFloat("corner_radius") * halfW;
    const float softness = r.getFloat("edge_softness");
    const Color pc = r.getColor("paint_color");
    const Vec3 paint = srgbToLinear(Vec3{pc.r, pc.g, pc.b});
    const float coverage = r.getFloat("paint_coverage");
    const float thickness = r.getFloat("paint_thickness");
    const float grain = r.getFloat("surface_grain");
    const int stoneFreq = noise::frequency(lerp(120.0f, 35.0f, r.getFloat("grain_size")));
    const float wear = ctx.wearAmount();
    const float paintWear = r.getFloat("paint_wear") * wear;
    const float chips = r.getFloat("chips") * wear;
    const float dirt = r.getFloat("dirt") * wear;
    const float apply = ctx.applyWear() ? 1.0f : 0.0f;

    const features::Domain d{false, ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const Vec3 dirtColor = srgbToLinear(Vec3{0.42f, 0.41f, 0.39f});

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        const float px = u - 0.5f, py = v - 0.5f;
        // Hand-applied edges are never perfectly straight.
        const float edgeNoise = 0.0025f * noise::fbm(u * 30.0f, v * 30.0f, d.period(30), layer(kEdge), 3);
        const float sd = (continuous ? std::abs(px) - halfW : roundedBox(px, py, halfW, halfL, radius)) + edgeNoise;

        const float soft = d.aa() + 0.004f * softness;
        float cover = 1.0f - smoothstep(-soft, soft, sd);
        // Overspray: speckles just outside the edge.
        if (softness > 0.0f && sd > 0.0f) {
            const int sf = 400;
            const float speck = noise::value(u * sf, v * sf, d.period(sf), layer(kSpray));
            const float zone = 1.0f - smoothstep(0.0f, 0.012f * softness, sd);
            cover = std::max(cover, smoothstep(0.82f, 0.9f, speck) * zone);
        }

        // The surface below: paint misses the gaps between stones and thins on their tops.
        const features::StoneSample st = features::stones(u, v, stoneFreq, layer(kStones), 0.9f, 0.04f, 0.7f, d);
        const float gapHole = grain * (1.0f - st.coverage) * 0.65f;

        // Wear: patches of worn paint (tops first), chips.
        const float wornPatch = features::patchyMask(u, v, saturate(paintWear * 1.2f), 4, layer(kWearMask), d);
        const float worn = saturate(wornPatch * (0.5f + 0.8f * st.dome));
        const float chip = features::blobs(u, v, 40, layer(kChips), saturate(chips) * 0.6f, 0.002f, 0.008f, 0.3f, d);
        const float dirtMask = dirt * noise::fbm01(u * 12.0f, v * 12.0f, d.period(12), layer(kDirt), 4);

        const float paintAmount = cover * coverage * (1.0f - gapHole) * (1.0f - apply * worn * 0.9f) * (1.0f - apply * chip);

        const float tone = noise::fbm01(u * 20.0f, v * 20.0f, d.period(20), layer(kTone), 3);
        Vec3 col = paint * (0.94f + 0.06f * tone);
        col = lerp(col, dirtColor, apply * dirtMask * 0.6f);

        out.albedo.at(x, y, 0) = col.x;
        out.albedo.at(x, y, 1) = col.y;
        out.albedo.at(x, y, 2) = col.z;
        out.height.at(x, y) = saturate(0.4f + 0.12f * st.dome * grain + 0.3f * thickness * paintAmount);
        out.roughness.at(x, y) = saturate(lerp(0.5f, 0.75f, apply * std::max(worn, dirtMask)) + 0.05f * tone);
        out.opacity.at(x, y) = saturate(paintAmount);
        out.wear.at(x, y) = ctx.wearEnabled() ? saturate(cover * std::max({worn, chip, dirtMask})) : 0.0f;
    });
}

} // namespace md
