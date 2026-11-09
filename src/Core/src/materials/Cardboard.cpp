#include "materials/Cardboard.h"

#include "materials/Features.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

using features::Domain;
using noise::fbm;
using noise::fbm01;
using noise::randomFromId;

enum Layer : std::uint32_t {
    kTone = 1, kHue, kFormation, kSurface, kFibres, kFibresFine, kSpecks, kFluteWarp, kFluteVis,
    kScuff, kScuffStreak, kFuzz, kCreases, kCreaseWobble, kDents, kStains, kCockle, kDirt,
    kTape, kTapeEdge, kTapeResidue, kTapeTear,
};

/// Unwrapped cell index -> stable cell id (wraps on seamless axes).
std::uint32_t cellId(int gx, int gy, int cells, std::uint32_t seed, const Domain& d)
{
    return noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0), noise::wrapIndex(gy, d.seamlessV ? cells : 0),
                           seed);
}

/// Short straight paper fibres, `density` (0..2) per cell on a grid of `freq` cells per tile
/// edge, mostly lying along u (the machine direction of the liner). Returns a signed tone in
/// [-3, 1]: < 0 where a dark (unbleached/recycled) fibre or shive lies, > 0 for a light one.
float fibres(float u, float v, int freq, std::uint32_t seed, float density, const Domain& d)
{
    const float cell = 1.0f / static_cast<float>(freq);
    const int cx = static_cast<int>(std::floor(u * freq)), cy = static_cast<int>(std::floor(v * freq));
    const float halfWidth = 0.06f * cell;
    // Fibres thinner than a pixel fade out instead of aliasing.
    const float wEff = std::max(halfWidth, 0.5f * d.pixel);
    const float strength = halfWidth / wEff;
    const float reach = 1.4f * cell + wEff + d.pixel;
    float tone = 0.0f;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = cellId(gx, gy, freq, seed, d);
            for (std::uint32_t k = 0; k < 2; ++k) {
                const std::uint32_t r = k * 8;
                if (randomFromId(id, r) >= density - static_cast<float>(k)) {
                    continue;
                }
                const float px = (static_cast<float>(gx) + randomFromId(id, r + 1)) * cell;
                const float py = (static_cast<float>(gy) + randomFromId(id, r + 2)) * cell;
                const float ddx = u - px, ddy = v - py;
                if (ddx * ddx + ddy * ddy > reach * reach) {
                    continue;
                }
                // 80 % roughly along u, the rest in any direction.
                const float a = randomFromId(id, r + 3) < 0.8f ? (randomFromId(id, r + 4) - 0.5f) * 1.1f
                                                               : randomFromId(id, r + 4) * kPi;
                const float dirX = std::cos(a), dirY = std::sin(a);
                const float halfLen = (0.5f + 0.9f * randomFromId(id, r + 5)) * cell;
                const float t = ddx * dirX + ddy * dirY;
                const float tc = std::clamp(t, -halfLen, halfLen);
                const float ex = ddx - tc * dirX, ey = ddy - tc * dirY;
                const float dist = std::sqrt(ex * ex + ey * ey);
                const float cov = 1.0f - smoothstep(0.5f * wEff, wEff + d.pixel, dist);
                const float taper = 1.0f - smoothstep(0.6f, 1.0f, std::abs(t) / halfLen);
                // Mostly dark fibres; ~4 % are shives (dark woody slivers) that stand out strongly.
                const float kind = randomFromId(id, r + 6);
                const float amp = kind < 0.04f ? -3.0f : (kind < 0.62f ? -1.0f : 1.0f) * (0.5f + 0.5f * randomFromId(id, r + 7));
                tone += amp * cov * taper * strength;
            }
        }
    }
    return std::clamp(tone, -3.0f, 1.0f);
}

/// Fold creases: straight grooves of random direction, at most one per cell.
struct CreaseSample {
    float groove = 0.0f; ///< 1 in the fold line, sharp V profile.
    float near = 0.0f;   ///< Wide falloff around the fold (bent board, crushed flutes).
    float core = 0.0f;   ///< Thin line along the fold where the liner surface cracked.
};

