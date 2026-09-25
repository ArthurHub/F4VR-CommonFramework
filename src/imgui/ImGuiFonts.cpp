#include "ImGuiFonts.h"

#include <algorithm>

#include <imgui.h>

#include "../render/TextFont.h"
#include "ImGuiSettings.h"

namespace f4cf::imgui::internal
{
    ImFont* loadCanvasFont(const float sizePixels, const float rasterScale)
    {
        auto& io = ImGui::GetIO();
        const float size = std::clamp(sizePixels, MIN_FONT_SIZE_PIXELS, MAX_FONT_SIZE_PIXELS);
        const float rasterSize = size * (std::max)(1.0f, rasterScale);

        const auto bytes = render::internal::textFontBytes();
        ImFontConfig config;
        // the bytes live for the whole process, so the atlas must not free them - and with ownership
        // off, ImGui only reads through the mutable pointer it asks for
        config.FontDataOwnedByAtlas = false;
        if (ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), rasterSize, &config)) {
            logger::info("Canvas font: {} at {:.0f}px, rasterized at {:.0f}px", render::internal::textFontSource(), size, rasterSize);
            return font;
        }

        logger::warn("Canvas font: ImGui would not take {}; falling back to its embedded bitmap font - canvases will look pixelated", render::internal::textFontSource());
        ImFontConfig fallback;
        fallback.SizePixels = rasterSize;
        return io.Fonts->AddFontDefault(&fallback);
    }
}
