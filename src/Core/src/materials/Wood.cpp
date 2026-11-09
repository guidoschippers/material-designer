#include "materials/Wood.h"

#include "materials/Features.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

using noise::fbm;
using noise::fbm01;
using noise::randomFromId;

enum Layer : std::uint32_t {
    kBoard = 1, kSegment, kWave, kFiber, kPores, kKnots, kGrainNoise, kScratches, kDents, kWorn, kFade,
    kRingNoise, kRingTone, kStreaks, kDashes, kWeather,
};

/// Colours and character of a species (sRGB colours).
struct Species {
    Vec3 earlywood;
    Vec3 latewood;
    Vec3 knot;
    float pores;      ///< visibility of open pores (oak high, pine low)
    float ringSharp;  ///< narrow, sharp latewood bands (pine) vs soft (oak)
};

Species species(const std::string& id)
{
    if (id == "pine") return {{0.84f, 0.66f, 0.46f}, {0.66f, 0.40f, 0.20f}, {0.30f, 0.16f, 0.08f}, 0.15f, 1.0f};
    if (id == "walnut") return {{0.42f, 0.28f, 0.18f}, {0.26f, 0.16f, 0.10f}, {0.16f, 0.10f, 0.06f}, 0.6f, 0.4f};
    return {{0.74f, 0.50f, 0.25f}, {0.58f, 0.34f, 0.14f}, {0.28f, 0.16f, 0.08f}, 1.0f, 0.3f}; // oak
}

struct Params {
    Species sp;
    Vec3 early, late, knotColor;
    Vec3 stainColor;
    float colorVariation = 0, grainScale = 1, ringsPerUnit = 0, waviness = 0, irregular = 0, figure = 0;
    float contrast = 0, fibrous = 0, pores = 0, limed = 0;
    float knots = 0, knotSize = 0;
    bool planks = true;
    int plankCount = 1, segmentCount = 1;
    float gap = 0, plankVariation = 0, gloss = 0, stain = 0;
    float scratches = 0, dents = 0, fading = 0, worn = 0, weathering = 0;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    p.sp = species(r.getEnum("wood_type"));
    p.early = srgbToLinear(p.sp.earlywood);
    p.late = srgbToLinear(p.sp.latewood);
    p.knotColor = srgbToLinear(p.sp.knot);
    p.colorVariation = r.getFloat("color_variation");
    p.grainScale = lerp(0.5f, 2.0f, r.getFloat("grain_scale"));
    p.ringsPerUnit = lerp(40.0f, 200.0f, r.getFloat("ring_density")) / p.grainScale;
    p.waviness = r.getFloat("waviness");
    p.irregular = r.getFloat("ring_irregularity");
    p.figure = r.getFloat("figure");
    p.contrast = r.getFloat("ring_contrast");
    p.fibrous = r.getFloat("fibrous");
    p.pores = r.getFloat("pores") * p.sp.pores;
    p.limed = r.getFloat("limed_pores");
    p.knots = r.getFloat("knots");
    p.knotSize = r.getFloat("knot_size");
    p.planks = r.getBool("planks");
    p.plankCount = p.planks ? noise::frequency(1.0f / r.getFloat("plank_width")) : 1;
    p.segmentCount = p.planks ? noise::frequency(1.0f / r.getFloat("plank_length")) : 1;
    p.gap = r.getFloat("plank_gap");
    p.plankVariation = r.getFloat("plank_variation");
    p.gloss = r.getFloat("gloss");
    p.stain = r.getFloat("stain");
    const Color sc = r.getColor("stain_color");
    p.stainColor = srgbToLinear(Vec3{sc.r, sc.g, sc.b});
    const float w = ctx.wearAmount();
    p.scratches = r.getFloat("scratches") * w;
    p.dents = r.getFloat("dents") * w;
    p.fading = r.getFloat("sun_fading") * w;
    p.worn = r.getFloat("worn_varnish") * w;
    p.weathering = r.getFloat("weathering") * w;
    return p;
}