CreaseSample creases(float u, float v, int cells, std::uint32_t seed, float probability, const Domain& d)
{
    CreaseSample out;
    if (probability <= 0.0f) {
        return out;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    // Folds are never perfectly straight.
    const float wobble = 0.004f * fbm(u * 8.0f, v * 8.0f, d.period(8), seed ^ 0x7a3c19e5U, 4);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = cellId(gx, gy, cells, seed, d);
            if (randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + randomFromId(id, 2)) * cellSize;
            const float a = randomFromId(id, 3) * kPi;
            const float dirX = std::cos(a), dirY = std::sin(a);
            const float halfLen = lerp(0.35f, 0.85f, randomFromId(id, 4)) * cellSize;
            const float halfWidth = std::max(lerp(0.005f, 0.012f, randomFromId(id, 5)), 1.5f * d.pixel);
            const float strength = 0.55f + 0.45f * randomFromId(id, 6);
            const float ddx = u - px, ddy = v - py;
            const float t = ddx * dirX + ddy * dirY;
            const float s = -ddx * dirY + ddy * dirX + wobble;
            const float te = t - std::clamp(t, -halfLen, halfLen);
            const float dist = std::sqrt(te * te + s * s);
            const float taper = 1.0f - smoothstep(0.55f, 1.0f, std::abs(t) / halfLen);
            const float v1 = saturate(1.0f - dist / halfWidth);
            const float groove = (0.65f * v1 * v1 + 0.35f * std::exp(-dist * dist / (4.0f * halfWidth * halfWidth))) *
                                 taper * strength;
            const float wide = 5.0f * halfWidth;
            const float nearV = std::exp(-dist * dist / (wide * wide)) * taper * strength;
            const float coreWidth = std::max(0.0008f, 0.75f * d.pixel);
            const float core = (1.0f - smoothstep(0.5f * coreWidth, coreWidth + d.pixel, dist)) * taper;
            out.groove = std::max(out.groove, groove);
            out.near = std::max(out.near, nearV);
            out.core = std::max(out.core, core);
        }
    }
    return out;
}

/// Dried water stains: a slightly darker area with a sharp, dark tide-mark ring at its edge and
/// sometimes a fainter inner ring from an earlier drying front.
struct StainSample {
    float inside = 0.0f; ///< Stained area, stronger towards the rim.
    float ring = 0.0f;   ///< Tide marks.
};

StainSample waterStains(float u, float v, int cells, std::uint32_t seed, float probability, const Domain& d)
{
    StainSample out;
    if (probability <= 0.0f) {
        return out;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    const int ef = cells * 5;
    const float edgeNoise = fbm(u * ef, v * ef, d.period(ef), seed ^ 0x2545f491U, 4);
    const int lf = cells * 2;
    const float lobes = fbm(u * lf, v * lf, d.period(lf), seed ^ 0x6c8e9cf5U, 2);
    const float ringWidth = std::max(0.0022f, 1.5f * d.pixel);
    auto ringAt = [&](float distUv) {
        // Sharp outer edge, softer towards the inside.
        return (1.0f - smoothstep(0.0f, ringWidth, distUv)) * smoothstep(-3.5f * ringWidth, 0.0f, distUv);
    };
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = cellId(gx, gy, cells, seed, d);
            if (randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + randomFromId(id, 2)) * cellSize;
            const float r = lerp(0.3f, 0.62f, randomFromId(id, 3)) * cellSize;
            const float stretch = 0.7f + 0.6f * randomFromId(id, 4);
            const float ddx = (u - px) * stretch, ddy = (v - py) / stretch;
            const float dist = std::sqrt(ddx * ddx + ddy * ddy) / r + 0.14f * edgeNoise + 0.3f * lobes;
            const float strength = 0.6f + 0.4f * randomFromId(id, 5);
            const float area = (1.0f - smoothstep(0.97f, 1.0f, dist)) * (0.45f + 0.55f * smoothstep(0.1f, 1.0f, dist));
            float ring = ringAt((dist - 1.0f) * r);
            if (randomFromId(id, 6) < 0.6f) {
                const float k = lerp(0.55f, 0.82f, randomFromId(id, 7));
                ring = std::max(ring, 0.55f * ringAt((dist - k) * r));
            }
            out.inside = std::max(out.inside, area * strength);
            out.ring = std::max(out.ring, ring * strength);
        }
    }
    return out;
}

/// Strips of parcel tape that were torn off: elongated rectangles along u or v with ragged ends.
float tapeStrips(float u, float v, int cells, std::uint32_t seed, float probability, const Domain& d)
{
    if (probability <= 0.0f) {
        return 0.0f;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    const float jitter = 0.0025f * fbm(u * 60.0f, v * 60.0f, d.period(60), seed ^ 0x68bc21ebU, 3);
    const float ragged = 0.012f * fbm01(u * 90.0f, v * 90.0f, d.period(90), seed ^ 0x1b873593U, 2);
    float best = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = cellId(gx, gy, cells, seed, d);
            if (randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + randomFromId(id, 2)) * cellSize;
            const bool alongU = randomFromId(id, 3) < 0.5f;
            const float halfLen = lerp(0.3f, 0.8f, randomFromId(id, 4)) * cellSize;
            const float halfWidth = lerp(0.028f, 0.045f, randomFromId(id, 5));
            const float along = alongU ? u - px : v - py, across = alongU ? v - py : u - px;
            const float sd = std::max(std::abs(along) - halfLen + ragged, std::abs(across) - halfWidth + jitter);
            best = std::max(best, 1.0f - smoothstep(-d.aa() * 0.5f, d.aa() * 0.5f, sd));
        }
    }
    return best;
}

