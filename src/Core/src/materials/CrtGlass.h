#pragma once

#include "md/core/Material.h"

namespace md {

/// Front glass of a 90s CRT television, switched off or as the glass layer over a picture that
/// comes from elsewhere: very smooth dark grey-green glass, an optional anti-glare coating and an
/// optional faint shadow mask / aperture grille behind it. The maps that matter most are roughness
/// and normal; albedo is dark.
/// Wear: dust film, smudges and fingerprints, fine wipe streaks.
class CrtGlassMaterial final : public Material {
public:
    CrtGlassMaterial();

    std::string_view id() const override { return "crt_glass"; }
    std::string_view displayName() const override { return "CRT glass"; }
    std::string_view description() const override
    {
        return "Front glass of a 90s CRT TV: dark tinted smooth glass, anti-glare coating, shadow mask or aperture grille.";
    }
    const ParamSchema& schema() const override { return schema_; }
    float reliefDepth() const override { return 0.001f; }

    void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const override;

private:
    ParamSchema schema_;
};

} // namespace md
