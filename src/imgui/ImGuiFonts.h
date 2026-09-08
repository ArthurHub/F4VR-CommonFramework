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
     * @param sizePixels rasterization size, deliberately far above the nominal on-screen size - the
     *        legibility trick in VR is to rasterize large and scale the QUAD down, never to
     *        re-rasterize. Clamped to a sane range.
     * @return the loaded font, or nullptr if even the embedded fallback failed.
     */
    ImFont* loadPanelFont(const char* modName, float sizePixels);
}
