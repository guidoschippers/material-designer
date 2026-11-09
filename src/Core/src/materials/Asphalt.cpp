#include "materials/Asphalt.h"

#include "materials/Features.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace md {

namespace {

using noise::fbm;
using noise::fbm01;
using noise::Period;
using noise::randomFromId;

/// Seed layers (each feature gets an independent random stream).
enum Layer : std::uint32_t {
    kCoarse = 1, kFine, kBinder, kSand, kCrack, kHairline, kHairlineMask, kPatch,
    kRavel, kOil, kTrack, kAlligator, kAlligatorMask, kSpeckle,
};

/// Typical gravel colours (sRGB). Stones deviate from the user's base colour towards these.
constexpr std::array<Vec3, 7> kPalette = {{
    {0.54f, 0.54f, 0.53f}, // grey
    {0.66f, 0.59f, 0.49f}, // beige
    {0.81f, 0.78f, 0.73f}, // white quartz
    {0.49f, 0.40f, 0.31f}, // brown
    {0.33f, 0.33f, 0.32f}, // dark grey
    {0.58f, 0.41f, 0.35f}, // reddish
    {0.48f, 0.50f, 0.53f}, // blue-grey
}};
constexpr std::array<float, 7> kPaletteWeights = {0.30f, 0.20f, 0.10f, 0.12f, 0.15f, 0.05f, 0.08f};

Vec3 paletteMean()
{
    Vec3 m;
    for (std::size_t i = 0; i < kPalette.size(); ++i) m = m + kPalette[i] * kPaletteWeights[i];
    return m;
}

struct Params {
    int coarseFreq = 0, fineFreq = 0;
    float presence = 0, gap = 0, roundness = 0, exposure = 0, fine = 0, colorVariation = 0;
    Vec3 aggregateSrgb;
    Vec3 binderLin;
    float cracks = 0, hairline = 0, patches = 0;

    bool wearOn = false;
    float applyFactor = 0; // 1 when wear is applied to the base maps
    float wear = 0;
    float wearCracks = 0, raveling = 0, oil = 0, tracks = 0;
    bool tracksVertical = true;
};

struct Sample {
    Vec3 albedo;
    float height = 0, roughness = 0, wear = 0;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    p.coarseFreq = noise::frequency(lerp(120.0f, 35.0f, r.getFloat("aggregate_size")));
    p.fineFreq = noise::frequency(static_cast<float>(p.coarseFreq) * 2.3f);
    const float density = r.getFloat("aggregate_density");
    p.presence = lerp(0.6f, 1.0f, density);
    p.gap = lerp(0.16f, 0.012f, density);
    p.roundness = r.getFloat("aggregate_roundness");
    p.exposure = r.getFloat("aggregate_exposure");
    p.fine = r.getFloat("fine_aggregate");
    p.colorVariation = r.getFloat("aggregate_color_variation");
    const Color c = r.getColor("aggregate_color");
    p.aggregateSrgb = {c.r, c.g, c.b};

    const float darkness = r.getFloat("binder_darkness");
    const Vec3 aged = srgbToLinear(Vec3{0.42f, 0.41f, 0.39f});
    const Vec3 fresh = srgbToLinear(Vec3{0.10f, 0.10f, 0.10f});
    p.binderLin = lerp(aged, fresh, darkness);

    p.cracks = r.getFloat("cracks");
    p.hairline = r.getFloat("hairline_cracks");
    p.patches = r.getFloat("patches");

