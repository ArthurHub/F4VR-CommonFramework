#include "SphereStyle.h"

#include "F4VROffsets.h"
#include "F4VRUtils.h"
#include "common/CommonUtils.h"

namespace f4cf::f4vr
{
    namespace
    {
        // The one mesh every preset draws on: additive and two-sided, its look set entirely at runtime.
        constexpr auto SPHERE_NIF = "f4cf\\activation-sphere.nif";
        constexpr auto ACTIVATION_SPHERE_TEXTURE = "f4cf\\activation-sphere.dds";
        constexpr auto DEBUG_SPHERE_TEXTURE = "f4cf\\debug-sphere.dds";

        // The effect shader's base color scale at glow 1: the brightest look the spheres are tuned for (the debug sphere).
        constexpr float MAX_BASE_COLOR_SCALE = 10.0f;

        /**
         * How strongly an activation-sphere preset draws: overall opacity, glow, and the middle-to-rim falloff.
         */
        struct PresetStrength
        {
            std::string_view name;
            float alpha;
            float glow;
            float centerOpacity;
            float rimOpacity;
        };

        constexpr PresetStrength PRESET_STRENGTHS[] = {
            { "full", 0.155f, 0.6f, 0.14f, 0.88f },
            { "medium", 0.145f, 0.5f, 0.045f, 1.0f },
            { "subtle", 0.105f, 0.4f, 0.025f, 0.82f },
            { "low", 0.09f, 0.35f, 0.015f, 0.8f },
        };

        struct PresetColor
        {
            std::string_view name;
            std::array<float, 3> rgb;
        };

        constexpr PresetColor PRESET_COLORS[] = {
            { "white", { 1.0f, 1.0f, 1.0f } },
            { "gray", { 0.7f, 0.7f, 0.7f } },
            { "cyan", { 0.1f, 0.68f, 1.0f } },
            { "green", { 0.401f, 1.0f, 0.105f } },
            { "purple", { 0.537f, 0.12f, 1.0f } },
            { "red", { 1.0f, 0.12f, 0.1f } },
            { "amber", { 1.0f, 0.5f, 0.08f } },
            { "gold", { 1.0f, 0.993f, 0.12f } },
        };

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

        bool isEffectShaderProperty(const RE::NiProperty* property)
        {
            const auto rtti = property ? property->GetRTTI() : nullptr;
            return rtti && std::string_view(rtti->GetName()) == "BSEffectShaderProperty";
        }

        /**
         * Whether no other shape can see this material: kept out of the shared material cache and referenced only by
         * the property holding it — so writing its values restyles that one shape.
         */
        bool isPrivateMaterial(const RE::BSShaderMaterial* material)
        {
            return material->hashKey == RE::BSShaderMaterial::UNIQUE_HASH_KEY && material->QRefCount() == 1;
        }

        /**
         * The property's material, made private first when other shapes can see it, so the values written next restyle
         * only this shape. A cloned mesh normally already owns its material; one acquired from the engine's shared
         * cache is swapped for a unique copy through the engine's own SetMaterial(unique), which copies the current
         * material before releasing it.
         */
        RE::BSEffectShaderMaterial* getPrivateMaterial(RE::BSShaderProperty* property, const RE::NiAVObject* geometry)
        {
            if (!property->material) {
                return nullptr;
            }
            if (!isPrivateMaterial(property->material)) {
                property->SetMaterial(property->material, true);
                if (!property->material || !isPrivateMaterial(property->material)) {
                    logger::error("'{}' could not get a material of its own; left as authored", geometry->name.c_str());
                    return nullptr;
                }
                logger::info("'{}' shared its material; restyling a private copy", geometry->name.c_str());
            }
            return static_cast<RE::BSEffectShaderMaterial*>(property->material);
        }

        /**
         * The Data-relative form the engine keeps texture names in ("Textures\..."), from a resolved path that may be
         * rooted at the game folder ("Data\Textures\...").
         */
        std::string toDataRelativePath(const std::string& path)
        {
            constexpr std::string_view dataPrefix = "data\\";
            if (path.size() > dataPrefix.size() && _strnicmp(path.c_str(), dataPrefix.data(), dataPrefix.size()) == 0) {
                return path.substr(dataPrefix.size());
            }
            return path;
        }

