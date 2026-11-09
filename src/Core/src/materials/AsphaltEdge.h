#pragma once

#include "md/core/Material.h"

namespace md {

/// Overlay for the asphalt next to a kerb: a darker, binder-rich band with broken-out stones,
/// a crack along its inner boundary and dirt against the kerb. The kerb is on the left (u = 0);
/// the texture tiles vertically only and fades to transparent towards the right, where it blends
/// into the base asphalt. Shares the stone parameters (and look) of the Asphalt material.
class AsphaltEdgeMaterial final : public Material {
public:
    AsphaltEdgeMaterial();

    std::string_view id() const override { return "asphalt_edge"; }
    std::string_view displayName() const override { return "Asphalt edge"; }
    std::string_view description() const override
    {
        return "Overlay: crumbling, binder-rich asphalt edge along a kerb (kerb on the left, tiles vertically).";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.01f; }
    MapSet outputMaps() const override { return MapSet::all(); }
    TileAxes tileAxes(const ParamReader&) const override { return {false, true}; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
