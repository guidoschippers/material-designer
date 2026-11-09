#pragma once

#include "md/core/Material.h"

namespace md {

/// Concrete slab: cement paste with cloudy tone variation, broom finish or formwork board
/// imprint, sparse exposed aggregate, pores and saw-cut expansion joints.
/// Wear: chipped joints/spalls, water stains, dirt in pores and joints, efflorescence.
class ConcreteMaterial final : public Material {
public:
    ConcreteMaterial();

    std::string_view id() const override { return "concrete"; }
    std::string_view displayName() const override { return "Concrete"; }
    std::string_view description() const override
    {
        return "Concrete slab with broom finish or formwork boards, exposed aggregate, pores and joints.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.005f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