        /**
         * Load a texture through the engine's texture cache. The loader's reference belongs to the caller, so it is
         * adopted rather than added to — or every load would keep the texture alive for good.
         */
        RE::NiPointer<RE::NiTexture> loadTexture(const std::string& path)
        {
            RE::NiTexture* loaded = nullptr;
            LoadTextureByPath(path.c_str(), true, loaded, 0, 0, 0);
            RE::NiPointer<RE::NiTexture> texture(loaded);
            if (loaded) {
                loaded->DecRefCount();
            }
            return texture;
        }
    }

    const std::string& SphereStyle::nifOrDefault() const
    {
        return nif.empty() ? getDefaultSphereStyle().nif : nif;
    }

    /**
     * Resolve a preset name: the debug look, or an activation-sphere color + strength pair.
     */
    std::optional<SphereStyle> findSphereStylePreset(const std::string_view name)
    {
        const std::string key = common::normalizeConfigToken(name);
        if (key == "debug") {
            return SphereStyle{
                .nif = SPHERE_NIF,
                .texture = DEBUG_SPHERE_TEXTURE,
                .color = { 0.797f, 1.0f, 0.634f, 0.176f },
                .glow = 1.0f,
            };
        }

        for (const auto& color : PRESET_COLORS) {
            if (!key.starts_with(color.name)) {
                continue;
            }
            for (const auto& strength : PRESET_STRENGTHS) {
                if (std::string_view(key).substr(color.name.size()) == strength.name) {
                    return SphereStyle{
                        .nif = SPHERE_NIF,
                        .texture = ACTIVATION_SPHERE_TEXTURE,
                        .color = { color.rgb[0], color.rgb[1], color.rgb[2], strength.alpha },
                        .glow = strength.glow,
                        .centerOpacity = strength.centerOpacity,
                        .rimOpacity = strength.rimOpacity,
                    };
                }
            }
        }
        return std::nullopt;
    }

    const SphereStyle& getDefaultSphereStyle()
    {
        static const SphereStyle style = *findSphereStylePreset("white-subtle");
        return style;
    }

    const SphereStyle& getDebugSphereStyle()
    {
        static const SphereStyle style = *findSphereStylePreset("debug");
        return style;
    }

    /**
     * Load the style's texture once, then restyle each effect-shaded shape on a material of its own. The texture's
     * name is written next to the texture itself, in the engine's Data-relative form, so the two never disagree should
     * the engine look the texture up again by name. A texture that fails to load leaves the mesh's own texture in place.
     *
     * The values the mesh was authored with are logged before they are overwritten: they must match the .nif, which
     * confirms the material layout on the running game.
     */
    void applySphereStyle(RE::NiAVObject* node, const SphereStyle& style)
    {
        if (!node) {
            return;
        }

        std::string texturePath;
        RE::NiPointer<RE::NiTexture> texture;
        if (!style.texture.empty()) {
            texturePath = resolveTexturePath(style.texture);
            texture = loadTexture(texturePath);
            if (!texture) {
                logger::error("texture '{}' did not load; '{}' keeps the texture its mesh names", texturePath, node->name.c_str());
            }
        }

        int styledShapes = 0;
        forEachGeometry(node, [&](RE::BSGeometry* geometry) {
            const auto property = geometry->GetRuntimeData().properties[1].get();
            if (!isEffectShaderProperty(property)) {
                return;
            }
            const auto material = getPrivateMaterial(static_cast<RE::BSShaderProperty*>(property), geometry);
            if (!material) {
                return;
            }

            logger::info("'{}' authored with texture '{}', color ({:.3f}, {:.3f}, {:.3f}, {:.3f}), base color scale {:.2f}, falloff {:.3f} -> {:.3f}",
                geometry->name.c_str(),
                material->baseTextureName.c_str(),
                material->baseColor.r,
                material->baseColor.g,
                material->baseColor.b,
                material->baseColor.a,
                material->baseColorScale,
                material->falloffStartOpacity,
                material->falloffStopOpacity);

            material->baseColor = RE::NiColorA{ style.color[0], style.color[1], style.color[2], style.color[3] };
            material->baseColorScale = style.glow * MAX_BASE_COLOR_SCALE;
            material->falloffStartOpacity = style.centerOpacity;
            material->falloffStopOpacity = style.rimOpacity;
            if (texture) {
                material->baseTexture = texture;
                material->baseTextureName = RE::BSFixedString(toDataRelativePath(texturePath));
            }
            ++styledShapes;
        });

        if (styledShapes == 0) {
            logger::warn("'{}' has no effect-shaded shape to style; it draws as authored", node->name.c_str());
        }
    }
}
