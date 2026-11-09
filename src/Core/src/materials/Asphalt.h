#pragma once

#include "md/core/Material.h"

namespace md {

/// Road asphalt with exposed gravel aggregate in a bitumen binder: stones of two sizes, binder
/// mottling, meandering cracks with crumbled edges, hairline cracks and binder-rich repairs.
/// Wear: crumbling/alligator cracks, raveling (lost stones), oil stains and polished tyre tracks.
class AsphaltMaterial final : public Material {
public:
    AsphaltMaterial();

    std::string_view id() const override { return "asphalt"; }
    std::string_view displayName() const override { return "Asphalt"; }
    std::string_view description() const override
    {
        return "Road asphalt with exposed gravel, cracks, repairs and traffic wear.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.01f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