struct Params {
    Vec3 paper;
    float colorVariation = 0, fibres = 0, specks = 0, roughness = 0;
    int flutes = 1;
    float fluteVisibility = 0;
    float scuffs = 0, creases = 0, dents = 0, stains = 0, dirt = 0, tape = 0;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    const Color c = r.getColor("paper_color");
    p.paper = srgbToLinear(Vec3{c.r, c.g, c.b});
    p.colorVariation = r.getFloat("color_variation");
    p.fibres = r.getFloat("fibres");
    p.specks = r.getFloat("recycled_specks");
    p.roughness = r.getFloat("roughness");
    p.flutes = noise::frequency(1.0f / r.getFloat("flute_spacing"));
    p.fluteVisibility = r.getFloat("flute_visibility");
    const float w = ctx.wearAmount();
    p.scuffs = r.getFloat("scuffs") * w;
    p.creases = r.getFloat("creases") * w;
    p.dents = r.getFloat("dents") * w;
    p.stains = r.getFloat("water_stains") * w;
    p.dirt = r.getFloat("dirt") * w;
    p.tape = r.getFloat("tape_residue") * w;
    return p;
}

} // namespace

CardboardMaterial::CardboardMaterial()
{
    auto& s = schema_;
    s.addColor("paper_color", "Paper colour", Color{0.64f, 0.48f, 0.32f})
        .inGroup("Paper")
        .tip("Colour of the kraft liner (sRGB). Typical brown kraft by default.");
    s.addFloat("color_variation", "Colour variation", 0.4, 0.0, 1.0)
        .inGroup("Paper")
        .tip("Cloudy variation in tone and hue, plus the fine mottling of the paper formation.");
    s.addFloat("fibres", "Fibres", 0.5, 0.0, 1.0)
        .inGroup("Paper")
        .tip("Visibility of fine light and dark paper fibres, mostly lying across the flutes (along U).");
    s.addFloat("recycled_specks", "Recycled specks", 0.4, 0.0, 1.0)
        .inGroup("Paper")
        .tip("Small dark specks and the odd coloured fleck from recycled paper.");
    s.addFloat("roughness", "Roughness", 0.86, 0.5, 1.0)
        .inGroup("Paper")
        .tip("Base roughness of the matte paper surface.");

    s.addFloat("flute_spacing", "Flute spacing", 0.02, 0.005, 0.1)
        .inGroup("Corrugation")
        .tip("Distance between flutes as a fraction of the texture (rounded so the texture tiles). "
             "The flutes run along V: each ridge is a vertical line, repeating across U.");
    s.addFloat("flute_visibility", "Flute visibility", 0.4, 0.0, 1.0)
        .inGroup("Corrugation")
        .tip("How strongly the flutes show through the liner as parallel ridges.");

    s.addFloat("scuffs", "Scuffs", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Abraded patches where the top of the liner is rubbed off: lighter, with fuzzy fibres.");
    s.addFloat("creases", "Creases", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Fold lines: dark grooves with crushed flutes around them.");
    s.addFloat("dents", "Dents", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Soft pressed-in dents where the flutes are crushed.");
    s.addFloat("water_stains", "Water stains", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Darker areas with tide-mark rings where water has soaked in and dried.");
    s.addFloat("dirt", "Dust & grime", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Greyish dust and grime, collecting in creases, dents and between the flutes.");
    s.addFloat("tape_residue", "Tape residue", 0.2, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Faint strips left by torn-off parcel tape: yellowish, slightly glossy adhesive and torn paper.");
}

void CardboardMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const bool wearOn = ctx.wearEnabled();
    const float aw = ctx.applyWear() ? 1.0f : 0.0f;

    const float fluteSpacing = 1.0f / static_cast<float>(p.flutes);
    // Flutes narrower than a few pixels would alias; fade them out instead.
    const float fluteFade = smoothstep(2.5f * d.pixel, 5.0f * d.pixel, fluteSpacing);
    const Vec3 dustColor = srgbToLinear(Vec3{0.40f, 0.37f, 0.33f});
    const Vec3 darkSpeck = srgbToLinear(Vec3{0.16f, 0.12f, 0.09f});
    const Vec3 stainTint{0.86f, 0.80f, 0.70f};
    const Vec3 ringTint{0.62f, 0.53f, 0.42f};

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        // --- Kraft liner: cloudy tone, hue drift and formation (mottling of fibre flocs) ---
        const float cloud = fbm(u * 3.0f, v * 3.0f, d.period(3), layer(kTone), 4);
        const float hue = fbm(u * 2.0f, v * 2.0f, d.period(2), layer(kHue), 3);
        const float formation = fbm(u * 48.0f, v * 48.0f, d.period(48), layer(kFormation), 3);
        const Vec3 hueScale = lerp(Vec3{0.97f, 0.99f, 1.06f}, Vec3{1.03f, 1.0f, 0.91f}, 0.5f + 0.5f * hue);
        Vec3 col = p.paper * lerp(Vec3{1.0f, 1.0f, 1.0f}, hueScale, p.colorVariation) *
                   (1.0f + p.colorVariation * (0.3f * cloud + 0.08f * formation));
        const float surface = fbm01(u * 256.0f, v * 256.0f, d.period(256), layer(kSurface), 2);
        float h = 0.62f + 0.03f * (surface - 0.5f) + 0.015f * formation;
        float rough = p.roughness + 0.05f * (surface - 0.5f) + 0.03f * cloud;

        // --- Fibres ---
        if (p.fibres > 0.0f) {
            const float f = 0.6f * fibres(u, v, 150, layer(kFibres), 1.0f, d) +
                            0.4f * fibres(u, v, 320, layer(kFibresFine), 1.2f, d);
            // Streaky texture along the machine direction (u).
            const float streaks = fbm(u * 16.0f, v * 160.0f, d.period(16, 160), layer(kFibres) ^ 0x3c6ef372U, 3);
            col = col * std::max(0.3f, 1.0f + p.fibres * (0.2f * f + 0.08f * streaks));
            h += p.fibres * (0.025f * std::min(std::abs(f), 1.0f) + 0.01f * streaks);
        }

        // --- Recycled specks: mostly dark, a few greyish or coloured flecks ---
        if (p.specks > 0.0f) {
            std::uint32_t id = 0;
            const float sp = features::blobs(u, v, 64, layer(kSpecks), p.specks * 0.3f, 0.0005f, 0.0024f, 0.5f, d, &id);
            if (sp > 0.0f) {
                const float kind = randomFromId(id, 7);
                Vec3 speck = darkSpeck;
                if (kind > 0.88f) {
                    speck = srgbToLinear(randomFromId(id, 8) < 0.5f ? Vec3{0.28f, 0.34f, 0.52f} : Vec3{0.55f, 0.24f, 0.22f});
                } else if (kind > 0.7f) {
                    speck = srgbToLinear(Vec3{0.45f, 0.43f, 0.40f});
                }
                col = lerp(col, speck, sp * (0.55f + 0.4f * randomFromId(id, 9)));
            }
        }

        // --- Wear features (mask written whenever wear is enabled) ---
        float scuff = 0.0f, dent = 0.0f, dirtAmount = 0.0f, adhesive = 0.0f, torn = 0.0f;
        CreaseSample crease;
        StainSample stain;
        if (wearOn) {
            if (p.scuffs > 0.0f) {
                const float patch = features::patchyMask(u, v, p.scuffs, 5, layer(kScuff), d);
                // Rub marks: irregular streaks along the diagonal (1, 1), periodic in u and v.
                const float streak = fbm01((u + v) * 6.0f, (u - v) * 28.0f, d.period(6, 28), layer(kScuffStreak), 5);
                const float ragged = fbm01(u * 96.0f, v * 96.0f, d.period(96), layer(kScuffStreak) ^ 0x5bd1e995U, 2);
                scuff = patch * smoothstep(0.3f, 0.75f, 0.6f * streak + 0.4f * ragged);
            }
            crease = creases(u, v, 3, layer(kCreases), p.creases * 0.7f, d);
            dent = features::blobs(u, v, 4, layer(kDents), p.dents * 0.45f, 0.04f, 0.09f, 1.0f, d);
            stain = waterStains(u, v, 3, layer(kStains), p.stains * 0.45f, d);
            const float grime = fbm01(u * 5.0f, v * 5.0f, d.period(5), layer(kDirt), 4);
            dirtAmount = p.dirt * smoothstep(0.3f, 0.7f, grime);
            const float tape = tapeStrips(u, v, 3, layer(kTape), p.tape * 0.5f, d);
            if (tape > 0.0f) {
                // Patchy adhesive left behind, and spots where the top paper tore off with the tape.
                const float res = fbm01(u * 40.0f, v * 40.0f, d.period(40), layer(kTapeResidue), 4);
                const float tear = fbm01(u * 30.0f, v * 30.0f, d.period(30), layer(kTapeTear), 4);
                adhesive = tape * smoothstep(0.38f, 0.55f, res);
                torn = tape * smoothstep(0.6f, 0.68f, tear);
            }
        }

        // --- Flutes, faint ridges through the liner (crushed in creases and dents) ---
        if (p.fluteVisibility > 0.0f) {
            const float wobble = 0.08f * fluteSpacing * fbm(u * 3.0f, v * 4.0f, d.period(3, 4), layer(kFluteWarp), 2);
            const float phase = (u + wobble) * static_cast<float>(p.flutes);
            const float crest = 1.0f - std::pow(std::abs(std::sin(kPi * phase)), 1.3f); // 1 on a flute tip
            const float crush = 1.0f - aw * std::max(crease.near, dent);
            const float vis = p.fluteVisibility * fluteFade * crush *
                              (0.6f + 0.4f * fbm01(u * 3.0f, v * 3.0f, d.period(3), layer(kFluteVis), 3));
            h += 0.16f * vis * (crest - 0.5f);
            col = col * (1.0f + 0.03f * vis * (crest - 0.5f));
        }

        // --- Apply wear to the base maps ---
        if (aw > 0.0f) {
            // Scuffs: top layer rubbed off, lighter and fuzzy.
            if (scuff > 0.0f) {
                const float fuzz = fbm01(u * 384.0f, v * 384.0f, d.period(384), layer(kFuzz), 2);
                const Vec3 abraded = adjustSaturation(col * 1.35f, 0.8f) * (0.88f + 0.24f * fuzz);
                col = lerp(col, abraded, 0.8f * scuff);
                h += 0.04f * scuff * (fuzz - 0.5f);
                rough = lerp(rough, 0.97f, scuff);
            }
            // Tape residue: yellowed adhesive (glossy) and patches of torn-off paper.
            if (adhesive > 0.0f || torn > 0.0f) {
                col = lerp(col, col * Vec3{0.98f, 0.9f, 0.62f}, 0.6f * adhesive);
                rough = lerp(rough, 0.5f, 0.8f * adhesive);
                h += 0.015f * adhesive;
                col = lerp(col, adjustSaturation(col * 1.3f, 0.75f), 0.8f * torn);
                h -= 0.03f * torn;
                rough = lerp(rough, 0.97f, torn);
            }
            // Creases and dents.
            h -= 0.2f * crease.groove + 0.16f * crease.near + 0.35f * dent;
            col = col * (1.0f - 0.14f * crease.groove - 0.06f * dent);
            const float crack = crease.core * smoothstep(0.45f, 0.6f, fbm01(u * 70.0f, v * 70.0f, d.period(70), layer(kCreaseWobble), 2));
            col = lerp(col, adjustSaturation(col * 1.3f, 0.8f), 0.7f * crack);
            // Water stains: darker, browner; dark tide marks; slight cockling.
            if (stain.inside > 0.0f || stain.ring > 0.0f) {
                const float cockle = fbm(u * 20.0f, v * 20.0f, d.period(20), layer(kCockle), 2);
                col = lerp(col, col * stainTint, stain.inside);
                col = lerp(col, col * ringTint, stain.ring);
                h += 0.03f * stain.inside * cockle;
                rough = lerp(rough, rough * 0.94f, stain.ring);
            }
            // Dust collects in low areas (between flutes, in creases and dents).
            if (dirtAmount > 0.0f) {
                const float low = saturate((0.62f - h) * 5.0f);
                col = lerp(col, dustColor * (0.8f + 0.4f * formation), 0.55f * dirtAmount * (0.35f + 0.65f * low));
                rough = lerp(rough, 0.95f, dirtAmount);
            }
        }

        out.albedo.at(x, y, 0) = saturate(col.x);
        out.albedo.at(x, y, 1) = saturate(col.y);
        out.albedo.at(x, y, 2) = saturate(col.z);
        out.height.at(x, y) = saturate(h);
        out.roughness.at(x, y) = saturate(rough);
        out.wear.at(x, y) =
            saturate(std::max({scuff, crease.groove, dent * 0.7f, stain.inside * 0.6f, stain.ring, dirtAmount * 0.6f, adhesive, torn}));
    });
}

} // namespace md
