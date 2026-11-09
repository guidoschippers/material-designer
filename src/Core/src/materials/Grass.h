#pragma once

#include "md/core/Material.h"

namespace md {

/// Lawn seen from above: several layers of individual tapered blades (dark at the base, light at
/// the tips), clumping, dry straw blades, clover/weeds, small flowers, fallen leaves and soil.
/// Wear: trampled paths, dry patches and bare soil.
class GrassMaterial final : public Material {
public:
    GrassMaterial();

    std::string_view id() const override { return "grass"; }
    std::string_view displayName() const override { return "Grass"; }
    std::string_view description() const override
    {
        return "Lawn with individual blades, clumps, dry blades, weeds, flowers and fallen leaves.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.02f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
