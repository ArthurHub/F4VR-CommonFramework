#include "ImGuiCursorLogic.h"

namespace f4cf::imgui::internal
{
    /**
     * The canvas under the cursor at the given screen pixel: of canvases that overlap there, the last one, which is drawn
     * over the others. A rectangle holds its left and top edges and not its right and bottom ones, so two canvases side by
     * side never share a pixel.
     */
    std::optional<CursorHit> canvasUnderCursor(const std::span<const ScreenRect> canvases, const float x, const float y)
    {
        for (std::size_t i = canvases.size(); i-- > 0;) {
            const auto& rect = canvases[i];
            if (x >= rect.left && y >= rect.top && x < rect.left + rect.width && y < rect.top + rect.height) {
                return CursorHit{ .index = i, .x = x - rect.left, .y = y - rect.top };
            }
        }
        return std::nullopt;
    }
}
