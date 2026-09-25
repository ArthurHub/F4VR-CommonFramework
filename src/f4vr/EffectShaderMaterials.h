#pragma once

#include <functional>

namespace f4cf::f4vr
{
    /**
     * Give every effect-shaded shape under `node` a material of its own, so values written to it later (see
     * forEachEffectShaderMaterial) change that one shape. A cloned mesh normally already owns its material; one acquired
     * from the engine's shared material cache is swapped for a unique copy through the engine's own SetMaterial(unique),
     * which copies the current values before releasing the shared one.
     * Call on a freshly cloned mesh, before it is first attached: the renderer has not seen the shape yet, so the swap
     * needs no render-pass rebuild. Main thread only.
     * Returns how many effect-shaded shapes under `node` own their material afterwards.
     */
    int makeEffectShaderMaterialsPrivate(RE::NiAVObject* node);

    /**
     * Call `fn` with the material and the shape of every effect-shaded shape under `node` that owns its material
     * (makeEffectShaderMaterialsPrivate). A shape still sharing its material is skipped with a warning, since writing to
     * it would change every shape sharing it.
     * The material's values (base color, base color scale, falloff) may be written on a mesh the renderer is drawing:
     * they are read on each draw, the same values the engine's own effect-shader controllers animate, which makes this
     * the way to tint or fade a shown mesh live. Its textures are not: set those before the mesh is first attached.
     * Main thread only.
     * Returns how many shapes `fn` was called for.
     */
    int forEachEffectShaderMaterial(RE::NiAVObject* node, const std::function<void(RE::BSEffectShaderMaterial& material, RE::BSGeometry& geometry)>& fn);
}
