#include "Text.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

#include "Controls.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // The line under an underlined text, in its font size: its top is below the baseline by the canvas font's underline
    // position, and it is as thick as its underline, which are 150 and 100 of the 2400 units ImGui sizes the font by.
    constexpr float UNDERLINE_OFFSET = 0.0625f;
    constexpr float UNDERLINE_THICKNESS = 0.042f;

    // the line under a range of a text, at the bottom of the text
    constexpr float RANGE_UNDERLINE = 2.0f;

    // the room after a mark in a text, as a space
    float markSpace()
    {
        return get().theme().space2;
    }

    /**
     * How a piece of text looks: its font size, its color, and its face.
     */
    struct Look
    {
        float fontSize = 0.0f;
        ImU32 color = 0;
        ImFont* font = nullptr;
        bool underline = false;
    };

    /**
     * A word of a line of text with the spaces after it, which a line is wrapped between.
     */
    struct Word
    {
        std::string_view text;

        // with its spaces, and without them
        float width = 0.0f;
        float inkWidth = 0.0f;
        Look look;

        // the mark it is, with no text
        Mark mark = Mark::None;
    };

    float widthOf(const std::string_view text, const Look& look)
    {
        return text.empty() ? 0.0f : look.font->CalcTextSizeA(look.fontSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
    }

    /**
     * Adds the words of a text in one look to those of its line.
     */
    void addWords(const std::string_view text, const Look& look, std::vector<Word>& words)
    {
        for (std::size_t start = 0; start < text.size();) {
            const auto inkEnd = (std::min)(text.find(' ', start), text.size());
            const auto end = (std::min)(text.find_first_not_of(' ', inkEnd), text.size());
            words.push_back({ .text = text.substr(start, end - start),
                .width = widthOf(text.substr(start, end - start), look),
                .inkWidth = widthOf(text.substr(start, inkEnd - start), look),
                .look = look });
            start = end;
        }
    }

    /**
     * The words of pieces of text that are one text. A piece is in its own font and size, or else in the current
     * ones. A mark is a word of its own, in the current size, with the room of a space after it.
     */
    std::vector<Word> wordsOf(const std::span<const TextPiece> pieces)
    {
        std::vector<Word> words;
        for (const auto& piece : pieces) {
            const Look look{ .fontSize = piece.size > 0.0f ? piece.size : ImGui::GetFontSize(),
                .color = piece.color,
                .font = piece.font ? piece.font : ImGui::GetFont(),
                .underline = piece.underline };
            if (piece.mark != Mark::None) {
                words.push_back({ .text = {}, .width = MARK_WIDTH + markSpace(), .inkWidth = MARK_WIDTH, .look = look, .mark = piece.mark });
            } else {
                addWords(piece.text, look, words);
            }
        }
        return words;
    }

    /**
     * Where the lines of words start when they are wrapped in a width: the place of the first word of each. A word that
     * is wider than the width is a line by itself.
     */
    std::vector<std::size_t> wrapWords(const std::span<const Word> words, const float width)
    {
        std::vector<std::size_t> starts;
        float lineWidth = 0.0f;
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (i == 0 || lineWidth + words[i].inkWidth > width) {
                starts.push_back(i);
                lineWidth = 0.0f;
            }
            lineWidth += words[i].width;
        }
        return starts;
    }

    /**
     * Draws words on one line, set in its room by the line's align. Words of a smaller size stand on the bottom of the
     * line. An underlined word has its line under it and under the spaces after it, but not under the spaces that end
     * the line. A mark is in the middle of the line's height. Returns the height of the line.
     */
    float drawWords(const std::span<const Word> words, const TextAlign align, const ImVec2& pos, const float width)
    {
        float height = 0.0f;
        float lineWidth = 0.0f;
        for (const auto& word : words) {
            height = (std::max)(height, word.look.fontSize);
            lineWidth += &word == &words.back() ? word.inkWidth : word.width;
        }
        if (!ImGui::IsRectVisible(pos, ImVec2(pos.x + width, pos.y + height))) {
            return height;
        }

        const float room = (std::max)(width - lineWidth, 0.0f);
        float x = pos.x;
        if (align != TextAlign::Left) {
            x += align == TextAlign::Center ? room * 0.5f : room;
        }
        auto* drawList = ImGui::GetWindowDrawList();
        for (const auto& word : words) {
            const auto& look = word.look;
            if (word.mark != Mark::None) {
                const ImVec2 center(x + MARK_WIDTH * 0.5f, pos.y + height * 0.5f);
                drawMark(word.mark, center);
                x += word.width;
                continue;
            }
            const ImVec2 at(std::floor(x), std::floor(pos.y + height - look.fontSize));
            drawList->AddText(look.font, look.fontSize, at, look.color, word.text.data(), word.text.data() + word.text.size());
            if (look.underline) {
                // to where the next word starts before it is set on a whole pixel, so the lines of two words meet
                const float end = x + (&word == &words.back() ? word.inkWidth : word.width);
                const float top = at.y + look.font->GetFontBaked(look.fontSize)->Ascent + look.fontSize * UNDERLINE_OFFSET;
                drawList->AddRectFilled(ImVec2(at.x, top), ImVec2(end, top + (std::max)(look.fontSize * UNDERLINE_THICKNESS, 1.0f)), look.color);
            }
            x += word.width;
        }
        return height;
    }

    /**
     * Draws the words from the first on one line, as many as leave room for an ellipsis in the width, and the first in
     * any case, and the ellipsis after them in the size and color of the last.
     */
    void drawCutLine(const std::span<const Word> words, const ImVec2& pos, const float width)
    {
        const auto& first = words.front().look;
        const float room = width - first.font->GetFontBaked(first.fontSize)->GetCharAdvance(first.font->EllipsisChar);
        std::size_t last = 0;
        float lineWidth = words.front().width;
        while (last + 1 < words.size() && lineWidth + words[last + 1].inkWidth <= room) {
            lineWidth += words[++last].width;
        }
        const auto drawn = words.first(last + 1);
        const float height = drawWords(drawn, TextAlign::Left, pos, width);

        const auto& end = words[last];
        const auto& look = end.look;
        const ImVec2 at(std::floor(pos.x + lineWidth - end.width + end.inkWidth), std::floor(pos.y + height - look.fontSize));
        look.font->RenderChar(ImGui::GetWindowDrawList(), look.fontSize, at, look.color, look.font->EllipsisChar);
    }
}

