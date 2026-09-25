#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace f4cf::f4vr
{
    /**
     * How a sphere visual (an activation sphere's zone, a vrui debug marker) looks: the mesh cloned for it, the
     * values set at runtime on that mesh's effect shader, and its size. One neutral mesh serves every preset, and neither
     * the mesh nor the texture path inside it names a mod, so any mod ships the same files. Start from a named preset
     * (findSphereStylePreset) and override single values on top.
     *
     * The mesh and texture paths resolve like any other framework resource: as given, then under Data\Meshes\ /
     * Data\Textures\, then under this mod's own folder there — so "f4cf\activation-sphere.nif" finds
     * Data\Meshes\<ModName>\f4cf\activation-sphere.nif.
     */
    struct SphereStyle
    {
        // The .nif cloned for the visual. Empty = the framework's activation-sphere mesh.
        std::string nif;
        // The base texture set on the mesh's effect shader. Empty = keep the texture the mesh itself names.
        std::string texture;
        // Base color as r, g, b, a (0..1 each); the texture is multiplied by it, and its alpha is the overall opacity.
        std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f };
        // Emissive brightness, 0..1: 1 is as bright as the debug sphere, the activation presets sit around 0.4-0.5.
        // Scaled onto the shader's base color scale, whose useful range the caller need not know.
        float glow = 1.0f;
        // Opacity where the surface faces the viewer (the middle of the sphere) and at its silhouette: the falloff
        // that makes an activation sphere read as a glowing rim. Only meshes authored with falloff use them; equal
        // values draw the surface evenly.
        float centerOpacity = 1.0f;
        float rimOpacity = 1.0f;
        // Drawn size relative to the zone the sphere marks: < 1 draws it inside the real (unscaled) zone, as a "hand is
        // inside" hint. Sizes the placed node rather than the shader, so it is honored by whoever places the mesh (the
        // activation sphere) and ignored by applySphereStyle; presets leave it at 1.
        float scale = 1.0f;

        /**
         * The mesh to clone: `nif`, or the framework's activation-sphere mesh when it is empty.
         */
        const std::string& nifOrDefault() const;

        bool operator==(const SphereStyle&) const = default;
    };

    /**
     * The named style presets, matched case-insensitively and ignoring separators:
     * - "<color>-<strength>": a glowing rim, color white / gray / cyan / green / purple / red / amber / gold, strength
     *   full / medium / subtle / low (e.g. "cyan-subtle").
     * - "debug": an evenly lit grid.
     * Returns std::nullopt for an unknown name.
     */
    std::optional<SphereStyle> findSphereStylePreset(std::string_view name);

    /**
     * The style an activation sphere uses unless configured otherwise: the "white-subtle" preset.
     */
    const SphereStyle& getDefaultSphereStyle();

    /**
     * The style of the vrui debug markers: the "debug" preset.
     */
    const SphereStyle& getDebugSphereStyle();

    /**
     * Restyle every effect-shaded shape under `node` on a material of its own (a clone already owns one; a material
     * shared with other shapes is swapped for a private copy first), writing the style's color, glow, and falloff
     * opacities, and its texture when it names one. Meant for a freshly
     * cloned mesh, before it is first attached: the renderer has not seen the shape yet, so the swapped material and
     * texture need no render-pass rebuild. To change a style, clone the mesh again. Main thread only.
     */
    void applySphereStyle(RE::NiAVObject* node, const SphereStyle& style);
}
