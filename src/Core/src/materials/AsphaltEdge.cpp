#include "materials/AsphaltEdge.h"

#include "materials/AsphaltSurface.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

enum Layer : std::uint32_t { kWobble = 101, kJag, kRich, kCrackSegment, kCrackWidth, kDirt, kDirtEdge };

struct EdgeParams {
    float width = 0, crumble = 0, richness = 0, stoneLoss = 0, boundaryCrack = 0, kerbDirt = 0, fade = 0;
    float wearCrumble = 0; // wear contribution (0 when wear is off)
    bool applyWear = false;
};

} // namespace

AsphaltEdgeMaterial::AsphaltEdgeMaterial()
{
    asphalt::addAggregateParams(schema_);
    auto& s = schema_;
    s.addFloat("edge_width", "Band width", 0.3, 0.05, 0.6)
        .inGroup("Edge")
        .tip("Width of the damaged band next to the kerb, as a fraction of the texture width.");
    s.addFloat("crumble", "Crumbling", 0.6, 0.0, 1.0)
        .inGroup("Edge")
        .tip("Irregularity of the band's boundary and how much the surface has sagged and broken up.");
    s.addFloat("binder_richness", "Binder richness", 0.7, 0.0, 1.0)
        .inGroup("Edge")
        .tip("How dark and binder-rich the band is (stones coated with bitumen).");
    s.addFloat("stone_loss", "Lost stones", 0.45, 0.0, 1.0)
        .inGroup("Edge")
        .tip("Stones broken out of the band, most of them near the kerb.");
    s.addFloat("boundary_crack", "Boundary crack", 0.5, 0.0, 1.0)
        .inGroup("Edge")
        .tip("Crack along the inner boundary of the band.");
    s.addFloat("kerb_dirt", "Kerb dirt", 0.4, 0.0, 1.0)
        .inGroup("Edge")
        .tip("Soil and debris collected against the kerb.");
    s.addFloat("fade", "Fade width", 0.35, 0.05, 1.0)
        .inGroup("Edge")
        .tip("Width of the transparent fade into the surrounding asphalt, relative to the band width.");
    s.addFloat("wear_crumble", "Extra crumbling", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("With wear: a wider band and more broken-out stones.");
}

void AsphaltEdgeMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const asphalt::Params ap = asphalt::readParams(ctx, false);
    const ParamReader& r = ctx.params();
    EdgeParams e;
    e.width = r.getFloat("edge_width");
    e.crumble = r.getFloat("crumble");
    e.richness = r.getFloat("binder_richness");
    e.stoneLoss = r.getFloat("stone_loss");
    e.boundaryCrack = r.getFloat("boundary_crack");
    e.kerbDirt = r.getFloat("kerb_dirt");
    e.fade = r.getFloat("fade");
    e.wearCrumble = r.getFloat("wear_crumble") * ctx.wearAmount();
    e.applyWear = ctx.applyWear();

    const features::Domain d{false, ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    // Noise along the edge: x = v * f, periodic in v only.
    auto alongPeriod = [&](int f) { return noise::Period{ctx.seamlessV() ? f : 0, 0}; };
    const Vec3 soil = srgbToLinear(Vec3{0.30f, 0.24f, 0.18f});

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        const float extra = e.applyWear ? e.wearCrumble : 0.0f;
        const float crumble = saturate(e.crumble + 0.5f * extra);

        // Irregular band boundary: large wobble plus jagged small-scale breaks.
        const float wobble = noise::fbm(v * 4.0f, 1.7f, alongPeriod(4), layer(kWobble), 4);
        const float jag = noise::fbm(v * 24.0f, u * 24.0f, alongPeriod(24), layer(kJag), 3);
        const float width = std::max(0.02f, e.width * (1.0f + 0.3f * extra) *
                                                 (1.0f + 0.35f * crumble * wobble + 0.25f * crumble * jag));
        const float t = u / width; // 0 at the kerb, 1 at the band boundary
        const float band = 1.0f - smoothstep(0.85f, 1.0f, t);

        asphalt::Local local;
        const float richNoise = noise::fbm01(v * 10.0f, u * 10.0f, alongPeriod(10), layer(kRich), 3);
        local.binderRich = band * e.richness * (0.75f + 0.25f * (1.0f - t)) * (0.8f + 0.2f * richNoise);
        local.stoneLoss = band * saturate(e.stoneLoss + extra * 0.5f) * (0.2f + 0.8f * std::pow(saturate(1.0f - t), 1.5f));
        local.sink = band * 0.06f * crumble * (1.0f - 0.5f * t);

        asphalt::Sample s = asphalt::evaluate(ap, d, seed, u, v, local);

        // Crack along the band boundary, interrupted in places.
        float crackLine = 0.0f;
        if (e.boundaryCrack > 0.0f) {
            const float dist = std::abs(t - 1.0f) * width;
            const float segment = smoothstep(0.35f, 0.5f, noise::fbm01(v * 3.0f, 0.5f, alongPeriod(3), layer(kCrackSegment), 3) +
                                                              0.3f * e.boundaryCrack);
            const float w = 0.0035f * (0.5f + e.boundaryCrack) *
                            (0.5f + noise::value(v * 40.0f, 3.3f, alongPeriod(40), layer(kCrackWidth)));
            crackLine = (1.0f - smoothstep(w * 0.5f, w * 0.5f + d.aa(), dist)) * segment;
            s.albedo = lerp(s.albedo, s.albedo * 0.12f, crackLine);
            s.height = lerp(s.height, 0.05f, crackLine);
            s.roughness = lerp(s.roughness, 0.95f, crackLine);
        }

        // Soil and debris against the kerb, filling the gaps between the stones.
        if (e.kerbDirt > 0.0f) {
            const float edgeNoise = 0.015f * noise::fbm(v * 12.0f, 0.3f, alongPeriod(12), layer(kDirtEdge), 3);
            const float reach = 0.02f + 0.08f * e.kerbDirt;
            const float zone = 1.0f - smoothstep(reach * 0.3f, reach, u + edgeNoise);
            const float patches = noise::fbm01(u * 30.0f, v * 30.0f, alongPeriod(30), layer(kDirt), 4);
            const float dirt = zone * e.kerbDirt * smoothstep(0.3f, 0.6f, patches + 0.2f * (1.0f - s.height));
            s.albedo = lerp(s.albedo, soil * (0.8f + 0.4f * patches), dirt * 0.9f);
            s.roughness = lerp(s.roughness, 0.95f, dirt);
            s.height = lerp(s.height, std::max(s.height, 0.34f), dirt * 0.6f);
        }

        // Opaque band, then a soft fade into the base asphalt; transparent at the far edge.
        float opacity = 1.0f - smoothstep(1.0f, 1.0f + e.fade, t);
        opacity *= 1.0f - smoothstep(0.9f, 0.99f, u);

        out.albedo.at(x, y, 0) = s.albedo.x;
        out.albedo.at(x, y, 1) = s.albedo.y;
        out.albedo.at(x, y, 2) = s.albedo.z;
        out.height.at(x, y) = saturate(s.height);
        out.roughness.at(x, y) = s.roughness;
        out.opacity.at(x, y) = opacity;
        out.wear.at(x, y) = saturate(std::max(local.stoneLoss, crackLine)) * (ctx.wearEnabled() ? 1.0f : 0.0f);
    });
}

} // namespace md
