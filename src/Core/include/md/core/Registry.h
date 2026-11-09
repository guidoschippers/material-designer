#pragma once

#include "md/core/Material.h"

#include <memory>
#include <string_view>
#include <vector>

namespace md {

/// Holds the available materials in registration order.
class MaterialRegistry {
public:
    /// The registry with all built-in materials (see src/materials/BuiltinMaterials.cpp).
    static const MaterialRegistry& builtin();

    void add(std::unique_ptr<Material> material);

    /// Material by id, or nullptr.
    const Material* find(std::string_view id) const;
    const std::vector<std::unique_ptr<Material>>& all() const { return materials_; }

private:
    std::vector<std::unique_ptr<Material>> materials_;
};

/// Registers every built-in material. Implemented in src/materials/BuiltinMaterials.cpp.
void registerBuiltinMaterials(MaterialRegistry& registry);

} // namespace md
