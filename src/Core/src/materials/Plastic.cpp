#include "materials/Plastic.h"

#include "materials/Features.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

using features::Domain;
using noise::fbm;
using noise::fbm01;
using noise::randomFromId;

// Layer values seed the noise of each feature: keep them stable so presets reproduce.
enum Layer : std::uint32_t {
    kMottle = 1, kEdm, kFlow = 5, kFlowMask, kSink, kScratches, kScuff, kScuffLines, kPolish,
    kDust, kDustSpecks, kGrime, kYellow, kDings,
};

/// Thin random scratches, clustered where things were handled or slid over the surface: mostly
/// short faint hairlines, a few longer and deeper ones. Returns intensity in [0, 1].
float scratchField(float u, float v, float amount, std::uint32_t seed, const Domain& d)
{
    if (amount <= 0.0f) {
        return 0.0f;
    }
    constexpr int cells = 10;
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    const float cs = 1.0f / cells;
    float best = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t cell = features::cellId(gx, gy, cells, seed, d);
            // Clustering: the scratch density varies smoothly from cell to cell (value noise on a
            // grid of half the resolution, periodic with the cells).
            const float density = noise::value(static_cast<float>(gx) * 0.5f, static_cast<float>(gy) * 0.5f,
                                               d.period(cells / 2), seed ^ 0x85ebca6bU);
            const float keep = amount * smoothstep(0.3f, 0.7f, density) * 1.6f;
            for (std::uint32_t k = 0; k < 4; ++k) {
                const std::uint32_t id = noise::hash32(cell + k * 0x9e3779b9U);
                if (randomFromId(id, 0) >= keep - static_cast<float>(k) * 0.25f) {
                    continue;
                }
                const Vec2 c{(static_cast<float>(gx) + randomFromId(id, 1)) * cs,
                             (static_cast<float>(gy) + randomFromId(id, 2)) * cs};
                const float angle = randomFromId(id, 3) * kPi;
                const float rl = randomFromId(id, 4);
                const float len = cs * (0.08f + 0.85f * rl * rl);
                const Vec2 dir{std::cos(angle) * len * 0.5f, std::sin(angle) * len * 0.5f};
                const float dist = features::segmentDistance({u, v}, c - dir, c + dir);
                const float width = std::max(0.00025f + 0.0004f * randomFromId(id, 5), 0.5f * d.pixel);
                const float s = (1.0f - smoothstep(width, width + d.aa(), dist)) * (0.2f + 0.8f * rl * randomFromId(id, 6));
                best = std::max(best, s);
            }
        }
    }
    return best;
}

/// Spark-eroded (EDM) texture: densely overlapping shallow craters with sharp rims, two scales.
/// Returns the crater depth in [0, 1] (0 on the rims, 1 at the deepest crater floors).
float edmTexture(float u, float v, int freq, std::uint32_t seed, const Domain& d)
{
    auto craters = [&](int f, std::uint32_t s) {
        const auto c = noise::voronoi(u * f, v * f, d.period(f), s, 1.0f);
        const float bowl = saturate(1.0f - c.f1 * c.f1 * 2.4f);
        return bowl * (0.45f + 0.55f * randomFromId(c.id, 1));
    };
    const int f2 = freq * 2 + 1;
    // Finest grain: faded to its mean once it gets smaller than a few pixels (it would alias).
    const int f4 = freq * 4;
    const float fineDetail = smoothstep(1.5f * d.pixel, 4.0f * d.pixel, 1.0f / static_cast<float>(f4));
    const float grain = fineDetail > 0.0f ? noise::value(u * f4, v * f4, d.period(f4), seed ^ 0x3c6ef372U) : 0.5f;
    return saturate(0.6f * craters(freq, seed) + 0.3f * craters(f2, seed ^ 0x9e3779b9U) +
                    0.1f * lerp(0.5f, grain, fineDetail));
}

