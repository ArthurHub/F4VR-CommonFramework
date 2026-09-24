#include "EffectShaderMaterials.h"

namespace f4cf::f4vr
{
    namespace
    {
        template <class Fn>
        void forEachGeometry(RE::NiAVObject* node, Fn&& fn)
        {
            if (const auto geometry = node->IsGeometry()) {
                fn(geometry);
                return;
            }
            if (const auto niNode = node->IsNode()) {
                for (const auto& child : niNode->children) {
                    if (child) {
                        forEachGeometry(child.get(), fn);
                    }
                }
            }
        }

        /**
         * The shape's effect shader property, or null when it is shaded by anything else.
         */
        RE::BSShaderProperty* getEffectShaderProperty(RE::BSGeometry* geometry)
        {
            const auto property = geometry->GetRuntimeData().properties[1].get();
            const auto rtti = property ? property->GetRTTI() : nullptr;
            if (!rtti || std::string_view(rtti->GetName()) != "BSEffectShaderProperty") {
                return nullptr;
            }
            return static_cast<RE::BSShaderProperty*>(property);
        }

        /**
         * Whether no other shape can see this material: kept out of the shared material cache and referenced only by
         * the property holding it — so writing its values changes that one shape.
         */
        bool isPrivateMaterial(const RE::BSShaderMaterial* material)
        {
            return material->hashKey == RE::BSShaderMaterial::UNIQUE_HASH_KEY && material->QRefCount() == 1;
        }
    }

    /**
     * Swap each shared material for a unique copy through the engine's own SetMaterial(unique), which copies the current
     * material before releasing it. A shape the engine won't give a copy is left as authored, with an error.
     */
    int makeEffectShaderMaterialsPrivate(RE::NiAVObject* node)
    {
        if (!node) {
            return 0;
        }

        int privateShapes = 0;
        forEachGeometry(node, [&](RE::BSGeometry* geometry) {
            const auto property = getEffectShaderProperty(geometry);
            if (!property || !property->material) {
                return;
            }
            if (!isPrivateMaterial(property->material)) {
                property->SetMaterial(property->material, true);
                if (!property->material || !isPrivateMaterial(property->material)) {
                    logger::error("'{}' could not get a material of its own; left as authored", geometry->name.c_str());
                    return;
                }
                logger::info("'{}' shared its material; switched to a private copy", geometry->name.c_str());
            }
            ++privateShapes;
        });
        return privateShapes;
    }

    int forEachEffectShaderMaterial(RE::NiAVObject* node, const std::function<void(RE::BSEffectShaderMaterial& material, RE::BSGeometry& geometry)>& fn)
    {
        if (!node) {
            return 0;
        }

        int visitedShapes = 0;
        forEachGeometry(node, [&](RE::BSGeometry* geometry) {
            const auto property = getEffectShaderProperty(geometry);
            if (!property || !property->material) {
                return;
            }
            if (!isPrivateMaterial(property->material)) {
                logger::warn("'{}' shares its material; left unchanged (see makeEffectShaderMaterialsPrivate)", geometry->name.c_str());
                return;
            }
            fn(*static_cast<RE::BSEffectShaderMaterial*>(property->material), *geometry);
            ++visitedShapes;
        });
        return visitedShapes;
    }
}
