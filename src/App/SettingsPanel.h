#pragma once

// Settings controls generated from the material's parameter schema (no per-material UI code).

#include "md/core/Material.h"

/// Draws the common settings. Returns true when a value changed.
bool drawCommonSettings(md::CommonSettings& common, md::MapSet availableMaps);

/// Draws one control per schema parameter, grouped by ParamDef::group. Wear-only parameters
/// are disabled while wear is off; `enabledBy` dependencies are honoured. Right-click resets
/// a parameter to its default. Returns true when a value changed.
bool drawMaterialParameters(const md::Material& material, md::MaterialSettings& settings);
