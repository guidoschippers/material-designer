// Registration of all built-in materials.
//
// To add a material: create <Name>.h/.cpp in this folder implementing md::Material,
// include the header below and add one registry.add(...) line. Nothing else (GUI, CLI,
// presets) needs to change: they are all driven by the material's parameter schema.

#include "md/core/Registry.h"

#include "materials/Asphalt.h"
#include "materials/AsphaltEdge.h"
#include "materials/RoadMarking.h"

namespace md {

void registerBuiltinMaterials(MaterialRegistry& registry)
{
    // Base materials
    registry.add(std::make_unique<AsphaltMaterial>());

    // Overlays
    registry.add(std::make_unique<AsphaltEdgeMaterial>());
    registry.add(std::make_unique<RoadMarkingMaterial>());
}

} // namespace md
