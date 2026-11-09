#include "md/core/Material.h"

#include "md/core/Error.h"
#include "md/core/Math.h"
#include "md/image/Filters.h"

#include <string>

namespace md {

const ImageF& MaterialMaps::get(MapType map) const
{
    switch (map) {
    case MapType::Albedo: return albedo;
    case MapType::Normal: return normal;
    case MapType::Roughness: return roughness;
    case MapType::Height: return height;
    case MapType::AmbientOcclusion: return ao;
    case MapType::Wear: return wear;
    }
    return albedo;
}

SurfaceMaps::SurfaceMaps(int s)
    : size(s), albedo(s, s, 3, srgbToLinear(0.5f)), height(s, s, 1, 0.5f), roughness(s, s, 1, 0.5f),
      wear(s, s, 1, 0.0f), occlusion(s, s, 1, 1.0f)
{
}

void GenerateContext::throwIfCancelled() const
{
    if (stop_.stop_requested()) {
        throw Cancelled();
    }
}

MaterialMaps Material::generate(const MaterialSettings& settings, const GenerateOptions& options) const
{
    const int size = options.sizeOverride > 0 ? options.sizeOverride : settings.common.size;
    if (!isValidSize(size)) {
        throw Error(ErrorCode::InvalidArgument, "invalid size " + std::to_string(size) +
                                                    " (must be a power of two between " + std::to_string(kMinSize) +
                                                    " and " + std::to_string(kMaxSize) + ")");
    }

    const ParamReader params(schema(), settings.params);
    const GenerateContext ctx(settings.common, params, size, options.stop);

    SurfaceMaps surface(size);
    generateSurface(ctx, surface);
    ctx.throwIfCancelled();

    const bool wrap = settings.common.seamless;
    const float depth = reliefDepth() * static_cast<float>(settings.common.normalStrength);

    MaterialMaps maps;
    maps.size = size;
    maps.normal = normalFromHeight(surface.height, depth, wrap, settings.common.normalConvention, options.stop);
    // AO uses the material's natural relief (not the user's normal strength) so it stays physically plausible.
    maps.ao = ambientOcclusion(surface.height, reliefDepth(), wrap, options.stop);
    for (std::size_t i = 0; i < maps.ao.data().size(); ++i) {
        maps.ao.data()[i] *= surface.occlusion.data()[i];
    }
    maps.albedo = std::move(surface.albedo);
    maps.roughness = std::move(surface.roughness);
    maps.height = std::move(surface.height);
    maps.wear = settings.common.wear ? std::move(surface.wear) : ImageF(size, size, 1, 0.0f);
    ctx.throwIfCancelled();
    return maps;
}

} // namespace md