/// Smooth periodic sawtooth in [-1, 1] (three Fourier terms): a slow rise and a quick fall. Used as
/// the cut depth along a board that must tile: mostly nested arches instead of closed rings.
float smoothSawtooth(float t)
{
    const float a = kTwoPi * t;
    return (std::sin(a) - std::sin(2.0f * a) / 2.0f + std::sin(3.0f * a) / 3.0f) / 1.42f;
}

/// Thin random scratches (several per grid cell), returns intensity in [0, 1].
float scratchField(float u, float v, float amount, std::uint32_t seed, const features::Domain& d)
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
            const std::uint32_t cell = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0),
                                                       noise::wrapIndex(gy, d.seamlessV ? cells : 0), seed);
            const int count = static_cast<int>(amount * 3.0f + randomFromId(cell, 0) * 1.5f);
            for (int k = 0; k < count; ++k) {
                const std::uint32_t id = noise::hash32(cell + static_cast<std::uint32_t>(k) * 0x9e3779b9U);
                const Vec2 c{(static_cast<float>(gx) + randomFromId(id, 1)) * cs, (static_cast<float>(gy) + randomFromId(id, 2)) * cs};
                const float angle = randomFromId(id, 3) * kPi;
                const float len = cs * (0.15f + 0.6f * randomFromId(id, 4));
                const Vec2 dir{std::cos(angle) * len * 0.5f, std::sin(angle) * len * 0.5f};
                const float dist = features::segmentDistance({u, v}, c - dir, c + dir);
                const float width = 0.0004f + 0.0008f * randomFromId(id, 5);
                const float s = (1.0f - smoothstep(width, width + d.aa(), dist)) * (0.4f + 0.6f * randomFromId(id, 6));
                best = std::max(best, s);
            }
        }
    }
    return best;
}

} // namespace

