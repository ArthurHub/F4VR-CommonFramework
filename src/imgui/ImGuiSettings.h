#pragma once

namespace f4cf::imgui
{
    /**
     * The range a theme's body font size (Theme::bodyFontSize) is clamped to: below 16 glyphs lose their
     * shape, above 128 the font texture grows for nothing.
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
     * How many atlas pixels each layout pixel is rasterized with, per axis (default 1.5), clamped
     * to MIN_SUPERSAMPLE..MAX_SUPERSAMPLE.
     *
     * ImGui lays every canvas out at 1x, so the font sizes, style metrics and pixel sizes in content
     * code mean the same at any factor; only the detail changes. The atlas texture is
     * MAX_CANVAS_PIXEL_SIZE times this on each side, so memory grows with the square - about 9MB at
     * 1.5, 16MB at 2 - and past the headset's own resolution a larger factor adds nothing visible.
     * The texture of a dedicated canvas is the canvas's own pixels times this on each side.
     *
     * Process-wide, and read when the first canvas draws, since the atlas texture is built from it then
     * and ImGui is set to rasterize at it: set it before that - later calls do nothing.
     */
    void setSupersample(float factor);
}

namespace f4cf::imgui::internal
{
    /**
     * The setting above, as clamped by its setter.
     */
    float supersample();
}