    p.wearOn = ctx.wearEnabled();
    p.applyFactor = ctx.applyWear() ? 1.0f : 0.0f;
    p.wear = ctx.wearAmount();
    p.wearCracks = r.getFloat("wear_cracks");
    p.raveling = r.getFloat("raveling");
    p.oil = r.getFloat("oil_stains");
    p.tracks = r.getFloat("tyre_tracks");
    p.tracksVertical = r.getEnum("track_direction") == "vertical";
    return p;
}

/// Linear colour of one stone, derived from its id.
Vec3 stoneColor(const Params& p, std::uint32_t id)
{
    static const Vec3 mean = paletteMean();
    float r = randomFromId(id, 10);
    std::size_t idx = 0;
    for (; idx + 1 < kPalette.size(); ++idx) {
        if (r < kPaletteWeights[idx]) break;
        r -= kPaletteWeights[idx];
    }
    Vec3 c = p.aggregateSrgb + (kPalette[idx] - mean) * (1.3f * p.colorVariation);
    const float brightness = 1.0f + (randomFromId(id, 11) - 0.5f) * (0.15f + 0.35f * p.colorVariation);
    return srgbToLinear(saturate(c * brightness));
}

struct CrackSample {
    float line = 0.0f;    ///< Crack opening.
    float crumble = 0.0f; ///< Zone around the crack where stones break out.
};

/// A few long meandering cracks (horizontal or vertical) with varying width.
CrackSample meanderingCracks(const Params& p, const features::Domain& d, std::uint32_t seed, float u, float v,
                             float widthScale)
{
    CrackSample out;
    if (p.cracks <= 0.0f) {
        return out;
    }
    const float count = p.cracks * 4.0f;
    const int n = static_cast<int>(std::ceil(count));
    for (int i = 0; i < n; ++i) {
        const std::uint32_t id = noise::hash32(seed + static_cast<std::uint32_t>(i) * 7919U);
        const float fadeIn = saturate(count - static_cast<float>(i));
        const bool horizontal = randomFromId(id, 0) < 0.5f;
        const float a = horizontal ? u : v; // along the crack
        const float b = horizontal ? v : u; // across the crack
        const float c = randomFromId(id, 1);
        const float lane = randomFromId(id, 2) * 50.0f; // decorrelates cracks (constant across a)
        constexpr int fq = 3;
        const Period pa{d.seamless ? fq : 0, 0};
        auto curve = [&](float t) { return c + 0.06f * fbm(t * fq, lane, pa, id, 5); };

        const float eps = 0.002f;
        const float slope = (curve(a + eps) - curve(a - eps)) / (2.0f * eps);
        const float dist = std::abs(periodicDelta(b, curve(a), d.seamless)) / std::sqrt(1.0f + slope * slope);

        // Cracks do not always run the full length of the tile.
        const Period ps{d.seamless ? 2 : 0, 0};
        const float segment = smoothstep(0.32f, 0.45f, fbm01(a * 2.0f, lane + 7.0f, ps, id ^ 0x5bd1e995U, 3));
        const Period pw{d.seamless ? 40 : 0, 0};
        const float width = 0.0026f * (0.4f + 1.2f * noise::value(a * 40.0f, lane, pw, id ^ 0x27d4eb2dU)) * widthScale;

        const float line = (1.0f - smoothstep(width * 0.5f, width * 0.5f + d.aa(), dist)) * segment * fadeIn;
        const float crumble = (1.0f - smoothstep(width, width * 4.0f, dist)) * segment * fadeIn;
        out.line = std::max(out.line, line);
        out.crumble = std::max(out.crumble, crumble);
    }
    return out;
}

Sample evaluate(const Params& p, const features::Domain& d, std::uint32_t seed, float u, float v)
{
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };

    // --- Wear masks (computed whenever wear is enabled, applied only with applyFactor) ---
    float track = 0.0f, oil = 0.0f, ravelMask = 0.0f, alligator = 0.0f;
    if (p.wearOn) {
        const float across = p.tracksVertical ? u : v;
        const float along = p.tracksVertical ? v : u;
        const Period pa{d.seamless ? 3 : 0, 0};
        const float wobble = 0.02f * fbm(along * 3.0f, 0.5f, pa, layer(kTrack), 3);
        const float variation = 0.7f + 0.3f * fbm01(along * 3.0f, 9.5f, pa, layer(kTrack) ^ 1U, 3);
        track = features::bands(across + wobble, 2, 0.25f, 0.085f, 0.7f) * saturate(p.tracks * p.wear * 1.4f) * variation;

        const float laneCentre = features::bands(across + wobble, 2, 0.0f, 0.22f, 1.0f);
        oil = features::blobs(u, v, 6, layer(kOil), saturate(p.oil * p.wear) * 0.7f, 0.015f, 0.06f, 0.6f, d) *
              (0.35f + 0.65f * laneCentre);

        ravelMask = features::patchyMask(u, v, saturate(p.raveling * p.wear * 0.7f), 4, layer(kRavel), d);
        alligator = features::crackNetwork(u, v, 10, layer(kAlligator), 0.0018f, 0.3f, d) *
                    features::patchyMask(u, v, saturate(p.wearCracks * p.wear), 3, layer(kAlligatorMask), d);
    }
    const float aw = p.applyFactor;

    // --- Cracks ---
    const CrackSample crack =
        meanderingCracks(p, d, layer(kCrack), u, v, 1.0f + 1.5f * saturate(p.wearCracks * p.wear) * aw);
    float hairline = 0.0f;
    if (p.hairline > 0.0f) {
        hairline = features::crackNetwork(u, v, 7, layer(kHairline), 0.0010f, 0.4f, d) *
                   features::patchyMask(u, v, p.hairline, 3, layer(kHairlineMask), d);
    }

