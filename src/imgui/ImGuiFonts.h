#pragma once

#include <cstdint>

struct ImFont;

namespace f4cf::imgui
{
    /**
     * The faces of the canvas font, in the order they are loaded into ImGui's font atlas. A mod ships
     * its own at the paths of render/TextFont.h: CUSTOM_TEXT_FONT_PATH and the two beside it.
     */
    enum class FontStyle : std::uint8_t
    {
        Regular,
        Bold,
        Italic,
    };

    ImFont* canvasFont(FontStyle style);
}

namespace f4cf::imgui::internal
{
    ImFont* loadCanvasFont(float sizePixels, float rasterScale);
}
