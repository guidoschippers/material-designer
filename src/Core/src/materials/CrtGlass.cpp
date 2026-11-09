#include "materials/CrtGlass.h"

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
    kTint = 1, kCoating, kDust, kDustSpecks, kPrints, kPrintWarp, kPrintPatch, kSmudges, kSmudgeStreaks,
    kWipes, kWipeMask,
};

/// Albedo factor of one phosphor colour (0 = red, 1 = green, 2 = blue). Averages to about 1.
Vec3 phosphor(int c)
{
    switch (c) {
    case 0: return {1.5f, 0.75f, 0.75f};
    case 1: return {0.75f, 1.5f, 0.75f};
    default: return {0.75f, 0.75f, 1.5f};
    }
}

int mod3(int i) { return ((i % 3) + 3) % 3; }

/// Aperture grille: vertical phosphor stripes R, G, B separated by thin dark gaps.
/// columns = number of sub-pixel stripes per tile (a multiple of 3).
Vec3 apertureGrille(float u, int columns, const Domain& d)
{
    const float x = u * static_cast<float>(columns);
    const int i = static_cast<int>(std::floor(x));
    const float f = x - static_cast<float>(i);
    const float aa = d.aa() * static_cast<float>(columns);
    const float stripe = smoothstep(0.12f - aa, 0.12f + aa, f) * (1.0f - smoothstep(0.88f - aa, 0.88f + aa, f));
    return lerp(Vec3{0.45f, 0.45f, 0.45f}, phosphor(mod3(i)), stripe);
}

/// Delta shadow mask: round phosphor dots on a triangular lattice, coloured so that the three
/// neighbours of every triangle differ. columns (multiple of 3) dots per row, rows (even) per tile.
Vec3 shadowMask(float u, float v, int columns, int rows, const Domain& d)
{
    const float a = 1.0f / static_cast<float>(columns), h = 1.0f / static_cast<float>(rows);
    const float y = v / h;
    const int j0 = static_cast<int>(std::floor(y));
    float best = 1e9f;
    int bestColor = 0;
    for (int j = j0 - 1; j <= j0 + 2; ++j) {
        // Dot (i, j) sits at x = (i + j / 2 + 0.25) * a, y = (j + 0.5) * h.
        const float xi = u / a - 0.5f * static_cast<float>(j) - 0.25f;
        const int i0 = static_cast<int>(std::floor(xi));
        for (int i = i0 - 1; i <= i0 + 2; ++i) {
            const float px = (static_cast<float>(i) + 0.5f * static_cast<float>(j) + 0.25f) * a;
            const float py = (static_cast<float>(j) + 0.5f) * h;
            const float dx = u - px, dy = v - py;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < best) {
                best = dist;
                bestColor = mod3(i - j);
            }
        }
    }
    const float r = 0.36f * a;
    const float dot = 1.0f - smoothstep(r - d.aa(), r + d.aa(), best);
    return lerp(Vec3{0.45f, 0.45f, 0.45f}, phosphor(bestColor), dot);
}

