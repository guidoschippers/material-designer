#pragma once

#include "md/core/Material.h"

namespace md {

/// Wood floors and beams: species presets (pine, oak, walnut), flat-sawn annual rings with
/// cathedral figures per plank, knots that bend the rings, open pores (optionally limed),
/// staggered planks with gaps. Wear: scratches, dents, sun fading and worn varnish.
class WoodMaterial final : public Material {
public:
    WoodMaterial();

    std::string_view id() const override { return "wood"; }
    std::string_view displayName() const override { return "Wood"; }
    std::string_view description() const override
    {
        return "Wood planks or boards (pine, oak, walnut) with rings, knots, pores and finish wear.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.003f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