    // --- Repairs ---
    const features::RectSample patch = features::rectangles(u, v, 3, layer(kPatch), p.patches * 0.8f, 0.05f, 0.15f, d);
    const float seam = patch.edgeDist < 1.0f ? 0.7f * (1.0f - smoothstep(0.0012f, 0.0012f + d.aa(), patch.edgeDist)) : 0.0f;

    // --- Binder ---
    const float mottle = fbm01(u * 8.0f, v * 8.0f, d.period(8), layer(kBinder), 5);
    const int sandFreq = p.fineFreq * 3;
    const float sand = noise::value(u * sandFreq, v * sandFreq, d.period(sandFreq), layer(kSand));
    const float sandSpeck = smoothstep(0.62f, 0.8f, sand) * (0.4f + 0.6f * p.fine);
    Vec3 binder = p.binderLin * (0.8f + 0.4f * mottle);
    binder = lerp(binder, srgbToLinear(Vec3{0.50f, 0.48f, 0.45f}), sandSpeck * 0.5f);
    binder = lerp(binder, srgbToLinear(Vec3{0.08f, 0.08f, 0.08f}), patch.inside * 0.7f);
    binder = lerp(binder, binder * 1.7f + Vec3{0.015f, 0.015f, 0.015f}, track * 0.5f * aw); // faded wheel paths
    binder = lerp(binder, binder * 1.3f + Vec3{0.01f, 0.01f, 0.01f}, p.wear * 0.4f * aw);   // oxidation

    float exposure = p.exposure * (1.0f - 0.6f * patch.inside);
    exposure = lerp(exposure, 1.0f, track * 0.5f * aw);

    // --- Stones ---
    features::StoneSample coarse =
        features::stones(u, v, p.coarseFreq, layer(kCoarse), p.presence, p.gap, p.roundness, d);
    features::StoneSample fine =
        features::stones(u, v, p.fineFreq, layer(kFine), p.fine * 0.95f, 0.06f, p.roundness, d);

    // Lost stones: crumbling along cracks and raveling.
    float pit = 0.0f;
    if (coarse.coverage > 0.0f) {
        const bool crumbled = crack.crumble > 0.15f + 0.85f * randomFromId(coarse.id, 8);
        const bool raveled = ravelMask * aw * 0.55f > randomFromId(coarse.id, 7);
        if (crumbled || raveled) {
            pit = coarse.coverage;
            coarse = {};
        }
    }
    if (fine.coverage > 0.0f && crack.crumble > 0.3f + 0.7f * randomFromId(fine.id, 8)) {
        fine = {};
    }

    // Mineral texture inside the stones.
    const int sf = p.coarseFreq * 4;
    const float speckle = fbm01(u * sf, v * sf, d.period(sf), layer(kSpeckle), 3);
    Vec3 col = binder;
    float h = 0.32f + 0.04f * mottle + 0.02f * sandSpeck;
    float rough = 0.9f - 0.05f * mottle;

    if (fine.coverage > 0.0f) {
        const Vec3 fc = stoneColor(p, fine.id) * (0.9f * (0.8f + 0.4f * speckle));
        const Vec3 visible = lerp(binder, fc, exposure * (0.25f + 0.75f * smoothstep(0.0f, 0.8f, fine.dome)));
        col = lerp(col, visible, fine.coverage);
        const float hf = 0.35f + 0.12f * fine.dome * (0.7f + 0.3f * randomFromId(fine.id, 3));
        h = lerp(h, std::max(h, hf), fine.coverage);
        rough = lerp(rough, 0.7f + 0.15f * randomFromId(fine.id, 4), fine.coverage);
    }
    if (coarse.coverage > 0.0f) {
        const float grain = 0.78f + 0.44f * speckle;
        const Vec3 cc = stoneColor(p, coarse.id) * grain;
        const Vec3 visible = lerp(binder, cc, exposure * (0.2f + 0.8f * smoothstep(0.0f, 0.85f, coarse.dome)));
        col = lerp(col, visible, coarse.coverage);
        const float hc = 0.40f + 0.30f * coarse.dome * (0.6f + 0.4f * randomFromId(coarse.id, 3));
        h = lerp(h, std::max(h, hc), coarse.coverage);
        rough = lerp(rough, 0.6f + 0.2f * randomFromId(coarse.id, 4), coarse.coverage);
    }
    if (pit > 0.0f) {
        col = lerp(col, binder * 0.55f, pit);
        h -= 0.16f * pit;
        rough = lerp(rough, 0.93f, pit);
    }

