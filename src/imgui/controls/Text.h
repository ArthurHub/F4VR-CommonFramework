#pragma once

#include <span>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "Marks.h"
#include "TextRange.h"

// Text of the controls: one line cut with an ellipsis, underlined ranges, and pieces of text wrapped as one text.
namespace f4cf::imgui::controls
{
    /**
     * A piece of a text in one look, or a mark with no text. A piece with no font or no size is in the current one.
     */
    struct TextPiece
    {
        std::string_view text;
        ImU32 color = 0;
        Mark mark = Mark::None;
        ImFont* font = nullptr;
        float size = 0.0f;
        bool underline = false;
    };

    int utf8Length(std::string_view text);
    float textWidth(std::string_view text);
    void drawText(float left, float right, float top, float height, ImU32 color, std::string_view text);
    void drawTextIn(const ImRect& box, std::string_view text, ImU32 color, float align, float padding = 0.0f);
    void drawTextWithSuffix(float left, float right, float top, float height, ImU32 color, std::string_view text, ImU32 suffixColor, std::string_view suffix);
    void drawUnderlines(float left, float right, float top, float height, ImU32 color, std::string_view text, std::span<const TextRange> ranges);
    float drawTextLine(float left, float right, float top, float height, std::span<const TextPiece> pieces);
    float drawWrappedText(std::span<const TextPiece> pieces, TextAlign align, const ImVec2& pos, float width);
    int countTextLines(std::span<const TextPiece> pieces, float width);
    void drawTextPieces(std::span<const TextPiece> pieces, const ImVec2& pos, float width, int maxLines);
}