/// Fingerprints: oval prints of greasy ridges in whorl/loop patterns, partly smeared.
/// Returns the grease amount in [0, 1].
float fingerprints(float u, float v, int cells, std::uint32_t seed, float probability, const Domain& d)
{
    if (probability <= 0.0f) {
        return 0.0f;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    const Vec2 w = noise::warp(u * 40.0f, v * 40.0f, d.period(40), seed ^ 0x7feb352dU, 2);
    const float patchy = fbm01(u * 30.0f, v * 30.0f, d.period(30), seed ^ 0x846ca68bU, 3);
    float best = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0),
                                                     noise::wrapIndex(gy, d.seamlessV ? cells : 0), seed);
            if (randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + 0.2f + 0.6f * randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + 0.2f + 0.6f * randomFromId(id, 2)) * cellSize;
            const float radius = lerp(0.022f, 0.04f, randomFromId(id, 3));
            const float angle = randomFromId(id, 4) * kPi;
            const float ca = std::cos(angle), sa = std::sin(angle);
            const float lx = ((u - px) * ca + (v - py) * sa) / radius;          // across the finger
            const float ly = (-(u - px) * sa + (v - py) * ca) / (radius * 1.4f); // along the finger
            const float r = std::sqrt(lx * lx + ly * ly);
            if (r > 1.3f) {
                continue;
            }
            // Ridges: rings around the core, turned into a spiral/loop by the angle and a warp.
            const float ridgeSpacing = 0.075f; // in units of the print radius
            const float phase = r / ridgeSpacing + std::atan2(ly, lx) / kTwoPi + 1.2f * (w.x + 0.5f * w.y);
            const float ridgePeriodUv = ridgeSpacing * radius;
            const float resolve = smoothstep(2.0f * d.pixel, 4.0f * d.pixel, ridgePeriodUv);
            const float ridges = lerp(0.5f, 0.5f + 0.5f * std::cos(kTwoPi * phase), resolve);
            const float outline = 1.0f - smoothstep(0.7f, 1.0f + 0.25f * w.x, r);
            const float pressure = 0.4f + 0.6f * smoothstep(0.2f, 0.6f, patchy);
            best = std::max(best, outline * pressure * (0.25f + 0.75f * ridges) * (0.6f + 0.4f * randomFromId(id, 5)));
        }
    }
    return best;
}

/// Circular wipe streaks left by a cloth: fine concentric arcs in a few swirls per tile.
float wipeStreaks(float u, float v, int cells, std::uint32_t seed, float probability, const Domain& d)
{
    if (probability <= 0.0f) {
        return 0.0f;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    // Hand-made swirls are never true circles: wobble the radius.
    const float wobble = 0.02f * fbm(u * 6.0f, v * 6.0f, d.period(6), seed ^ 0x3243f6a9U, 3);
    float best = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0),
                                                     noise::wrapIndex(gy, d.seamlessV ? cells : 0), seed);
            if (randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + randomFromId(id, 2)) * cellSize;
            const float rInner = lerp(0.05f, 0.25f, randomFromId(id, 3)) * cellSize * 2.0f;
            const float rOuter = rInner + lerp(0.06f, 0.15f, randomFromId(id, 4));
            const float ddx = u - px, ddy = v - py;
            const float r = std::sqrt(ddx * ddx + ddy * ddy) + wobble;
            const float band = smoothstep(rInner, rInner + 0.03f, r) * (1.0f - smoothstep(rOuter - 0.03f, rOuter, r));
            if (band <= 0.0f) {
                continue;
            }
            // Fine streaks: 1D noise across the arcs, two scales.
            const std::uint32_t s = id ^ 0x9e3779b9U;
            const float fine = noise::value(r * 260.0f, 0.5f, noise::Period{}, s);
            const float coarse = noise::value(r * 90.0f, 3.5f, noise::Period{}, s ^ 0x68e31da4U);
            const float resolve = smoothstep(2.0f * d.pixel, 4.0f * d.pixel, 1.0f / 260.0f);
            const float streak = lerp(0.35f, smoothstep(0.55f, 0.85f, fine), resolve) * (0.4f + 0.6f * coarse);
            best = std::max(best, band * streak * (0.5f + 0.5f * randomFromId(id, 5)));
        }
    }
    return best;
}

