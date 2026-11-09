// Registration of all built-in materials.
//
// To add a material: create <Name>.h/.cpp in this folder implementing md::Material,
// include the header below and add one registry.add(...) line. Nothing else (GUI, CLI,
// presets) needs to change: they are all driven by the material's parameter schema.

#include "md/core/Registry.h"

#include "materials/Asphalt.h"

namespace md {

void registerBuiltinMaterials(MaterialRegistry& registry)
{
    registry.add(std::make_unique<AsphaltMaterial>());
}

} // namespace md
