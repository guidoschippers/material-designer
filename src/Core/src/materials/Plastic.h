#pragma once

#include "md/core/Material.h"

namespace md {

/// Moulded ABS / polystyrene of late-90s consumer electronics (VCRs, consoles, controllers):
/// a uniform colour, from glossy to the fine spark-eroded (EDM) matte texture of moulded housings,
/// with optional faint flow lines and sink marks.
/// Wear: fine scratches and scuffs, hand-polished glossy areas, dust, grime in low areas,
/// UV yellowing and small dings.
class PlasticMaterial final : public Material {
public:
    PlasticMaterial();

    std::string_view id() const override { return "plastic"; }
    std::string_view displayName() const override { return "Plastic"; }
    std::string_view description() const override
    {
        return "Moulded ABS plastic of 90s electronics: glossy to spark-eroded matte texture, flow lines, sink marks.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.003f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
