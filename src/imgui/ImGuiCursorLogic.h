#pragma once

#include <cstddef>
#include <optional>
#include <span>

// The game-free part of the cursor on flat, kept here so it is tested without the game.
namespace f4cf::imgui::internal
{
    /**
     * A canvas's rectangle on the screen, in its pixels, from the screen's top left.
     */
    struct ScreenRect
    {
        float left = 0.0f;
        float top = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    /**
     * The canvas the cursor is on, by its index among the given ones, and where on it, in its pixels from its top left.
     */
    struct CursorHit
    {
        std::size_t index = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    std::optional<CursorHit> canvasUnderCursor(std::span<const ScreenRect> canvases, float x, float y);
}
