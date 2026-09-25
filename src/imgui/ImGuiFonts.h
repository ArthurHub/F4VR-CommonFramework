#pragma once

struct ImFont;

namespace f4cf::imgui::internal
{
    /**
     * Load the canvas font into the current ImGui context's atlas: the framework's text font, from
     * the same bytes the primitive renderer draws with - the mod's own file at
     * render::CUSTOM_TEXT_FONT_PATH when it ships a usable one, the embedded Roboto Medium
     * otherwise - so a UIImGuiPanel and a UITextPanel always share a typeface.
     *
     * ImGui's embedded bitmap font is the last resort, logged as a warning, for when ImGui will not
     * take the font at all.
     *
     * @param sizePixels the size text is laid out at - see setFontSizePixels. Clamped to the same
     *        range, so no caller can ask for a size the setter would have refused.
     * @param rasterScale how much larger than that the glyphs are rasterized. The caller sets
     *        io.FontGlobalScale to its inverse, so layout still sees sizePixels.
     * @return the loaded font, or nullptr if even the bitmap fallback failed.
     */
    ImFont* loadCanvasFont(float sizePixels, float rasterScale);
}