struct Params {
    Vec3 tint;
    float tintVariation = 0, roughness = 0, antiGlare = 0, maskVisibility = 0;
    bool grille = false;
    int maskColumns = 3, maskRows = 2;
    float dust = 0, prints = 0, wipes = 0;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    const Color c = r.getColor("tint");
    p.tint = srgbToLinear(Vec3{c.r, c.g, c.b});
    p.tintVariation = r.getFloat("tint_variation");
    p.roughness = r.getFloat("roughness");
    p.antiGlare = r.getFloat("anti_glare");
    p.maskVisibility = r.getFloat("mask_visibility");
    p.grille = r.getEnum("mask_type") == "aperture_grille";
    // Pitch = distance between neighbouring phosphors of the same colour.
    const float pitch = r.getFloat("mask_pitch");
    p.maskColumns = 3 * noise::frequency(1.0f / pitch);
    // Delta mask: dot spacing a = pitch / sqrt(3), row height a * sqrt(3) / 2 = pitch / 2.
    const float a = pitch / 1.7320508f;
    p.maskColumns = p.grille ? p.maskColumns : 3 * noise::frequency(1.0f / (3.0f * a));
    p.maskRows = 2 * noise::frequency(1.0f / pitch); // row height = pitch / 2
    const float w = ctx.wearAmount();
    p.dust = r.getFloat("dust") * w;
    p.prints = r.getFloat("fingerprints") * w;
    p.wipes = r.getFloat("wipe_streaks") * w;
    return p;
}

} // namespace

