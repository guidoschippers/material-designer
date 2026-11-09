#pragma once

#include "md/core/Material.h"

namespace md {

/// Outside of a brown corrugated shipping box (single-wall board, kraft liner): kraft paper with
/// cloudy formation, fine fibres and recycled specks; the flutes of the corrugation show faintly
/// through the liner as parallel ridges.
///
/// Orientation: the flutes run along the V axis (each ridge is a vertical line of constant u,
/// repeating across u). The liner's paper fibres lie mostly along U, across the flutes, as on
/// real board (the paper's machine direction is perpendicular to the flutes).
///
/// Wear: scuffs (abraded, lighter, fuzzy fibres), creases and dents (crushed flutes), water stains
/// with tide-mark rings, dust/grime and faint parcel-tape residue.
class CardboardMaterial final : public Material {
public:
    CardboardMaterial();

    std::string_view id() const override { return "cardboard"; }
    std::string_view displayName() const override { return "Cardboard"; }
    std::string_view description() const override
    {
        return "Brown corrugated cardboard: kraft liner with fibres, recycled specks and faint flutes.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.006f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
