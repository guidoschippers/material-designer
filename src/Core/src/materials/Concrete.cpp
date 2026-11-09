#include "materials/Concrete.h"

#include "materials/Features.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace md {

namespace {

using noise::fbm;
using noise::fbm01;
using noise::randomFromId;

enum Layer : std::uint32_t {
    kTone = 1, kPaste, kBroom, kBoard, kBoardGrain, kStones, kPores, kCracks, kCrackMask,
    kChips, kSpalls, kStains, kDirt, kEfflo, kMoss, kJointEdge,
};

/// Exposed aggregate colours (sRGB): orange/brown/tan, dark grey, white, grey.
constexpr std::array<Vec3, 6> kAggregate = {{
    {0.62f, 0.42f, 0.26f}, {0.50f, 0.36f, 0.25f}, {0.72f, 0.62f, 0.47f},
    {0.28f, 0.27f, 0.27f}, {0.86f, 0.84f, 0.80f}, {0.55f, 0.54f, 0.52f},
}};

/// Distance (in UV units) to the nearest of `count` evenly spaced lines across one axis.
float lineDistance(float coord, int count)
{
    const float t = coord * static_cast<float>(count);
    return std::abs(t - std::round(t)) / static_cast<float>(count);
}

struct Params {
    Vec3 tone;
    float toneVariation = 0, broom = 0;
    bool broomHorizontal = true;
    bool formwork = false;
    int boards = 0;
    float aggregate = 0, aggregateVariation = 0, pores = 0, cracks = 0;
    int aggregateFreq = 0;
    bool joints = false;
    int jointCount = 1;
    bool jointsU = true, jointsV = true;
    float jointWidth = 0;
    float chips = 0, stains = 0, dirt = 0, efflorescence = 0;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    const Color c = r.getColor("base_tone");
    p.tone = srgbToLinear(Vec3{c.r, c.g, c.b});
    p.toneVariation = r.getFloat("tone_variation");
    p.broom = r.getFloat("broom_finish");
    p.broomHorizontal = r.getEnum("broom_direction") == "horizontal";
    p.formwork = r.getBool("formwork");
    p.boards = noise::frequency(1.0f / r.getFloat("board_width"));
    p.aggregate = r.getFloat("aggregate_visibility");
    p.aggregateVariation = r.getFloat("aggregate_color_variation");
    p.aggregateFreq = noise::frequency(lerp(260.0f, 90.0f, r.getFloat("aggregate_size")));
    p.pores = r.getFloat("pore_density");
    p.cracks = r.getFloat("cracks");
    p.joints = r.getBool("expansion_joints");
    p.jointCount = r.getInt("joints_per_tile");
    const std::string& dir = r.getEnum("joint_direction");
    p.jointsU = dir != "horizontal"; // vertical lines (constant u)
    p.jointsV = dir != "vertical";   // horizontal lines (constant v)
    p.jointWidth = r.getFloat("joint_width");
    const float w = ctx.wearAmount();
    p.chips = r.getFloat("chips") * w;
    p.stains = r.getFloat("water_stains") * w;
    p.dirt = r.getFloat("dirt") * w;
    p.efflorescence = r.getFloat("efflorescence") * w;
    return p;
}

} // namespace

