#pragma once

// Shared asphalt surface model (stones, binder, cracks, repairs, wear), used by the Asphalt
// base material and the Asphalt edge overlay so both look identical where they meet.

#include "materials/Features.h"

#include "md/core/Material.h"

#include <cstdint>

namespace md::asphalt {

struct Params {
    int coarseFreq = 0, fineFreq = 0;
    float presence = 0, gap = 0, roundness = 0, exposure = 0, fine = 0, colorVariation = 0;
    Vec3 aggregateSrgb;
    Vec3 binderLin;
    float cracks = 0, hairline = 0, patches = 0;

    bool wearOn = false;
    float applyFactor = 0; // 1 when wear is applied to the base maps
    float wear = 0;
    float wearCracks = 0, raveling = 0, oil = 0, tracks = 0;
    bool tracksVertical = true;
};

struct Sample {
    Vec3 albedo;
    float height = 0, roughness = 0, wear = 0;
};

/// Per-pixel modifiers applied by overlays (all 0 = plain asphalt).
struct Local {
    float binderRich = 0.0f; ///< 0..1: darker, binder-rich surface where stones are coated
    float stoneLoss = 0.0f;  ///< 0..1: probability that a stone has broken out
    float sink = 0.0f;       ///< height reduction (crumbled, sagging edge)
};

/// Adds the "Aggregate" and "Binder" parameter groups (shared keys across asphalt materials).
void addAggregateParams(ParamSchema& schema);
/// Adds the "Damage" and "Wear" parameter groups of the base asphalt.
void addDamageParams(ParamSchema& schema);

/// Reads the parameters. Without damage, only the aggregate/binder keys are read and cracks,
/// repairs and wear stay off.
Params readParams(const GenerateContext& ctx, bool withDamage);

Sample evaluate(const Params& p, const features::Domain& d, std::uint32_t seed, float u, float v,
                const Local& local = {});

} // namespace md::asphalt
