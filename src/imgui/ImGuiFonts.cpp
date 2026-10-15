#include "ImGuiFonts.h"

#include <algorithm>

#include <imgui.h>

#include "../render/TextFont.h"
#include "ImGuiSettings.h"

namespace f4cf::imgui::internal
{
    // Roboto-Black.ttf and Roboto-MediumItalic.ttf, compiled in by cmake/embed-binary.cmake (see CMakeLists.txt)
    extern const std::uint8_t BOLD_FONT_TTF[];
    extern const std::size_t BOLD_FONT_TTF_SIZE;
    extern const std::uint8_t ITALIC_FONT_TTF[];
    extern const std::size_t ITALIC_FONT_TTF_SIZE;
}

namespace
{
    using f4cf::imgui::FontStyle;
    using f4cf::render::internal::FontFile;

    /**
     * The file of a face of the canvas font. Each is resolved once and kept for the life of the process,
     * as the atlas reads its bytes in place.
     */
    const FontFile& faceFile(const FontStyle style)
    {
        using namespace f4cf::imgui::internal;
        using f4cf::render::internal::resolveFontFile;
        static const FontFile bold = resolveFontFile(f4cf::render::CUSTOM_BOLD_FONT_PATH, std::span(BOLD_FONT_TTF, BOLD_FONT_TTF_SIZE), "embedded Roboto Black");
        static const FontFile italic = resolveFontFile(f4cf::render::CUSTOM_ITALIC_FONT_PATH, std::span(ITALIC_FONT_TTF, ITALIC_FONT_TTF_SIZE), "embedded Roboto Medium Italic");
        if (style == FontStyle::Bold) {
            return bold;
        }
        return style == FontStyle::Italic ? italic : f4cf::render::internal::textFontFile();
    }

    /**
     * Adds a face to the current ImGui context's atlas, at its default size. ImGui's embedded font stands
     * in for one ImGui will not take, so every face keeps its place in the atlas.
     */
    ImFont* addFace(const FontStyle style, const float sizePixels)
    {
        auto& io = ImGui::GetIO();
        const auto& file = faceFile(style);
        const auto bytes = file.bytes();
        ImFontConfig config;
        // the bytes live for the whole process, so the atlas must not free them - and with ownership
        // off, ImGui only reads through the mutable pointer it asks for
        config.FontDataOwnedByAtlas = false;
        if (ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), sizePixels, &config)) {
            return font;
        }

        logger::warn("Canvas font: ImGui would not take {}; falling back to its embedded font", file.source);
        ImFontConfig fallback;
        fallback.SizePixels = sizePixels;
        return io.Fonts->AddFontDefaultVector(&fallback);
    }
}

namespace f4cf::imgui
{
    /**
     * A face of the canvas font in the current ImGui context, for a run of text in it: ImGui::PushFont,
     * or ImDrawList::AddText with the font. Null before the canvas font is loaded.
     */
    ImFont* canvasFont(const FontStyle style)
    {
        const auto& fonts = ImGui::GetIO().Fonts->Fonts;
        const int index = static_cast<int>(style);
        return index < fonts.Size ? fonts[index] : nullptr;
    }
}

namespace f4cf::imgui::internal
{
    /**
     * Loads the canvas font into the current ImGui context's atlas: its regular face from the bytes the
     * primitive renderer draws with, so a UIImGuiPanel and a UITextPanel share a typeface, then its bold
     * and italic, in the order of FontStyle. sizePixels, clamped as setFontSizePixels clamps it, is the
     * size text is drawn at by default; ImGui rasterizes each size it draws as it is first drawn.
     * Returns the regular face, which is ImGui's default font.
     */
    ImFont* loadCanvasFont(const float sizePixels)
    {
        const float size = std::clamp(sizePixels, MIN_FONT_SIZE_PIXELS, MAX_FONT_SIZE_PIXELS);
        ImFont* regular = addFace(FontStyle::Regular, size);
        addFace(FontStyle::Bold, size);
        addFace(FontStyle::Italic, size);
        logger::info("Canvas font: {}, {} and {} at {:.0f}px", faceFile(FontStyle::Regular).source, faceFile(FontStyle::Bold).source, faceFile(FontStyle::Italic).source, size);
        return regular;
    }
}
