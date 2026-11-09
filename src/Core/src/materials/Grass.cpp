#include "materials/Grass.h"

#include "materials/Features.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

using noise::fbm;
using noise::fbm01;
using noise::randomFromId;

enum Layer : std::uint32_t {
    kBlades = 1, kClump, kFlow, kSoil, kWeeds, kWeedLeaves, kFlowers, kLeaves, kPath, kDry, kBare, kDryBlades,
};

constexpr int kLayerCount = 6;

struct Palette {
    Vec3 base, tip, tipAlt; ///< sRGB: dark base, bright tip, alternative tip hue
};

Palette palette(const std::string& id)
{
    if (id == "dry") return {{0.20f, 0.20f, 0.08f}, {0.66f, 0.62f, 0.34f}, {0.52f, 0.55f, 0.24f}};
    if (id == "summer") return {{0.10f, 0.18f, 0.05f}, {0.55f, 0.62f, 0.22f}, {0.40f, 0.55f, 0.16f}};
    return {{0.05f, 0.14f, 0.03f}, {0.42f, 0.62f, 0.14f}, {0.30f, 0.52f, 0.12f}}; // lush
}

struct Params {
    Palette pal;
    float density = 0, length = 0, width = 0, variation = 0, clumping = 0, dryBlades = 0;
    float weeds = 0, flowers = 0, leaves = 0, soil = 0;
    int freq = 0;
    float paths = 0, dryPatches = 0, bareSoil = 0;
    bool pathVertical = true;
};

Params readParams(const GenerateContext& ctx)
{
    const ParamReader& r = ctx.params();
    Params p;
    p.pal = palette(r.getEnum("palette"));
    p.density = r.getFloat("blade_density");
    p.length = lerp(0.015f, 0.07f, r.getFloat("blade_length"));
    p.width = lerp(0.0012f, 0.0045f, r.getFloat("blade_width"));
    p.variation = r.getFloat("color_variation");
    p.clumping = r.getFloat("clumping");
    p.dryBlades = r.getFloat("dry_blades");
    p.weeds = r.getFloat("weeds");
    p.flowers = r.getFloat("flowers");
    p.leaves = r.getFloat("leaves");
    p.soil = r.getFloat("soil_visibility");
    // Grid cell ~ 0.75 x blade length, so a 5 x 5 neighbourhood contains every blade that can reach a pixel.
    p.freq = noise::frequency(1.0f / (p.length * 0.75f));
    const float w = ctx.wearAmount();
    p.paths = r.getFloat("trampled_paths") * w;
    p.dryPatches = r.getFloat("dry_patches") * w;
    p.bareSoil = r.getFloat("bare_soil") * w;
    p.pathVertical = r.getEnum("path_direction") == "vertical";
    return p;
}

struct Hit {
    float height = -1.0f;
    Vec3 color;
    float roughness = 0.6f;
};

/// Distance from p to segment ab and the parameter t along it.
float segmentDistance(Vec2 p, Vec2 a, Vec2 b, float& t)
{
    const Vec2 ab = b - a, ap = p - a;
    t = saturate(dot(ap, ab) / std::max(dot(ab, ab), 1e-12f));
    return length(ap - ab * t);
}

} // namespace

