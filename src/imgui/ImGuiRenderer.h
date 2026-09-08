#pragma once

#include "../render/SubmitHook.h"
#include "ImGuiLayer.h"

namespace f4cf::imgui::internal
{
    /**
     * Render-thread half of the ImGui layer: rasterizes a published ImGui frame into an offscreen
     * atlas texture, then composites each panel's slice of that atlas as a world-space quad on the
     * submitted eye texture, through the engine's own per-eye matrices.
     *
     * ImGui emits per-command SCISSOR RECTANGLES in 2D screen space - that is how scrolling regions,
     * child windows and tables clip - and there is no scissor for an arbitrarily oriented 3D quad.
     * Which is why the draw data is rasterized flat into a texture first and only then placed in the
     * world, rather than transforming ImGui's vertices into world space directly.
     */
    namespace renderer
    {
        /**
         * Create the atlas + quad pipeline and register the draw callback on the shared Submit hook
         * (idempotent). False while the D3D device or the OpenVR compositor is unavailable - safe to
         * retry every frame from the game thread.
         *
         * @param atlasWidth / atlasHeight size of the shared panel texture; all panels pack into it.
         */
        bool ensureInstalled(int atlasWidth, int atlasHeight);

        /**
         * Hand this frame's ImGui pixels and quads to the render thread (game thread). An empty
         * frame goes dormant.
         */
        void publish(RenderFrame&& frame);
    }
}
