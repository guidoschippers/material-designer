#include "md/core/Registry.h"

#include <stdexcept>
#include <string>

namespace md {

const MaterialRegistry& MaterialRegistry::builtin()
{
    static const MaterialRegistry registry = [] {
        MaterialRegistry r;
        registerBuiltinMaterials(r);
        return r;
    }();
    return registry;
}

void MaterialRegistry::add(std::unique_ptr<Material> material)
{
    if (!material) {
        throw std::invalid_argument("MaterialRegistry::add: null material");
    }
    if (find(material->id())) {
        throw std::invalid_argument("MaterialRegistry::add: duplicate material id '" + std::string(material->id()) + "'");
    }
    materials_.push_back(std::move(material));
}

const Material* MaterialRegistry::find(std::string_view id) const
{
    for (const auto& m : materials_) {
        if (m->id() == id) {
            return m.get();
        }
    }
    return nullptr;
}

} // namespace md
