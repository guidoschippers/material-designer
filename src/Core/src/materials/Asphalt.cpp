#include "materials/Asphalt.h"

#include "materials/AsphaltSurface.h"

namespace md {

AsphaltMaterial::AsphaltMaterial()
{
    asphalt::addAggregateParams(schema_);
    asphalt::addDamageParams(schema_);
}

void AsphaltMaterial::generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const
{
    const asphalt::Params p = asphalt::readParams(ctx, true);
    const features::Domain d{ctx.seamlessU(), ctx.seamlessV(), ctx.pixelSize()};
    const std::uint32_t seed = ctx.seed();

    ctx.forEachPixel([&](int x, int y, float u, float v) {
        const asphalt::Sample s = asphalt::evaluate(p, d, seed, u, v);
        out.albedo.at(x, y, 0) = s.albedo.x;
        out.albedo.at(x, y, 1) = s.albedo.y;
        out.albedo.at(x, y, 2) = s.albedo.z;
        out.height.at(x, y) = s.height;
        out.roughness.at(x, y) = s.roughness;
        out.wear.at(x, y) = s.wear;
    });
}

} // namespace md