CrtGlassMaterial::CrtGlassMaterial()
{
    auto& s = schema_;
    s.addColor("tint", "Glass tint", Color{0.13f, 0.15f, 0.14f})
        .inGroup("Glass")
        .tip("Colour of the switched-off screen seen through the glass (sRGB): near-black, slightly grey-green.");
    s.addFloat("tint_variation", "Tint variation", 0.2, 0.0, 1.0)
        .inGroup("Glass")
        .tip("Very faint cloudy variation of the tint.");
    s.addFloat("roughness", "Roughness", 0.04, 0.0, 0.5)
        .inGroup("Glass")
        .tip("Roughness of the clean glass (very smooth).");
    s.addFloat("anti_glare", "Anti-glare coating", 0.0, 0.0, 1.0)
        .inGroup("Glass")
        .tip("Matte anti-glare coating: raises the roughness and adds a fine micro-texture to the normal map.");

    s.addEnum("mask_type", "Mask type", {{"shadow_mask", "Shadow mask (dots)"}, {"aperture_grille", "Aperture grille (stripes)"}},
              "shadow_mask")
        .inGroup("Mask")
        .tip("Phosphor pattern behind the glass: round RGB dots (most TVs) or vertical RGB stripes (Trinitron).");
    s.addFloat("mask_visibility", "Mask visibility", 0.0, 0.0, 1.0)
        .inGroup("Mask")
        .tip("How visible the phosphor pattern is in the albedo (0 = off).");
    s.addFloat("mask_pitch", "Mask pitch", 0.006, 0.002, 0.05)
        .inGroup("Mask")
        .tip("Distance between phosphors of the same colour, as a fraction of the texture (rounded so it tiles).");

    s.addFloat("dust", "Dust", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Light grey matte dust film with specks.");
    s.addFloat("fingerprints", "Fingerprints & smudges", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Greasy fingerprints (whorl and loop ridges) and smeared smudges, mostly visible in the roughness.");
    s.addFloat("wipe_streaks", "Wipe streaks", 0.2, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Fine circular streaks left by wiping the screen with a cloth.");
}

void CrtGlassMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const bool wearOn = ctx.wearEnabled();
    const float aw = ctx.applyWear() ? 1.0f : 0.0f;

    // Phosphors narrower than ~2 pixels cannot be shown; fade the pattern out instead of aliasing.
    const float subpixel = 1.0f / static_cast<float>(p.maskColumns);
    const float maskFade = smoothstep(1.5f * d.pixel, 3.0f * d.pixel, subpixel);
    const float maskAmount = p.maskVisibility * maskFade;
    const Vec3 dustColor = srgbToLinear(Vec3{0.52f, 0.51f, 0.49f});
    const Vec3 greaseTint{1.25f, 1.22f, 1.18f};

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        const float cloud = fbm(u * 3.0f, v * 3.0f, d.period(3), layer(kTint), 3);
        Vec3 col = p.tint * (1.0f + 0.1f * p.tintVariation * cloud);
        float h = 0.5f;
        float rough = p.roughness;

        // --- Anti-glare coating: fine micro-texture, matte ---
        if (p.antiGlare > 0.0f) {
            const float micro = fbm01(u * 320.0f, v * 320.0f, d.period(320), layer(kCoating), 2);
            h += 0.25f * p.antiGlare * (micro - 0.5f);
            rough = lerp(rough, std::max(rough, 0.38f + 0.06f * (micro - 0.5f)), p.antiGlare);
            col = col * (1.0f + 0.12f * p.antiGlare); // the diffusing coating looks slightly greyer
        }

        // --- Phosphor mask behind the glass ---
        if (maskAmount > 0.0f) {
            const Vec3 f = p.grille ? apertureGrille(u, p.maskColumns, d) : shadowMask(u, v, p.maskColumns, p.maskRows, d);
            col = col * lerp(Vec3{1.0f, 1.0f, 1.0f}, f, maskAmount);
        }

        // --- Wear ---
        float dust = 0.0f, specks = 0.0f, grease = 0.0f, wipe = 0.0f;
        if (wearOn) {
            if (p.dust > 0.0f) {
                const float film = 0.5f + 0.5f * smoothstep(0.25f, 0.75f, fbm01(u * 4.0f, v * 4.0f, d.period(4), layer(kDust), 4));
                dust = saturate(p.dust * film * 0.8f);
                specks = features::blobs(u, v, 110, layer(kDustSpecks), p.dust * 0.45f, 0.0005f, 0.0014f, 0.6f, d) *
                         std::min(1.0f, 2.0f * p.dust);
            }
            if (p.prints > 0.0f) {
                const float print = fingerprints(u, v, 5, layer(kPrints), p.prints * 0.55f, d);
                // Smudges: smeared grease, streaky in a random direction per smudge.
                std::uint32_t sid = 0;
                const float smudgeArea = features::blobs(u, v, 3, layer(kSmudges), p.prints * 0.6f, 0.05f, 0.11f, 0.8f, d, &sid);
                float smudge = 0.0f;
                if (smudgeArea > 0.0f) {
                    const bool diag = randomFromId(sid, 9) < 0.5f;
                    const float st = diag ? fbm01((u + v) * 4.0f, (u - v) * 90.0f, d.period(4, 90), layer(kSmudgeStreaks), 4, 0.6f)
                                          : fbm01((u - v) * 4.0f, (u + v) * 90.0f, d.period(4, 90), layer(kSmudgeStreaks), 4, 0.6f);
                    smudge = smudgeArea * (0.3f + 0.7f * smoothstep(0.35f, 0.7f, st)) * 0.7f;
                }
                grease = std::max(print, smudge);
            }
            // Only parts of each swirl leave streaks.
            wipe = wipeStreaks(u, v, 2, layer(kWipes), p.wipes * 0.7f, d) *
                   smoothstep(0.35f, 0.65f, fbm01(u * 5.0f, v * 5.0f, d.period(5), layer(kWipeMask), 3));
        }

        if (aw > 0.0f) {
            // Grease and streaks: blotchy roughness, faintly lighter (scattering film), tiny relief.
            rough = lerp(rough, std::max(rough, 0.32f), grease);
            col = lerp(col, col * greaseTint, grease);
            h += 0.06f * grease;
            rough = lerp(rough, std::max(rough, 0.24f), wipe);
            col = lerp(col, col * 1.12f, wipe);
            // Dust: matte, lighter film.
            // Dust: a thin film still lets the glass reflect (blurred); specks are fully matte.
            col = lerp(col, dustColor, 0.3f * dust + 0.6f * specks);
            rough = lerp(rough, std::max(rough, 0.3f), dust);
            rough = lerp(rough, 0.85f, specks);
            h += 0.04f * dust + 0.1f * specks;
        }

        out.albedo.at(x, y, 0) = saturate(col.x);
        out.albedo.at(x, y, 1) = saturate(col.y);
        out.albedo.at(x, y, 2) = saturate(col.z);
        out.height.at(x, y) = saturate(h);
        out.roughness.at(x, y) = saturate(rough);
        out.wear.at(x, y) = saturate(std::max({dust, specks, grease, wipe}));
    });
}

} // namespace md