GrassMaterial::GrassMaterial()
{
    auto& s = schema_;
    s.addFloat("blade_density", "Density", 0.8, 0.0, 1.0)
        .inGroup("Blades")
        .tip("How densely the grass grows.");
    s.addFloat("blade_length", "Blade length", 0.45, 0.0, 1.0)
        .inGroup("Blades")
        .tip("Length of the blades (short lawn to long grass).");
    s.addFloat("blade_width", "Blade width", 0.4, 0.0, 1.0)
        .inGroup("Blades")
        .tip("Width of the blades.");
    s.addEnum("palette", "Palette", {{"lush", "Lush green"}, {"summer", "Summer"}, {"dry", "Dry"}}, "lush")
        .inGroup("Blades")
        .tip("Colour palette of the grass.");
    s.addFloat("color_variation", "Colour variation", 0.5, 0.0, 1.0)
        .inGroup("Blades")
        .tip("Variation in hue and brightness between blades.");
    s.addFloat("clumping", "Clumping", 0.5, 0.0, 1.0)
        .inGroup("Blades")
        .tip("How much the blades grow in tufts with gaps between them.");
    s.addFloat("dry_blades", "Dry blades", 0.2, 0.0, 1.0)
        .inGroup("Blades")
        .tip("Straw-coloured dead blades mixed in.");

    s.addFloat("weeds", "Weeds & clover", 0.2, 0.0, 1.0)
        .inGroup("Extras")
        .tip("Patches of clover and small broadleaf weeds.");
    s.addFloat("flowers", "Flowers", 0.1, 0.0, 1.0)
        .inGroup("Extras")
        .tip("Small white and yellow flowers (daisies, buttercups).");
    s.addFloat("leaves", "Fallen leaves", 0.15, 0.0, 1.0)
        .inGroup("Extras")
        .tip("Brown fallen leaves lying on top of the grass.");
    s.addFloat("soil_visibility", "Visible soil", 0.15, 0.0, 1.0)
        .inGroup("Extras")
        .tip("How much bare soil shows between the blades.");

    s.addFloat("trampled_paths", "Trampled path", 0.5, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("A path where the grass is flattened, worn and partly bare.");
    s.addEnum("path_direction", "Path direction", {{"vertical", "Vertical"}, {"horizontal", "Horizontal"}}, "vertical")
        .inGroup("Wear")
        .wear()
        .tip("Direction of the trampled path.");
    s.addFloat("dry_patches", "Dry patches", 0.4, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Patches of dry, dead grass.");
    s.addFloat("bare_soil", "Bare soil", 0.3, 0.0, 1.0)
        .inGroup("Wear")
        .wear()
        .tip("Patches where the grass is gone.");
}

void GrassMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const Params p = readParams(ctx);
    const features::Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();
    auto layer = [seed](Layer l) { return noise::subSeed(seed, l); };
    const float aw = ctx.applyWear() ? 1.0f : 0.0f;
    const bool wearOn = ctx.wearEnabled();
    const int F = p.freq;
    const float cell = 1.0f / static_cast<float>(F);
    const int clumpFreq = std::max(1, F / 4);
    const Vec3 baseLin = srgbToLinear(p.pal.base);
    const Vec3 tipLin = srgbToLinear(p.pal.tip);
    const Vec3 tipAltLin = srgbToLinear(p.pal.tipAlt);
    const Vec3 straw = srgbToLinear(Vec3{0.72f, 0.64f, 0.42f});
    const Vec3 soilDark = srgbToLinear(Vec3{0.17f, 0.13f, 0.09f});
    const Vec3 soilLight = srgbToLinear(Vec3{0.36f, 0.29f, 0.21f});
    const Vec3 thatch = srgbToLinear(Vec3{0.09f, 0.13f, 0.04f}); // shadowed depth of the turf

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        // --- Wear masks ---
        float path = 0.0f, dryPatch = 0.0f, bare = 0.0f;
        if (wearOn) {
            const float across = p.pathVertical ? u : v, along = p.pathVertical ? v : u;
            const noise::Period pa{d.seamlessAxis(!p.pathVertical) ? 3 : 0, 0};
            const float wobble = 0.05f * fbm(along * 3.0f, 0.7f, pa, layer(kPath), 3);
            path = features::bands(across + wobble, 1, 0.5f, 0.06f + 0.08f * p.paths, 0.6f) * saturate(p.paths * 1.5f);
            dryPatch = features::patchyMask(u, v, p.dryPatches, 3, layer(kDry), d);
            bare = features::patchyMask(u, v, p.bareSoil * 0.8f, 4, layer(kBare), d);
        }
        const float pathA = path * aw, dryA = dryPatch * aw, bareA = bare * aw;

        // --- Ground: thatch and soil ---
        const float soilNoise = fbm01(u * 16.0f, v * 16.0f, d.period(16), layer(kSoil), 4);
        const float soilAmount = saturate(p.soil + 0.6f * bareA + 0.5f * pathA * path);
        Hit top;
        top.height = 0.04f + 0.05f * soilNoise;
        top.color = lerp(thatch * (0.7f + 0.6f * soilNoise), lerp(soilDark, soilLight, soilNoise),
                         smoothstep(0.75f, 0.95f, soilNoise + soilAmount * 0.9f));
        top.roughness = 0.95f;

        // --- Blades ---
        const float presenceBase = p.density * (1.0f - 0.85f * bareA) * (1.0f - 0.5f * pathA);
        const int cx = static_cast<int>(std::floor(u * F)), cy = static_cast<int>(std::floor(v * F));
        for (int l = 0; l < kLayerCount; ++l) {
            const std::uint32_t lseed = noise::hash32(layer(kBlades) + static_cast<std::uint32_t>(l) * 0x632be5abU);
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int gx = cx + dx, gy = cy + dy;
                    const std::uint32_t id = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? F : 0),
                                                             noise::wrapIndex(gy, d.seamlessV ? F : 0), lseed);
                    const Vec2 root{(static_cast<float>(gx) + randomFromId(id, 1)) * cell,
                                    (static_cast<float>(gy) + randomFromId(id, 2)) * cell};
                    // Clumps: blade density follows a low-frequency field.
                    const float clump = noise::value(root.x * clumpFreq * 2.0f, root.y * clumpFreq * 2.0f,
                                                     d.period(clumpFreq * 2), layer(kClump));
                    const float presence = presenceBase * lerp(1.0f, smoothstep(0.25f, 0.7f, clump) * 1.4f, p.clumping);
                    if (randomFromId(id, 0) >= presence) continue;

                    // Direction: locally coherent flow mixed with randomness; trampled blades lie along the path.
                    const float flow = noise::value(root.x * clumpFreq, root.y * clumpFreq, d.period(clumpFreq), layer(kFlow)) * kTwoPi * 2.0f;
                    float angle = lerp(randomFromId(id, 3) * kTwoPi, flow, 0.4f);
                    const float pathAngle = (p.pathVertical ? 0.5f : 0.0f) * kPi + (randomFromId(id, 4) < 0.5f ? 0.0f : kPi);
                    const float flat = pathA;
                    angle = lerp(angle, pathAngle + (randomFromId(id, 5) - 0.5f) * 0.6f, flat);
                    // Standing blades appear shorter from above; flattened ones show their full length.
                    const float tilt = lerp(0.35f + 0.65f * randomFromId(id, 6), 1.0f, flat);
                    const float len = p.length * (0.6f + 0.8f * randomFromId(id, 7)) * tilt;
                    const Vec2 tip{root.x + std::cos(angle) * len, root.y + std::sin(angle) * len};

                    float t = 0.0f;
                    const float dist = segmentDistance({u, v}, root, tip, t);
                    const float w = p.width * (0.7f + 0.6f * randomFromId(id, 8)) * (1.0f - 0.85f * t * t);
                    if (dist > w + d.aa()) continue;
                    const float cover = 1.0f - smoothstep(w, w + d.aa(), dist);

                    const float layerH = (static_cast<float>(l) + 1.0f) / kLayerCount;
                    const float h = 0.15f + 0.45f * layerH * (1.0f - 0.7f * flat) + 0.35f * t * (1.0f - flat) + 0.1f * flat;
                    if (h <= top.height || cover < 0.5f) continue;

                    // Colour: dark base to bright tip; dry blades are straw coloured.
                    const float hueMix = randomFromId(id, 9) * p.variation;
                    Vec3 c = lerp(baseLin, lerp(tipLin, tipAltLin, hueMix), smoothstep(0.0f, 0.9f, t * 0.6f + layerH * 0.5f));
                    c = c * (1.0f + (randomFromId(id, 10) - 0.5f) * 0.5f * p.variation);
                    const float dryChance = p.dryBlades * 0.35f + dryA * 0.9f + pathA * 0.3f;
                    if (randomFromId(id, 11) < dryChance) {
                        c = straw * (0.6f + 0.4f * t) * (0.85f + 0.3f * randomFromId(id, 12));
                    }
                    c = lerp(c, adjustSaturation(c, 0.6f) * 1.2f, pathA * 0.5f); // trampled: paler
                    top.height = h;
                    top.color = c;
                    top.roughness = 0.55f + 0.2f * (1.0f - t);
                }
            }
        }

        // --- Clover and broadleaf weeds: patches of round leaves on top ---
        if (p.weeds > 0.0f) {
            const float patch = features::blobs(u, v, 5, layer(kWeeds), p.weeds * 0.6f, 0.04f, 0.1f, 0.3f, d);
            if (patch > 0.0f) {
                const int lf = noise::frequency(1.0f / (p.length * 0.35f));
                const auto vc = noise::voronoi(u * lf, v * lf, d.period(lf), layer(kWeedLeaves), 0.9f);
                const float leaf = 1.0f - smoothstep(0.32f, 0.38f, vc.f1);
                const float h = 0.55f + 0.3f * (1.0f - vc.f1);
                if (leaf * patch > 0.5f && h > top.height && randomFromId(vc.id, 0) < 0.8f) {
                    top.height = h;
                    top.color = srgbToLinear(Vec3{0.24f, 0.44f, 0.15f}) * (0.7f + 0.5f * (1.0f - vc.f1)) *
                                (0.85f + 0.3f * randomFromId(vc.id, 1));
                    top.roughness = 0.5f;
                }
            }
        }

        // --- Flowers ---
        if (p.flowers > 0.0f) {
            std::uint32_t fid = 0;
            const float flower = features::blobs(u, v, 30, layer(kFlowers), p.flowers * 0.5f, 0.003f, 0.006f, 0.3f, d, &fid);
            if (flower > 0.5f) {
                const bool daisy = randomFromId(fid, 9) < 0.6f;
                const Vec3 petal = daisy ? Vec3{0.92f, 0.92f, 0.88f} : Vec3{0.95f, 0.80f, 0.15f};
                top.color = srgbToLinear(flower > 0.92f && daisy ? Vec3{0.95f, 0.75f, 0.15f} : petal);
                top.height = std::max(top.height, 0.95f);
                top.roughness = 0.6f;
            }
        }

        // --- Fallen leaves: pointed ellipses on top ---
        if (p.leaves > 0.0f) {
            constexpr int lc = 7;
            const int lx = static_cast<int>(std::floor(u * lc)), ly = static_cast<int>(std::floor(v * lc));
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int gx = lx + dx, gy = ly + dy;
                    const std::uint32_t id = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? lc : 0),
                                                             noise::wrapIndex(gy, d.seamlessV ? lc : 0), layer(kLeaves));
                    if (randomFromId(id, 0) >= p.leaves * 0.7f) continue;
                    const Vec2 c{(static_cast<float>(gx) + randomFromId(id, 1)) / lc, (static_cast<float>(gy) + randomFromId(id, 2)) / lc};
                    const float a = randomFromId(id, 3) * kTwoPi;
                    const float size = 0.012f + 0.02f * randomFromId(id, 4);
                    const float qx = ((u - c.x) * std::cos(a) + (v - c.y) * std::sin(a)) / size;
                    const float qy = (-(u - c.x) * std::sin(a) + (v - c.y) * std::cos(a)) / size;
                    const float halfWidth = 0.45f * (1.0f - qx * qx);
                    if (std::abs(qx) < 1.0f && std::abs(qy) < halfWidth) {
                        const float vein = 1.0f - smoothstep(0.0f, 0.06f, std::abs(qy));
                        const Vec3 brown = lerp(Vec3{0.55f, 0.30f, 0.12f}, Vec3{0.70f, 0.50f, 0.22f}, randomFromId(id, 5));
                        top.color = srgbToLinear(brown) * (0.9f - 0.25f * vein);
                        top.height = std::max(top.height, 0.85f + 0.05f * (1.0f - std::abs(qx)));
                        top.roughness = 0.7f;
                    }
                }
            }
        }

        out.albedo.at(x, y, 0) = saturate(top.color.x);
        out.albedo.at(x, y, 1) = saturate(top.color.y);
        out.albedo.at(x, y, 2) = saturate(top.color.z);
        out.height.at(x, y) = saturate(top.height);
        out.roughness.at(x, y) = saturate(top.roughness);
        // Deep parts of the turf get extra occlusion on top of the height-based AO.
        out.occlusion.at(x, y) = 0.55f + 0.45f * smoothstep(0.05f, 0.7f, top.height);
        out.wear.at(x, y) = saturate(std::max({path, dryPatch, bare}));
    });
}

} // namespace md