    // Repairs: fresh binder is a bit smoother; seams are glossy tar.
    rough = lerp(rough, rough * 0.85f, patch.inside);
    if (seam > 0.0f) {
        col = lerp(col, srgbToLinear(Vec3{0.08f, 0.08f, 0.08f}), seam);
        h = lerp(h, std::max(h, 0.36f), seam);
        rough = lerp(rough, 0.35f, seam);
    }

    // Cracks.
    const float crackLine = std::max({crack.line, hairline * 0.7f, alligator * aw});
    col = lerp(col, col * 0.12f, crackLine);
    h = lerp(h, 0.06f, std::max(crack.line, alligator * aw) + hairline * 0.4f * (1.0f - crack.line));
    rough = lerp(rough, 0.95f, crackLine);

    // Oil stains: darker, slightly brownish and much smoother.
    const float oilApplied = oil * aw;
    col = lerp(col, col * Vec3{0.38f, 0.35f, 0.32f}, oilApplied);
    rough = lerp(rough, 0.28f, oilApplied);

    // Polished wheel paths and slight rutting.
    rough *= 1.0f - 0.35f * track * aw;
    h -= 0.04f * track * aw;

    Sample s;
    s.albedo = saturate(col);
    s.height = saturate(h);
    s.roughness = saturate(rough);
    s.wear = saturate(std::max({track * 0.7f, oil, ravelMask * (pit > 0.0f ? 1.0f : 0.5f), alligator}));
    return s;
}

} // namespace

AsphaltMaterial::AsphaltMaterial()
{
    auto& s = schema_;
    s.addFloat("aggregate_size", "Stone size", 0.5, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Size of the gravel stones. 0 = fine chippings, 1 = coarse gravel.");
    s.addFloat("aggregate_density", "Stone density", 0.85, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("How densely the stones are packed. Low values show more binder between the stones.");
    s.addFloat("aggregate_roundness", "Roundness", 0.75, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Stone shape: 0 = angular crushed stone, 1 = rounded river gravel.");
    s.addFloat("fine_aggregate", "Fine grit", 0.6, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Amount of small grit and sand between the larger stones.");
    s.addFloat("aggregate_exposure", "Exposure", 0.85, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("How much of the stones' own colour shows through the binder film. Fresh asphalt is low, worn asphalt high.");
    s.addColor("aggregate_color", "Stone colour", Color{0.60f, 0.57f, 0.53f})
        .inGroup("Aggregate")
        .tip("Average colour of the stones (sRGB).");
    s.addFloat("aggregate_color_variation", "Colour variation", 0.6, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Spread of stone colours towards grey, beige, white, brown and reddish gravel.");

    s.addFloat("binder_darkness", "Binder darkness", 0.6, 0.0, 1.0)
        .inGroup("Binder")
        .tip("Colour of the bitumen binder: 0 = faded grey, 1 = fresh black.");

    s.addFloat("cracks", "Cracks", 0.2, 0.0, 1.0)
        .inGroup("Damage")
        .tip("Long meandering cracks with crumbled edges. Higher values add more cracks.");
    s.addFloat("hairline_cracks", "Hairline cracks", 0.15, 0.0, 1.0)
        .inGroup("Damage")
        .tip("Fine network of hairline cracks in patches.");
    s.addFloat("patches", "Repairs", 0.15, 0.0, 1.0)
        .inGroup("Damage")
        .tip("Rectangular repairs with fresh, binder-rich asphalt and glossy tar seams.");

    s.addFloat("wear_cracks", "Crumbling cracks", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Widens the cracks and adds alligator cracking in patches.");
    s.addFloat("raveling", "Raveling", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Loss of stones, leaving dark pits in the binder.");
    s.addFloat("oil_stains", "Oil stains", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Dark, glossy oil stains, mostly in the middle of the lane.");
    s.addFloat("tyre_tracks", "Tyre tracks", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Faded, polished wheel paths with slight rutting.");
    s.addEnum("track_direction", "Track direction", {{"vertical", "Vertical"}, {"horizontal", "Horizontal"}}, "vertical")
        .inGroup("Wear")
        .wear()
        .tip("Direction in which traffic drives over the texture.");
}

void AsphaltMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const features::Domain d{ctx.seamless(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        const Sample s = evaluate(p, d, seed, u, v);
        out.albedo.at(x, y, 0) = s.albedo.x;
        out.albedo.at(x, y, 1) = s.albedo.y;
        out.albedo.at(x, y, 2) = s.albedo.z;
        out.height.at(x, y) = s.height;
        out.roughness.at(x, y) = s.roughness;
        out.wear.at(x, y) = s.wear;
    });
}

} // namespace md
