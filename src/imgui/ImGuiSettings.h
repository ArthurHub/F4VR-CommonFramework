#pragma once

namespace f4cf::imgui
{
    /**
     * The range setFontSizePixels clamps to: below 16 glyphs lose their shape, above 128 the font
     * texture grows for nothing.
     */
    inline constexpr float MIN_FONT_SIZE_PIXELS = 16.0f;
    inline constexpr float MAX_FONT_SIZE_PIXELS = 128.0f;

    /**
     * The range setSupersample clamps to: 1 is no supersampling, and at 4 the atlas texture is
     * already 64MB and well past what a headset can resolve.
     */
    inline constexpr float MIN_SUPERSAMPLE = 1.0f;
    inline constexpr float MAX_SUPERSAMPLE = 4.0f;

    /**
     * Size text is laid out at, in layout pixels (default 24px), clamped to MIN_FONT_SIZE_PIXELS..
     * MAX_FONT_SIZE_PIXELS. The glyphs are rasterized at the supersample factor times this, so they
     * keep their detail in the supersampled atlas.
     *
     * Process-wide, not a property of a panel: there is one font, built when the first panel draws,
     * so set this before then - later calls do nothing. A panel that wants text of its own size can
     * push a scale from inside its content callback with ImGui::SetWindowFontScale, which resamples
     * the raster rather than rebuilding it.
     *
     * For a UICanvas this is exactly how big the text is at scale 1, 48px being one vrui unit to the
     * em (CANVAS_PIXELS_PER_UNIT); a container's scale enlarges it with the rest of the canvas.
     */
    void setFontSizePixels(float sizePixels);

    /**
     * How many atlas pixels each layout pixel is rasterized with, per axis (default 1.5), clamped
     * to MIN_SUPERSAMPLE..MAX_SUPERSAMPLE.
     *
     * ImGui lays every panel out at 1x, so the font size, style metrics and pixel sizes in content
     * code mean the same at any factor; only the detail changes. The atlas texture is
     * MAX_PANEL_PIXEL_SIZE times this on each side, so memory grows with the square - about 9MB at
     * 1.5, 16MB at 2 - and past the headset's own resolution a larger factor adds nothing visible.
     *
     * Process-wide, and read when the first panel draws, since the atlas texture and the font raster
     * are built from it then: set it before that - later calls do nothing.
     */
    void setSupersample(float factor);
}

namespace f4cf::imgui::internal
{
    /**
     * The settings above, as clamped by their setters.
     */
    float fontSizePixels();
    float supersample();
}