namespace f4cf::imgui::controls
{
    /**
     * The count of the characters of a UTF-8 text: its bytes that start one.
     */
    int utf8Length(const std::string_view text)
    {
        return static_cast<int>(std::ranges::count_if(text, [](const char c) {
            return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
        }));
    }

    /**
     * The width of a line of text in the current font size.
     */
    float textWidth(const std::string_view text)
    {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    }

    /**
     * One line of text in the current font size, centered in a height. It ends in an ellipsis where it does not fit
     * before the right edge.
     */
    void drawText(const float left, const float right, const float top, const float height, const ImU32 color, const std::string_view text)
    {
        if (text.empty()) {
            return;
        }
        const float y = top + (height - ImGui::GetFontSize()) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(left, y), ImVec2(right, y + ImGui::GetFontSize()), right, text.data(), text.data() + text.size(), nullptr);
        ImGui::PopStyleColor();
    }

    /**
     * One line of text in a box, in from its sides by a padding. The align sets it in that room: 0 at the left, 0.5 in
     * the middle and 1 at the right. A text that is wider than the room ends in an ellipsis.
     */
    void drawTextIn(const ImRect& box, const std::string_view text, const ImU32 color, const float align, const float padding)
    {
        const float room = box.GetWidth() - padding * 2.0f;
        // on a whole pixel to the left, so a text that just fits its room gets no ellipsis
        const float left = std::floor(box.Min.x + padding + (std::max)(room - textWidth(text), 0.0f) * align);
        drawText(left, box.Max.x - padding, box.Min.y, box.GetHeight(), color, text);
    }

    /**
     * One line of text, as `drawText` draws it, with a text after it in another color, as a file's extension, which
     * keeps its room: the text ends in an ellipsis before it where both do not fit.
     */
    void drawTextWithSuffix(const float left, const float right, const float top, const float height, const ImU32 color, const std::string_view text, const ImU32 suffixColor,
        const std::string_view suffix)
    {
        if (suffix.empty()) {
            drawText(left, right, top, height, color, text);
            return;
        }
        const float textRight = (std::max)(left, right - textWidth(suffix));
        drawText(left, textRight, top, height, color, text);
        drawText((std::min)(left + textWidth(text), textRight), right, top, height, suffixColor, suffix);
    }

    /**
     * A line under ranges of the bytes of a line of text that `drawText` drew from `left`: 2 px at the bottom of the
     * text, in its color. A text that ends in an ellipsis has no line past the letters before it.
     */
    void drawUnderlines(const float left, const float right, const float top, const float height, const ImU32 color, const std::string_view text,
        const std::span<const TextRange> ranges)
    {
        if (ranges.empty()) {
            return;
        }
        const float bottom = top + (height + ImGui::GetFontSize()) * 0.5f;
        const float shown = textWidth(text) <= right - left ? right : right - textWidth("\xE2\x80\xA6");
        auto* drawList = ImGui::GetWindowDrawList();
        for (const auto& range : ranges) {
            const float start = left + textWidth(text.substr(0, range.begin));
            const float end = (std::min)(left + textWidth(text.substr(0, range.end)), shown);
            if (end > start) {
                drawList->AddRectFilled(ImVec2(start, bottom - RANGE_UNDERLINE), ImVec2(end, bottom), ImGui::GetColorU32(color));
            }
        }
    }

    /**
     * One line of pieces, each in its look, centered in a height, as `drawText` draws a line, and clipped to its room.
     * It ends in an ellipsis where it does not fit before the right edge. Returns the width it takes, which is not more
     * than its room.
     */
    float drawTextLine(const float left, const float right, const float top, const float height, const std::span<const TextPiece> pieces)
    {
        const auto words = wordsOf(pieces);
        if (words.empty()) {
            return 0.0f;
        }

        float lineHeight = 0.0f;
        float lineWidth = 0.0f;
        for (const auto& word : words) {
            lineHeight = (std::max)(lineHeight, word.look.fontSize);
            lineWidth += &word == &words.back() ? word.inkWidth : word.width;
        }
        const ImVec2 pos(left, top + (height - lineHeight) * 0.5f);
        ImGui::PushClipRect(ImVec2(left, top), ImVec2(right, top + height), true);
        if (lineWidth <= right - left) {
            drawWords(words, TextAlign::Left, pos, right - left);
        } else {
            drawCutLine(words, pos, right - left);
        }
        ImGui::PopClipRect();
        return (std::min)(lineWidth, right - left);
    }

    /**
     * Pieces as one paragraph in a width, wrapped between its words where it is wider, each line set by the align. A
     * paragraph with no words is as high as a line of the current font size. Returns its height.
     */
    float drawWrappedText(const std::span<const TextPiece> pieces, const TextAlign align, const ImVec2& pos, const float width)
    {
        const auto words = wordsOf(pieces);
        if (words.empty()) {
            return ImGui::GetFontSize();
        }
        const auto starts = wrapWords(words, width);
        float y = pos.y;
        for (std::size_t i = 0; i < starts.size(); ++i) {
            const auto end = i + 1 < starts.size() ? starts[i + 1] : words.size();
            y += drawWords(std::span<const Word>(words).subspan(starts[i], end - starts[i]), align, ImVec2(pos.x, y), width);
        }
        return y - pos.y;
    }

    /**
     * The number of lines that pieces take as one text, wrapped in a width.
     */
    int countTextLines(const std::span<const TextPiece> pieces, const float width)
    {
        return static_cast<int>(wrapWords(wordsOf(pieces), width).size());
    }

    /**
     * Pieces as one text, wrapped in a width between the words, a line of the current font size apart. A text that
     * needs more lines than it is given ends its last line in an ellipsis.
     */
    void drawTextPieces(const std::span<const TextPiece> pieces, const ImVec2& pos, const float width, const int maxLines)
    {
        const auto words = wordsOf(pieces);
        const auto starts = wrapWords(words, width);
        const auto lineCount = (std::min)(starts.size(), static_cast<std::size_t>((std::max)(maxLines, 0)));
        for (std::size_t line = 0; line < lineCount; ++line) {
            const ImVec2 at(pos.x, pos.y + static_cast<float>(line) * ImGui::GetFontSize());
            const auto first = starts[line];
            if (line + 1 < lineCount || lineCount == starts.size()) {
                const auto end = line + 1 < starts.size() ? starts[line + 1] : words.size();
                drawWords(std::span<const Word>(words).subspan(first, end - first), TextAlign::Left, at, width);
            } else {
                drawCutLine(std::span<const Word>(words).subspan(first), at, width);
            }
        }
    }
}