struct Params {
    Vec3 color;
    float colorVariation = 0, texture = 0, textureDepth = 0, roughMin = 0, roughMax = 0, flowLines = 0, sinkMarks = 0;
    int textureFreq = 1;
    float scratches = 0, polish = 0, dust = 0, grime = 0, yellowing = 0, dings = 0;
    Vec3 yellowTint;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    const Color c = r.getColor("color");
    p.color = srgbToLinear(Vec3{c.r, c.g, c.b});
    p.colorVariation = r.getFloat("color_variation");
    p.texture = r.getFloat("texture");
    p.textureFreq = noise::frequency(1.0f / r.getFloat("texture_scale"));
    p.textureDepth = r.getFloat("texture_depth");
    p.roughMin = r.getFloat("roughness_min");
    p.roughMax = std::max(p.roughMin, r.getFloat("roughness_max"));
    p.flowLines = r.getFloat("flow_lines");
    p.sinkMarks = r.getFloat("sink_marks");
    const float w = ctx.wearAmount();
    p.scratches = r.getFloat("scratches") * w;
    p.polish = r.getFloat("hand_polish") * w;
    p.dust = r.getFloat("dust") * w;
    p.grime = r.getFloat("grime") * w;
    p.yellowing = r.getFloat("yellowing") * w;
    const Color t = r.getColor("yellowing_tint");
    p.yellowTint = srgbToLinear(Vec3{t.r, t.g, t.b});
    p.dings = r.getFloat("dings") * w;
    return p;
}

} // namespace

