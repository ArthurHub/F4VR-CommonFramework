#pragma once

#include <memory>
#include <vector>

#include <imgui.h>

#include "ImGuiPanel.h"

namespace f4cf::imgui::internal
{
    // Defined alongside setFontSizePixels() in the panel translation unit.
    float fontSizePixels();

    /**
     * ImDrawData handed across the game/render thread boundary.
     *
     * ImGui's own draw data points into per-frame buffers that the next NewFrame() recycles, so a
     * render thread still reading last frame's lists while the game thread starts the next one is a
     * use-after-free. ImGui ships ImDrawList::CloneOutput() for exactly this: the clone owns its
     * vertex/index/command buffers, costing a few KB of memcpy per frame for panel-sized UI.
     */
    struct ClonedDrawData
    {
        ClonedDrawData() = default;
        ~ClonedDrawData();

        ClonedDrawData(const ClonedDrawData&) = delete;
        ClonedDrawData& operator=(const ClonedDrawData&) = delete;

        /**
         * Deep-copy the context's current draw data into this holder.
         */
        void copyFrom(const ImDrawData& source);

        ImDrawData drawData{};
    };

    /**
     * One panel's composited quad: four world-space corners (resolved game-side) and the sub-rect of
     * the shared atlas that holds its pixels. Every placement mode reduces to this, which is why the
     * renderer needs to know nothing about vrui, nodes or billboards.
     */
    struct PanelQuad
    {
        RE::NiPoint3 topLeft;
        RE::NiPoint3 topRight;
        RE::NiPoint3 bottomRight;
        RE::NiPoint3 bottomLeft;
        float u0 = 0;
        float v0 = 0;
        float u1 = 1;
        float v1 = 1;
        // distance to the viewer, for painter-order sorting (no depth write, so overlap is our job)
        float viewerDistance = 0;
        // whether the world may hide this panel; quads are grouped by it so each group is one draw
        bool occluded = true;
    };

    /**
     * One published frame: the ImGui pixels to rasterize into the atlas, plus where each panel's
     * slice of that atlas goes in the world.
     */
    struct RenderFrame
    {
        // shared, not unique: the render thread takes a reference under the publish mutex and draws
        // outside it, so publishing never blocks on a draw and a frame that misses a publish redraws
        // the last one instead of blinking
        std::shared_ptr<ClonedDrawData> drawData;
        std::vector<PanelQuad> quads;

        bool empty() const
        {
            return !drawData || quads.empty();
        }
    };

    // Panel registration, called from Panel's constructor/destructor (game thread).
    void registerPanel(Panel* panel);
    void unregisterPanel(Panel* panel);

    /**
     * The per-frame pump, run after the mod's onFrameUpdate: build one ImGui frame containing every
     * visible panel, clone it, and publish it with the quads to the render thread. A no-op while no
     * panel is visible.
     */
    void onFrameEnd();
}
