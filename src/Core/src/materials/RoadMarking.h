#pragma once

#include "md/core/Material.h"

namespace md {

/// Overlay: painted road marking (a single dash, or a continuous line that tiles vertically).
/// The paint follows the texture of the surface below: thin on stone tops, missing in gaps,
/// and wears off the tops first.
class RoadMarkingMaterial final : public Material {
public:
    RoadMarkingMaterial();

    std::string_view id() const override { return "road_marking"; }
    std::string_view displayName() const override { return "Road marking"; }
    std::string_view description() const override
    {
        return "Overlay: painted road marking (dash or continuous line) with paint that follows the surface and wears.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.006f; }
    MapSet outputMaps() const override { return MapSet::all(); }
    TileAxes tileAxes(const ParamReader& params) const override;

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