PlasticMaterial::PlasticMaterial()
{
    auto& s = schema_;
    s.addColor("color", "Colour", Color{0.74f, 0.73f, 0.71f})
        .inGroup("Plastic")
        .tip("Colour of the plastic (sRGB). Light console grey by default; use e.g. #1c1c1e for black VCR plastic.");
    s.addFloat("color_variation", "Colour variation", 0.2, 0.0, 1.0)
        .inGroup("Plastic")
        .tip("Faint cloudy variation in tone (moulded plastic is nearly uniform).");

    s.addFloat("texture", "Surface texture", 0.8, 0.0, 1.0)
        .inGroup("Finish")
        .tip("0 = smooth, glossy plastic; 1 = the fine spark-eroded (EDM) matte texture of moulded housings.");
    s.addFloat("texture_scale", "Texture scale", 0.004, 0.001, 0.03)
        .inGroup("Finish")
        .tip("Size of the spark-eroded grain as a fraction of the texture (rounded so the texture tiles).");
    s.addFloat("texture_depth", "Texture depth", 0.5, 0.0, 1.0)
        .inGroup("Finish")
        .tip("Relief of the spark-eroded grain in the height and normal maps.");
    s.addFloat("roughness_min", "Roughness (glossy)", 0.18, 0.0, 1.0)
        .inGroup("Finish")
        .tip("Roughness of the smooth plastic, reached with surface texture 0 (and where hands polished it).");
    s.addFloat("roughness_max", "Roughness (textured)", 0.62, 0.0, 1.0)
        .inGroup("Finish")
        .tip("Roughness of the fully spark-eroded texture.");
    s.addFloat("flow_lines", "Flow lines", 0.0, 0.0, 1.0)
        .inGroup("Moulding")
        .tip("Faint wavy lines where the molten plastic flowed into the mould (gloss and tone).");
    s.addFloat("sink_marks", "Sink marks", 0.0, 0.0, 1.0)
        .inGroup("Moulding")
        .tip("Shallow, soft depressions over internal ribs and bosses (mostly visible in reflections).");

    s.addFloat("scratches", "Scratches & scuffs", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Fine scratches and scuffed patches: lighter (stress-whitened) on dark plastic, greyish on light plastic.");
    s.addFloat("hand_polish", "Hand polish", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Areas worn glossy by hands: the texture is flattened and the roughness drops.");
    s.addFloat("dust", "Dust", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Light grey matte dust film with specks, thicker in low areas.");
    s.addFloat("grime", "Grime", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Dark greasy grime collecting in the texture, scratches and dings.");
    s.addFloat("yellowing", "UV yellowing", 0.0, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Yellowing of aged (light) plastic from UV light and flame retardants; hardly visible on black plastic.");
    s.addColor("yellowing_tint", "Yellowing tint", Color{0.80f, 0.72f, 0.50f})
        .inGroup("Wear")
        .wear()
        .tip("Hue of fully yellowed plastic (sRGB); the lightness of the base colour is kept.");
    s.addFloat("dings", "Dings", 0.2, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Small dents and nicks with a stress-whitened rim.");
}

void PlasticMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const bool wearOn = ctx.wearEnabled();
    const float aw = ctx.applyWear() ? 1.0f : 0.0f;

    // Grain smaller than a few pixels cannot be shown in relief; it only remains in the roughness.
    const float grainSize = 1.0f / static_cast<float>(p.textureFreq);
    const float grainDetail = smoothstep(1.5f * d.pixel, 4.0f * d.pixel, grainSize);
    const float baseLum = luminance(p.color);
    const float darkness = 1.0f - smoothstep(0.05f, 0.3f, baseLum); // 1 for black plastic
    const Vec3 dustColor = srgbToLinear(Vec3{0.58f, 0.56f, 0.53f});
    const Vec3 grimeColor = srgbToLinear(Vec3{0.16f, 0.14f, 0.12f});
    const float tintLum = std::max(luminance(p.yellowTint), 1e-4f);

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        const float mottle = fbm(u * 4.0f, v * 4.0f, d.period(4), layer(kMottle), 4);
        Vec3 col = p.color * (1.0f + 0.06f * p.colorVariation * mottle);
        float h = 0.6f; // height without the crater relief (added at the end)

        // --- Spark-eroded texture ---
        float crater = 0.0f;
        if (p.texture > 0.0f) {
            crater = edmTexture(u, v, p.textureFreq, layer(kEdm), d);
        }
        float rough = lerp(p.roughMin, p.roughMax, p.texture * (0.85f + 0.15f * crater)) + 0.02f * mottle;

        // --- Moulding defects ---
        if (p.flowLines > 0.0f) {
            // Wavy, roughly parallel bands (as from a gate at one side of the part).
            const float warpV = 0.1f * fbm(u * 2.0f, v * 2.0f, d.period(2), layer(kFlow), 3) +
                                0.008f * fbm(u * 12.0f, v * 12.0f, d.period(12), layer(kFlow) ^ 0x51ed270bU, 2);
            const float lines = std::pow(0.5f + 0.5f * std::cos(kTwoPi * 22.0f * (v + warpV)), 4.0f);
            const float mask = smoothstep(0.35f, 0.65f, fbm01(u * 2.0f, v * 2.0f, d.period(2), layer(kFlowMask), 2));
            const float strength = 0.4f + 0.6f * fbm01(u * 6.0f, v * 6.0f, d.period(6), layer(kFlowMask) ^ 0x2545f491U, 2);
            const float flow = p.flowLines * lines * mask * strength;
            col = col * (1.0f - 0.06f * flow);
            rough = lerp(rough, std::min(1.0f, rough + 0.15f), flow);
            h -= 0.01f * flow;
        }
        if (p.sinkMarks > 0.0f) {
            const float sink = features::blobs(u, v, 4, layer(kSink), p.sinkMarks * 0.6f, 0.03f, 0.08f, 1.0f, d);
            h -= 0.12f * p.sinkMarks * sink;
        }

        // --- Wear features ---
        float scratch = 0.0f, scuff = 0.0f, polish = 0.0f, dust = 0.0f, grime = 0.0f, ding = 0.0f, yellow = 0.0f;
        if (wearOn) {
            scratch = scratchField(u, v, p.scratches, layer(kScratches), d);
            if (p.scratches > 0.0f) {
                // Scuffs: patches of dense micro-scratches, mostly in one direction.
                const float patchA = features::patchyMask(u, v, p.scratches * 0.7f, 4, layer(kScuff), d);
                const float patchB = features::patchyMask(u, v, p.scratches * 0.7f, 4, layer(kScuff) ^ 0x27d4eb2fU, d);
                const float linesA = fbm01((u + v) * 6.0f, (u - v) * 240.0f, d.period(6, 240), layer(kScuffLines), 2);
                const float linesB = fbm01((u - v) * 6.0f, (u + v) * 240.0f, d.period(6, 240), layer(kScuffLines) ^ 0x165667b1U, 2);
                scuff = std::max(patchA * smoothstep(0.55f, 0.8f, linesA), patchB * smoothstep(0.55f, 0.8f, linesB));
            }
            if (p.polish > 0.0f) {
                // Soft-edged areas where hands rest; graded, never a hard boundary.
                const float m = fbm01(u * 3.0f, v * 3.0f, d.period(3), layer(kPolish), 4);
                const float threshold = lerp(0.7f, 0.3f, saturate(p.polish));
                polish = saturate(p.polish * 1.4f) * smoothstep(threshold - 0.15f, threshold + 0.3f, m);
            }
            if (p.dust > 0.0f) {
                const float film = 0.45f + 0.55f * smoothstep(0.2f, 0.75f, fbm01(u * 4.0f, v * 4.0f, d.period(4), layer(kDust), 4));
                const float specks = features::blobs(u, v, 90, layer(kDustSpecks), p.dust * 0.4f, 0.0006f, 0.0016f, 0.6f, d);
                dust = saturate(p.dust * film + specks * std::min(1.0f, 2.0f * p.dust));
            }
            if (p.grime > 0.0f) {
                grime = p.grime * smoothstep(0.3f, 0.7f, fbm01(u * 5.0f, v * 5.0f, d.period(5), layer(kGrime), 4));
            }
            ding = features::blobs(u, v, 12, layer(kDings), p.dings * 0.25f, 0.003f, 0.009f, 0.7f, d);
            yellow = p.yellowing * (0.75f + 0.35f * fbm(u * 3.0f, v * 3.0f, d.period(3), layer(kYellow), 3));
        }

        // Relief: dings and scratches, then the craters (flattened where polished by hands).
        // The crater relief is faded out when the grain is too fine to show (grainDetail), but dust
        // and grime must still find the crater floors, so `low` is based on the unfaded relief:
        // otherwise the preview would lose the grime that the full-size export shows.
        h -= aw * (0.25f * ding + 0.04f * scratch + 0.01f * scuff);
        const float craterDepth = 0.3f * p.texture * p.textureDepth * (1.0f - 0.75f * aw * polish) * crater;
        const float low = saturate((0.6f - (h - craterDepth)) * 6.0f);
        h -= craterDepth * grainDetail;

        if (aw > 0.0f) {
            // UV yellowing: tint towards the yellowed hue at the same lightness, slightly darker.
            if (yellow > 0.0f) {
                const Vec3 yellowed = p.yellowTint * (luminance(col) / tintLum * 0.92f);
                col = lerp(col, yellowed, saturate(yellow));
            }
            // Hand polish: glossier, very slightly darker (skin grease).
            rough = lerp(rough, p.roughMin * 0.8f, 0.8f * polish);
            col = col * (1.0f - 0.04f * polish);
            // Scratches and scuffs: stress-whitened on dark plastic, greyish on light plastic.
            const float marks = std::max(scratch, 0.4f * scuff);
            const Vec3 marked = lerp(col * 0.82f + Vec3{0.02f, 0.02f, 0.02f}, col * 1.3f + Vec3{0.06f, 0.06f, 0.06f}, darkness);
            col = lerp(col, marked, marks);
            rough = lerp(rough, 0.45f, 0.7f * marks);
            // Dings: dented, rough, with a whitened rim.
            const float rim = saturate(4.0f * ding * (1.0f - ding));
            col = lerp(col, col * 1.2f + Vec3{0.05f, 0.05f, 0.05f}, 0.5f * rim);
            rough = lerp(rough, 0.6f, ding);
        }
        // Low areas (crater floors, dings, scratches, sink marks) collect grime and dust.
        if (aw > 0.0f) {
            col = lerp(col, grimeColor, 0.7f * grime * smoothstep(0.1f, 0.8f, low));
            col = lerp(col, dustColor, 0.55f * dust * (0.4f + 0.6f * low));
            rough = lerp(rough, 0.92f, dust);
        }

        out.albedo.at(x, y, 0) = saturate(col.x);
        out.albedo.at(x, y, 1) = saturate(col.y);
        out.albedo.at(x, y, 2) = saturate(col.z);
        out.height.at(x, y) = saturate(h);
        out.roughness.at(x, y) = saturate(rough);
        out.wear.at(x, y) = saturate(
            std::max({scratch, 0.6f * scuff, 0.5f * polish, 0.7f * dust, 0.7f * grime * low, ding, 0.3f * yellow}));
    });
}

} // namespace md