WoodMaterial::WoodMaterial()
{
    auto& s = schema_;
    s.addEnum("wood_type", "Wood type", {{"pine", "Light (pine)"}, {"oak", "Medium (oak)"}, {"walnut", "Dark (walnut)"}}, "oak")
        .inGroup("Wood")
        .tip("Species preset: colours, ring character and pores.");
    s.addFloat("color_variation", "Colour variation", 0.5, 0.0, 1.0)
        .inGroup("Wood")
        .tip("Tonal variation within the wood.");

    s.addFloat("grain_scale", "Grain scale", 0.5, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Scale of the grain figure: low = fine, high = large, bold figure.");
    s.addFloat("ring_density", "Ring density", 0.5, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Number of annual rings: low = wide rings (fast growth), high = tight rings.");
    s.addFloat("waviness", "Waviness", 0.35, 0.0, 1.0)
        .inGroup("Grain")
        .tip("How much the grain meanders.");
    s.addFloat("ring_irregularity", "Ring irregularity", 0.5, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Uneven ring widths and contrast, and organic distortion of the ring pattern.");
    s.addFloat("figure", "Cathedral figure", 0.35, 0.0, 1.0)
        .inGroup("Grain")
        .enabledIf("planks")
        .tip("Share of boards cut close to the log centre, which shows nested arches (cathedrals); the rest has near-straight grain.");
    s.addFloat("ring_contrast", "Ring contrast", 0.6, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Contrast between light earlywood and dark latewood.");
    s.addFloat("fibrous", "Fibrous streaks", 0.3, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Coarse, open-grained look: ragged ring edges and dark latewood broken into short streaks.");
    s.addFloat("pores", "Pores", 0.5, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Visibility of the open pores (strong in oak, weak in pine).");
    s.addFloat("limed_pores", "Limed pores", 0.25, 0.0, 1.0)
        .inGroup("Grain")
        .tip("Pores filled with white residue (limed or worn white-oil finish).");

    s.addFloat("knots", "Knots", 0.3, 0.0, 1.0)
        .inGroup("Knots")
        .tip("Number of knots.");
    s.addFloat("knot_size", "Knot size", 0.4, 0.0, 1.0)
        .inGroup("Knots")
        .tip("Size of the knots; the rings bend around them.");

    s.addBool("planks", "Planks", true)
        .inGroup("Planks")
        .tip("Lay the wood out as planks. Off = one continuous board with straight grain (tiles in both directions).");
    s.addFloat("plank_width", "Plank width", 0.2, 0.08, 1.0)
        .inGroup("Planks")
        .enabledIf("planks")
        .tip("Plank width as a fraction of the texture (rounded so the planks tile).");
    s.addFloat("plank_length", "Plank length", 0.5, 0.2, 1.0)
        .inGroup("Planks")
        .enabledIf("planks")
        .tip("Plank length as a fraction of the texture; 1 = no butt joints. Joints are staggered.");
    s.addFloat("plank_gap", "Gap", 0.3, 0.0, 1.0)
        .inGroup("Planks")
        .enabledIf("planks")
        .tip("Width and depth of the joints between planks.");
    s.addFloat("plank_variation", "Plank variation", 0.5, 0.0, 1.0)
        .inGroup("Planks")
        .enabledIf("planks")
        .tip("Colour difference between planks.");

    s.addFloat("gloss", "Gloss", 0.4, 0.0, 1.0)
        .inGroup("Finish")
        .tip("Finish: 0 = raw/matte, 1 = high-gloss varnish.");
    s.addFloat("stain", "Stain", 0.0, 0.0, 1.0)
        .inGroup("Finish")
        .tip("Strength of a coloured stain; it soaks in more where the grain is open (latewood, pores).");
    s.addColor("stain_color", "Stain colour", Color{0.38f, 0.34f, 0.31f})
        .inGroup("Finish")
        .tip("Colour of the stain.");

    s.addFloat("scratches", "Scratches", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Fine light scratches through the finish.");
    s.addFloat("dents", "Dents", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Small dents and dings.");
    s.addFloat("sun_fading", "Sun fading", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Bleached, desaturated colour from sunlight.");
    s.addFloat("worn_varnish", "Worn varnish", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Patches where the finish has worn off: duller and lighter.");
    s.addFloat("weathering", "Weathering", 0.0, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Outdoor weathering: the finish (and stain) erodes in streaks along the grain, exposing lighter, "
             "greying wood; the soft earlywood wears away so the grain stands out.");
}

void WoodMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const features::Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const float aw = ctx.applyWear() ? 1.0f : 0.0f;
    const bool wearOn = ctx.wearEnabled();
    const int n = p.plankCount, segs = p.segmentCount;
    const float boardWidth = 1.0f / static_cast<float>(n);
    const int knotCells = std::max(2, static_cast<int>(std::lround(6.0f / p.grainScale)));

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        // --- Board layout ---
        const int plank = static_cast<int>(std::floor(u * n));
        const std::uint32_t plankId = noise::hashCell(noise::wrapIndex(plank, d.seamlessU ? n : 0), 0, layer(kBoard));
        const float stagger = randomFromId(plankId, 0) / static_cast<float>(segs);
        const float vs = (v + stagger) * static_cast<float>(segs);
        const int segment = static_cast<int>(std::floor(vs));
        const std::uint32_t boardId = noise::hashCell(noise::wrapIndex(plank, d.seamlessU ? n : 0),
                                                      noise::wrapIndex(segment, d.seamlessV ? segs : 0), layer(kSegment));
        const float lx = u * n - static_cast<float>(plank); // 0..1 across the plank
        const float ly = vs - static_cast<float>(segment);  // 0..1 along the board

        // Meandering grain (periodic in v).
        const float wave = p.waviness * 0.012f * p.grainScale *
                           fbm(v * 3.0f, randomFromId(boardId, 1) * 40.0f, {d.seamlessV ? 3 : 0, 0}, layer(kWave), 4);
        const float fineWave = 0.002f * fbm(u * 40.0f, v * 6.0f, d.period(40, 6), layer(kGrainNoise), 3);

        // --- Annual rings ---
        float rings;
        int ringPeriod; // ring index period (straight grain only), 0 = none
        if (p.planks) {
            // Flat-sawn board: distance to the (virtual) log axis. The cut depth changes slowly along
            // the board, which produces the cathedral arches.
            // Most boards are cut far from the log axis (near-straight grain); about a third close
            // to it, which shows cathedral arches.
            const bool cathedral = randomFromId(boardId, 6) < p.figure;
            const float side = randomFromId(boardId, 7) < 0.5f ? -1.0f : 1.0f;
            const float cx = cathedral ? 0.2f + 0.6f * randomFromId(boardId, 2) : 0.5f + side * (2.0f + 4.0f * randomFromId(boardId, 2));
            // The log axis is tilted relative to the board: the cut depth changes linearly along a
            // board (nested arches). With a single segment per tile a periodic variation keeps it seamless.
            // Distance along the board (texture units) times the slope of the cut relative to the log axis.
            const float segLength = 1.0f / static_cast<float>(segs);
            const float along = segs > 1 ? (ly - 0.5f) * segLength * (randomFromId(boardId, 4) < 0.5f ? -1.0f : 1.0f)
                                         : 0.13f * smoothSawtooth(v + randomFromId(boardId, 4));
            const float slope = 0.06f * p.grainScale;
            const float halfRange = slope * (segs > 1 ? 0.5f * segLength : 0.13f);
            // The depth stays positive so the arches never turn into closed rings (bullseyes).
            const float depth = (0.02f + 0.12f * randomFromId(boardId, 3)) * p.grainScale + halfRange * 1.1f + slope * along;
            // Organic, low-frequency distortion of the growth rings (a real log is not a cylinder).
            const float distort = p.irregular * 3.0f / p.ringsPerUnit *
                                  fbm(lx * 1.5f + randomFromId(boardId, 8) * 50.0f, v * 1.0f, {0, d.seamlessV ? 1 : 0},
                                      layer(kRingNoise), 3);
            const float X = (lx - cx) * boardWidth + wave + fineWave;
            const float D = depth + distort;
            rings = std::sqrt(X * X + D * D) * p.ringsPerUnit;
            // Uneven ring widths: a slow, monotonic-ish warp of the ring index.
            rings += p.irregular * 1.2f * fbm(rings * 0.3f, randomFromId(boardId, 9) * 97.0f, {}, layer(kRingNoise) ^ 3u, 3);
            ringPeriod = 0;
        } else {
            // Straight grain across the whole tile; integer ring count keeps it seamless.
            const int count = noise::frequency(p.ringsPerUnit * 0.35f);
            const int irregularFreq = std::max(1, count / 3);
            const float irregularShift = p.irregular * 1.2f / static_cast<float>(count) *
                                         fbm(u * static_cast<float>(irregularFreq), 0.5f, {d.seamlessU ? irregularFreq : 0, 0},
                                             layer(kRingNoise) ^ 3u, 3);
            rings = (u + wave + fineWave + irregularShift) * static_cast<float>(count);
            ringPeriod = d.seamlessU ? count : 0;
        }

        // --- Knots: rings bend around them, dark core ---
        float knotCore = 0.0f;
        if (p.knots > 0.0f) {
            const int kcx = static_cast<int>(std::floor(u * knotCells)), kcy = static_cast<int>(std::floor(v * knotCells));
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int gx = kcx + dx, gy = kcy + dy;
                    const std::uint32_t kid = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? knotCells : 0),
                                                              noise::wrapIndex(gy, d.seamlessV ? knotCells : 0), layer(kKnots));
                    if (randomFromId(kid, 0) >= p.knots * 0.45f) continue;
                    const float kx = (static_cast<float>(gx) + 0.2f + 0.6f * randomFromId(kid, 1)) / knotCells;
                    const float ky = (static_cast<float>(gy) + randomFromId(kid, 2)) / knotCells;
                    if (p.planks && static_cast<int>(std::floor(kx * n)) != plank) continue; // stays inside its plank
                    const float radius = (0.004f + 0.012f * p.knotSize * (0.5f + randomFromId(kid, 3))) * p.grainScale;
                    const float ddx = u - kx, ddy = (v - ky) * 0.6f; // slightly elongated along the grain
                    const float dist = std::sqrt(ddx * ddx + ddy * ddy);
                    const float influence = std::exp(-(dist * dist) / (radius * radius * 16.0f));
                    rings += influence * (radius * p.ringsPerUnit * 1.5f) / (dist / radius + 0.8f);
                    knotCore = std::max(knotCore, 1.0f - smoothstep(radius * 0.7f, radius, dist));
                }
            }
        }

        // Fibrous texture: anisotropic noise that frays the ring edges and breaks latewood into streaks.
        const float streak = fbm(u * 200.0f, v * 12.0f, d.period(200, 12), layer(kStreaks), 3);
        const float ringPos = rings + p.fibrous * 0.3f * streak;
        const float ringIndex = std::floor(ringPos);
        const float f = ringPos - ringIndex;
        // Latewood band: narrow and sharp for pine, broad and soft for oak/walnut.
        const float bandStart = lerp(0.45f, 0.72f, p.sp.ringSharp);
        float late = smoothstep(bandStart, lerp(0.85f, 0.9f, p.sp.ringSharp), f) * (1.0f - smoothstep(0.93f, 1.0f, f));
        // Rings differ in strength (good and bad growth years).
        const int ri = noise::wrapIndex(static_cast<int>(ringIndex), ringPeriod);
        const float ringStrength = 1.0f + p.irregular * 0.8f * (randomFromId(noise::hashCell(ri, 0, layer(kRingTone) ^ boardId), 0) - 0.5f);
        const float dashes = noise::value(u * 300.0f, v * 14.0f, d.period(300, 14), layer(kDashes));
        late *= ringStrength * lerp(1.0f, 0.25f + 0.75f * smoothstep(0.3f, 0.6f, dashes), p.fibrous);

        // --- Colour ---
        const float fiber = noise::value(u * 700.0f, v * 18.0f, d.period(700, 18), layer(kFiber));
        const float fiberAmp = 0.12f + 0.25f * p.fibrous;
        const float tone = (1.0f + (randomFromId(boardId, 5) - 0.5f) * 0.35f * p.plankVariation) *
                           (1.0f + (fbm01(u * 5.0f, v * 2.0f, d.period(5, 2), layer(kGrainNoise) ^ 7u, 3) - 0.5f) * 0.3f * p.colorVariation);
        Vec3 col = lerp(p.early, p.late, saturate(late * p.contrast)) * tone * (1.0f - 0.5f * fiberAmp + fiberAmp * fiber);
        col = lerp(col, p.knotColor, knotCore);

        // Open pores: short dashes along the grain.
        const float poreNoise = noise::value(u * 520.0f, v * 45.0f, d.period(520, 45), layer(kPores));
        const float pore = p.pores * smoothstep(0.72f, 0.85f, poreNoise) * (0.4f + 0.6f * saturate(late));
        const Vec3 poreCol = lerp(col * 0.55f, srgbToLinear(Vec3{0.86f, 0.83f, 0.76f}), p.limed);
        col = lerp(col, poreCol, pore);

        float h = 0.5f + 0.06f * late * p.sp.ringSharp + (0.02f + 0.04f * p.fibrous) * fiber - 0.12f * pore + 0.03f * knotCore;
        float rough = lerp(0.8f, 0.22f, p.gloss) + 0.15f * pore + 0.05f * fiber;

        // --- Stain: soaks in more where the grain is open ---
        const Vec3 bare = col;
        if (p.stain > 0.0f) {
            const float absorb = saturate(0.8f + 0.2f * late + 0.4f * pore + 0.3f * p.fibrous * (fiber - 0.5f));
            // The stain keeps the grain visible: darker in latewood, lighter in earlywood.
            const Vec3 stained = p.stainColor * (0.55f + 0.9f * luminance(col) / std::max(luminance(p.early), 1e-4f)) *
                                 (1.0f - 0.4f * p.contrast * saturate(late));
            col = lerp(col, stained, p.stain * absorb);
        }

        // --- Weathering: the finish erodes in streaks along the grain, the wood greys ---
        float exposed = 0.0f;
        if (wearOn && p.weathering > 0.0f) {
            const float streaks = fbm(u * 90.0f, v * 8.0f, d.period(90, 8), layer(kWeather), 4);
            const float patches = fbm(u * 3.0f, v * 2.0f, d.period(3, 2), layer(kWeather) ^ 5u, 3);
            const float flecks = noise::value(u * 260.0f, v * 36.0f, d.period(260, 36), layer(kWeather) ^ 11u);
            const float s = 0.5f + 0.8f * streaks + 0.6f * patches - 0.3f * saturate(late) + 0.35f * (flecks - 0.5f) +
                            0.15f * (fiber - 0.5f);
            const float t = 1.0f - p.weathering;
            exposed = smoothstep(t - 0.06f, t + 0.06f, s);
            const float lb = luminance(bare);
            const Vec3 grey = Vec3{lb * 1.02f, lb, lb * 0.97f} * 1.2f + Vec3{0.02f, 0.02f, 0.02f};
            const Vec3 weatheredWood = lerp(bare * 1.1f, grey, 0.35f + 0.35f * p.weathering);
            col = lerp(col, weatheredWood, aw * exposed);
            // Overall dull, greyish haze where the finish is thin.
            const float lc = luminance(col);
            col = lerp(col, Vec3{lc, lc, lc * 1.03f} * 1.05f, aw * p.weathering * 0.5f * fbm01(u * 2.0f, v * 2.0f, d.period(2), layer(kWeather) ^ 9u, 3));
            rough = lerp(rough, 0.88f, aw * lerp(0.5f * p.weathering, 1.0f, exposed));
            h -= aw * p.weathering * (0.2f * (1.0f - saturate(late)) + 0.06f * exposed + 0.08f * (0.5f - flecks));
        }

        // --- Plank joints and bevels ---
        if (p.planks && p.gap > 0.0f) {
            const float edgeU = std::min(lx, 1.0f - lx) * boardWidth;
            const float edgeV = segs > 1 ? std::min(ly, 1.0f - ly) / static_cast<float>(segs) : 1.0f;
            const float edge = std::min(edgeU, edgeV);
            const float halfGap = 0.0004f + 0.0016f * p.gap;
            const float groove = 1.0f - smoothstep(halfGap, halfGap + d.aa(), edge);
            const float bevel = 1.0f - smoothstep(0.0f, halfGap * 4.0f, edge);
            h -= 0.15f * bevel * p.gap + 0.3f * groove;
            col = lerp(col, col * 0.15f, groove);
            rough = lerp(rough, 0.9f, groove);
        }

        // --- Wear ---
        float scratch = 0.0f, dent = 0.0f, worn = 0.0f, fade = 0.0f;
        if (wearOn) {
            scratch = scratchField(u, v, p.scratches, layer(kScratches), d);
            dent = features::blobs(u, v, 14, layer(kDents), p.dents * 0.5f, 0.002f, 0.007f, 0.6f, d);
            worn = features::patchyMask(u, v, p.worn, 3, layer(kWorn), d);
            fade = p.fading * (0.6f + 0.4f * fbm01(u * 2.0f, v * 2.0f, d.period(2), layer(kFade), 3));
        }
        const float l = luminance(col);
        col = lerp(col, Vec3{l, l, l} * 1.25f + Vec3{0.03f, 0.03f, 0.03f}, aw * fade * 0.6f);
        col = lerp(col, col * 1.15f + Vec3{0.02f, 0.02f, 0.02f}, aw * worn * 0.5f);
        rough = lerp(rough, 0.75f, aw * worn);
        col = lerp(col, lerp(col, lerp(p.early * 1.2f, Vec3{0.6f, 0.58f, 0.55f}, 0.4f), 0.35f), aw * scratch);
        rough = lerp(rough, 0.85f, aw * scratch);
        h -= aw * (0.04f * scratch + 0.15f * dent);
        col = lerp(col, col * 0.85f, aw * dent);

        out.albedo.at(x, y, 0) = saturate(col.x);
        out.albedo.at(x, y, 1) = saturate(col.y);
        out.albedo.at(x, y, 2) = saturate(col.z);
        out.height.at(x, y) = saturate(h);
        out.roughness.at(x, y) = saturate(rough);
        out.wear.at(x, y) = saturate(std::max({scratch, dent, worn * 0.7f, fade * 0.4f, exposed * 0.8f}));
    });
}

} // namespace md