ConcreteMaterial::ConcreteMaterial()
{
    auto& s = schema_;
    s.addColor("base_tone", "Base tone", Color{0.64f, 0.62f, 0.58f})
        .inGroup("Surface")
        .tip("Colour of the cement paste (sRGB). Light warm grey by default.");
    s.addFloat("tone_variation", "Tone variation", 0.4, 0.0, 1.0)
        .inGroup("Surface")
        .tip("Cloudy large-scale variation in tone.");
    s.addFloat("broom_finish", "Broom finish", 0.5, 0.0, 1.0)
        .inGroup("Surface")
        .tip("Strength of the fine parallel striations left by a broom or brush.");
    s.addEnum("broom_direction", "Broom direction", {{"horizontal", "Horizontal"}, {"vertical", "Vertical"}}, "horizontal")
        .inGroup("Surface")
        .tip("Direction of the broom striations.");
    s.addBool("formwork", "Formwork boards", false)
        .inGroup("Surface")
        .tip("Imprint of timber formwork boards (for walls), with board seams and wood grain.");
    s.addFloat("board_width", "Board width", 0.2, 0.05, 0.5)
        .inGroup("Surface")
        .enabledIf("formwork")
        .tip("Width of a formwork board as a fraction of the texture.");

    s.addFloat("aggregate_visibility", "Aggregate visibility", 0.55, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("How much fine aggregate (small pebbles) is exposed at the surface.");
    s.addFloat("aggregate_size", "Aggregate size", 0.4, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Size of the exposed pebbles.");
    s.addFloat("aggregate_color_variation", "Aggregate colours", 0.7, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Spread of pebble colours: orange/brown, tan, dark grey and white.");
    s.addFloat("pore_density", "Pores", 0.4, 0.0, 1.0)
        .inGroup("Aggregate")
        .tip("Small air holes and pits in the surface.");

    s.addBool("expansion_joints", "Expansion joints", true)
        .inGroup("Joints")
        .tip("Saw-cut joints dividing the slab.");
    s.addInt("joints_per_tile", "Joints per tile", 1, 1, 4)
        .inGroup("Joints")
        .enabledIf("expansion_joints")
        .tip("Number of joints across the texture in each direction.");
    s.addEnum("joint_direction", "Joint direction", {{"both", "Both"}, {"horizontal", "Horizontal"}, {"vertical", "Vertical"}}, "both")
        .inGroup("Joints")
        .enabledIf("expansion_joints")
        .tip("Which joints to cut: a grid, or parallel lines only.");
    s.addFloat("joint_width", "Joint width", 0.3, 0.0, 1.0)
        .inGroup("Joints")
        .enabledIf("expansion_joints")
        .tip("Width of the saw cut.");
    s.addFloat("cracks", "Cracks", 0.1, 0.0, 1.0)
        .inGroup("Joints")
        .tip("Hairline shrinkage cracks.");

    s.addFloat("chips", "Chips & spalls", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Chipped joint edges and small spalled areas exposing the aggregate.");
    s.addFloat("water_stains", "Water stains", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Darker, soft-edged stains where water has dried.");
    s.addFloat("dirt", "Dirt & moss", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Dirt in pores and striations; dirt and moss in the joints.");
    s.addFloat("efflorescence", "Efflorescence", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("White salt deposits, mostly along joints and cracks.");
}

void ConcreteMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const features::Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const bool wearOn = ctx.wearEnabled();
    const float aw = ctx.applyWear() ? 1.0f : 0.0f;
    const Vec3 dirtColor = srgbToLinear(Vec3{0.30f, 0.27f, 0.22f});
    const Vec3 mossColor = srgbToLinear(Vec3{0.16f, 0.20f, 0.08f});

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        // --- Cement paste ---
        const float cloud = fbm(u * 3.0f, v * 3.0f, d.period(3), layer(kTone), 5);
        const float paste = fbm01(u * 64.0f, v * 64.0f, d.period(64), layer(kPaste), 4);
        Vec3 col = p.tone * (1.0f + 0.18f * p.toneVariation * cloud) * (0.93f + 0.14f * paste);
        float h = 0.5f + 0.03f * paste;
        float rough = 0.86f + 0.06f * paste;

        // Fine sand grains in the paste: light and dark specks below stone size.
        const int sandFreq = p.aggregateFreq * 3;
        const float sand = noise::value(u * sandFreq, v * sandFreq, d.period(sandFreq), layer(kPaste) ^ 0x5u);
        col = col * (1.0f + 0.25f * (smoothstep(0.78f, 0.92f, sand) - smoothstep(0.22f, 0.08f, sand)));

        // --- Broom striations: noise stretched along the broom direction ---
        if (p.broom > 0.0f) {
            const float along = p.broomHorizontal ? u : v, across = p.broomHorizontal ? v : u;
            const noise::Period per = p.broomHorizontal ? d.period(6, 320) : d.period(320, 6);
            const float s = p.broomHorizontal ? noise::fbm(along * 6.0f, across * 320.0f, per, layer(kBroom), 2)
                                              : noise::fbm(across * 320.0f, along * 6.0f, per, layer(kBroom), 2);
            h += 0.06f * p.broom * s;
            col = col * (1.0f + 0.05f * p.broom * s);
        }

        // --- Formwork boards ---
        if (p.formwork) {
            const int board = static_cast<int>(std::floor(v * p.boards));
            const std::uint32_t id = noise::hashCell(0, noise::wrapIndex(board, d.seamlessV ? p.boards : 0), layer(kBoard));
            col = col * (0.94f + 0.12f * randomFromId(id, 0));
            const float grain = noise::fbm(u * 8.0f, v * 160.0f + 13.0f * randomFromId(id, 1), d.period(8, 160), layer(kBoardGrain), 3);
            h += 0.04f * grain;
            const float seamDist = lineDistance(v, p.boards);
            const float seam = 1.0f - smoothstep(0.0008f, 0.0008f + d.aa(), seamDist);
            h += 0.08f * seam; // concrete squeezes into the board gaps: raised fins
            col = col * (1.0f - 0.08f * seam);
        }

        // --- Exposed aggregate ---
        const features::StoneSample st = features::stones(u, v, p.aggregateFreq, layer(kStones),
                                                          p.aggregate * 0.9f, 0.2f, 0.65f, d);
        if (st.coverage > 0.0f) {
            const std::size_t idx = static_cast<std::size_t>(randomFromId(st.id, 10) * kAggregate.size()) % kAggregate.size();
            const Vec3 stoneSrgb = lerp(Vec3{0.55f, 0.53f, 0.50f}, kAggregate[idx], p.aggregateVariation) *
                                   (0.85f + 0.3f * randomFromId(st.id, 11));
            const Vec3 stone = srgbToLinear(saturate(stoneSrgb));
            col = lerp(col, stone, st.coverage * (0.5f + 0.5f * smoothstep(0.0f, 0.3f, st.dome)));
            h = lerp(h, h + 0.08f * st.dome, st.coverage);
            rough = lerp(rough, 0.7f, st.coverage);
        }

        // --- Pores ---
        float pore = 0.0f;
        if (p.pores > 0.0f) {
            pore = features::blobs(u, v, 110, layer(kPores), p.pores * 0.18f, 0.001f, 0.0028f, 0.4f, d);
            h -= 0.2f * pore;
            col = lerp(col, col * 0.45f, pore);
        }

        // --- Joints ---
        float jointDist = 1.0f;
        if (p.joints) {
            if (p.jointsU) jointDist = std::min(jointDist, lineDistance(u + 0.5f / p.jointCount, p.jointCount));
            if (p.jointsV) jointDist = std::min(jointDist, lineDistance(v + 0.5f / p.jointCount, p.jointCount));
        }
        // Chipped joint edges (wear): the joint widens irregularly.
        const float chipNoise = fbm01(u * 60.0f, v * 60.0f, d.period(60), layer(kJointEdge), 3);
        const float halfWidth = 0.0008f + 0.0022f * p.jointWidth;
        const float chipWidth = halfWidth + aw * p.chips * 0.008f * smoothstep(0.45f, 0.8f, chipNoise);
        const float joint = p.joints ? 1.0f - smoothstep(halfWidth, halfWidth + d.aa(), jointDist) : 0.0f;
        const float chipped = p.joints ? (1.0f - smoothstep(chipWidth, chipWidth + d.aa(), jointDist)) * (1.0f - joint) : 0.0f;

        // --- Cracks ---
        float crack = 0.0f;
        if (p.cracks > 0.0f) {
            crack = features::crackNetwork(u, v, 5, layer(kCracks), 0.0009f, 0.22f, d) *
                    features::patchyMask(u, v, p.cracks, 2, layer(kCrackMask), d);
        }

        // --- Wear ---
        float stain = 0.0f, dirtAmount = 0.0f, efflo = 0.0f, spall = 0.0f;
        if (wearOn) {
            spall = features::blobs(u, v, 5, layer(kSpalls), p.chips * 0.5f, 0.01f, 0.035f, 0.25f, d);
            stain = features::blobs(u, v, 3, layer(kStains), p.stains * 0.9f, 0.06f, 0.18f, 0.5f, d);
            const float grime = fbm01(u * 6.0f, v * 6.0f, d.period(6), layer(kDirt), 4);
            dirtAmount = p.dirt * smoothstep(0.35f, 0.75f, grime);
            const float nearJoint = 1.0f - smoothstep(0.0f, 0.03f, jointDist);
            const float effloNoise = fbm01(u * 20.0f, v * 20.0f, d.period(20), layer(kEfflo), 4);
            efflo = p.efflorescence * smoothstep(0.45f, 0.75f, effloNoise) * std::max(nearJoint, crack > 0.0f ? 1.0f : 0.3f * effloNoise);
        }

        // Spalls and chipped edges: rough, lower, aggregate exposed.
        const float broken = std::max(spall, chipped) * aw;
        col = lerp(col, col * 0.85f, broken);
        h -= 0.15f * broken;
        rough = lerp(rough, 0.95f, broken);

        // Joint: deep, dark, with dirt and moss (wear).
        if (joint > 0.0f) {
            const float moss = aw * p.dirt * fbm01(u * 40.0f, v * 40.0f, d.period(40), layer(kMoss), 3);
            const Vec3 jointCol = lerp(srgbToLinear(Vec3{0.12f, 0.11f, 0.10f}), mossColor, smoothstep(0.3f, 0.7f, moss));
            col = lerp(col, jointCol, joint);
            h = lerp(h, 0.05f, joint);
            rough = lerp(rough, 0.95f, joint);
        }
        col = lerp(col, col * 0.3f, crack);
        h = lerp(h, 0.2f, crack);

        // Water stains darken; dirt collects in low areas; efflorescence is chalky white.
        col = lerp(col, col * 0.72f, aw * stain);
        const float low = saturate((0.5f - h) * 3.0f);
        col = lerp(col, dirtColor * 0.6f, aw * dirtAmount * (0.25f + 0.75f * std::max(low, pore)));
        col = lerp(col, srgbToLinear(Vec3{0.88f, 0.87f, 0.84f}), aw * efflo * 0.8f);
        rough = lerp(rough, 0.97f, aw * efflo);

        out.albedo.at(x, y, 0) = saturate(col.x);
        out.albedo.at(x, y, 1) = saturate(col.y);
        out.albedo.at(x, y, 2) = saturate(col.z);
        out.height.at(x, y) = saturate(h);
        out.roughness.at(x, y) = saturate(rough);
        out.wear.at(x, y) = saturate(std::max({spall, chipped, stain * 0.7f, dirtAmount * 0.6f, efflo}));
    });
}

} // namespace md
