#pragma once

struct ImFont;

namespace f4cf::imgui::internal
{
    /**
     * Load the panel font into the current ImGui context's atlas, trying in order:
     *
     *   1. Data/Interface/<modName>/<modName>.ttf   (a mod may ship its own)
     *   2. %WINDIR%\Fonts\arialbd.ttf
     *   3. %WINDIR%\Fonts\segoeuib.ttf
     *   4. ImGui's embedded bitmap font             (logged as a warning - it looks pixelated)
     *
     * Windows system fonts as the fallback mean the framework works with zero shipped assets and
     * raises no font-licensing question. The variants are deliberately BOLD: regular weight goes
     * mushy at VR's effective angular resolution.
     *
     * @param sizePixels the size text is laid out at - see setFontSizePixels. Clamped to the same
     *        range, so no caller can ask for a size the setter would have refused.
     * @param rasterScale how much larger than that the glyphs are rasterized. The caller sets
     *        io.FontGlobalScale to its inverse, so layout still sees sizePixels.
     * @return the loaded font, or nullptr if even the embedded fallback failed.
     */
    ImFont* loadPanelFont(const char* modName, float sizePixels, float rasterScale);
}
