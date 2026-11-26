#include "Rows.h"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include "Controls.h"
#include "Frames.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // the room a row keeps on its right, past the scroll bar's
    float rowRightGap()
    {
        return get().theme().space2;
    }

    // the gap between a mark and its count, and between a heading's arrow or mark and its text: half the gap between
    // two counts
    float markGap()
    {
        return get().theme().space2;
    }

    // the room between a heading's text or its counts and its rule, and between the counts and the right edge
    float ruleGap()
    {
        return get().theme().space3;
    }

    // the room a heading's rule keeps at the least
    constexpr float RULE_MIN_WIDTH = 16.0f;

    // the room above and below a section heading's text
    float headingPadding()
    {
        return get().theme().space2;
    }

    /**
     * The width of a count's mark: the dot's, or a mark's.
     */
    float countMarkWidth(const CountMark mark)
    {
        return mark == CountMark::Dot ? DOT_WIDTH : MARK_WIDTH;
    }
}

namespace f4cf::imgui::controls
{
    /**
     * Where a heading's text starts in its row, as a row's text does in its frame, so a heading lines up with the rows:
     * the theme's space3.
     */
    float headingTextInset()
    {
        return get().theme().space3;
    }

    /**
     * The width of a row at the cursor, the same whether or not the window's scroll bar shows. A row of a section in a
     * section is set in, and ends where the other rows do.
     */
    float rowWidth()
    {
        const float indent = ImGui::GetCursorPosX() - ImGui::GetCursorStartPos().x;
        return ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize - rowRightGap() - indent;
    }

    /**
     * Adds a row at the cursor, as wide as the rows and as high as a control. A row that is pressed has the look of a
     * row under the ray, and a stronger one while the trigger is held; any other is only its room. Its content is the
     * caller's, drawn over it while it is shown.
     */
    Row addRow(const bool pressable)
    {
        const ImVec2 size(rowWidth(), get().theme().controlHeight);
        Row row;
        if (pressable) {
            row.pressed = ImGui::InvisibleButton("row", size);
            row.started = ImGui::IsItemActivated();
            row.held = ImGui::IsItemActive();
        } else {
            ImGui::Dummy(size);
        }
        row.shown = ImGui::IsItemVisible();
        if (row.shown) {
            row.pointed = ImGui::IsItemHovered();
            if (pressable) {
                drawRowState();
            }
        }
        return row;
    }

    /**
     * Adds the row of a heading that folds at the cursor, by its id: a pressed row, whose press folds or opens it. The
     * caller keeps whether it is folded, and draws the heading in it with `drawFoldingHeading`.
     */
    Row addFoldingRow(const char* id, bool& folded)
    {
        ImGui::PushID(id);
        const auto row = addRow(true);
        ImGui::PopID();
        if (row.pressed) {
            folded = !folded;
        }
        return row;
    }

    /**
     * Counts that each have their mark before them, ending at a right edge in the current font, in the middle of a
     * control's height from a top. Returns their left edge.
     */
    float drawMarkedCounts(const float right, const float top, const std::span<const MarkedCount> counts)
    {
        float width = 0.0f;
        for (const auto& count : counts) {
            width += (width > 0.0f ? markGap() * 2.0f : 0.0f) + countMarkWidth(count.mark) + markGap() + textWidth(count.text);
        }
        const float left = std::floor(right - width);
        const float middle = top + get().theme().controlHeight * 0.5f;
        float x = left;
        for (const auto& count : counts) {
            const ImVec2 center(x + countMarkWidth(count.mark) * 0.5f, middle);
            switch (count.mark) {
            case CountMark::Error:
                drawErrorMark(center);
                break;
            case CountMark::Warning:
                drawWarningMark(center);
                break;
            case CountMark::Dot:
                drawDot(center, get().colors().warning);
                break;
            }
            x += countMarkWidth(count.mark) + markGap();
            drawText(x, right, top, get().theme().controlHeight, count.color, count.text);
            x += textWidth(count.text) + markGap() * 2.0f;
        }
        return left;
    }

    /**
     * A heading that folds, in a row at a place and in a width: an arrow that points right while it is folded and down
     * while it is open, its text, and a rule from it to the right edge. The arrow starts where a heading's text does.
     * Its text and arrow are muted, also under the ray; with the error or the warning mark before its text, they are in
     * that mark's color. The counts end the rule, in the detail size, and the ranges `found` of the text are underlined.
     */
    void drawFoldingHeading(const std::string_view text, const ImVec2& pos, const float width, const bool folded, const std::span<const MarkedCount> counts, const Mark mark,
        const std::span<const TextRange> found)
    {
        const auto& theme = get().theme();
        const auto& colors = get().colors();
        auto color = colors.muted;
        if (mark == Mark::Error) {
            color = colors.error;
        } else if (mark == Mark::Warning) {
            color = colors.warning;
        }
        const float arrowLeft = pos.x + headingTextInset();
        drawFoldArrow(ImVec2(arrowLeft, pos.y + (theme.controlHeight - FOLD_ARROW_SIZE) * 0.5f), !folded, color);
        float ruleRight = pos.x + width;
        if (!counts.empty()) {
            ImGui::PushFont(nullptr, theme.detailFontSize);
            ruleRight = drawMarkedCounts(ruleRight - ruleGap(), pos.y, counts) - markGap();
            ImGui::PopFont();
        }
        float textLeft = arrowLeft + FOLD_ARROW_SIZE + markGap();
        if (mark != Mark::None) {
            drawMark(mark, ImVec2(textLeft + MARK_WIDTH * 0.5f, pos.y + theme.controlHeight * 0.5f));
            textLeft += MARK_WIDTH + markGap();
        }
        const float textRight = (std::min)(textLeft + textWidth(text), ruleRight - RULE_MIN_WIDTH - ruleGap());
        drawText(textLeft, textRight, pos.y, theme.controlHeight, color, text);
        drawUnderlines(textLeft, textRight, pos.y, theme.controlHeight, color, text, found);
        const float ruleLeft = std::ceil(textRight) + ruleGap();
        if (ruleLeft < ruleRight) {
            const float ruleTop = std::floor(pos.y + (theme.controlHeight - theme.frameBorderSize) * 0.5f);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ruleLeft, ruleTop), ImVec2(ruleRight, ruleTop + theme.frameBorderSize), ImGui::GetColorU32(colors.rule));
        }
    }

    /**
     * The heading of a section of rows at the cursor: its muted text, set in as a row's text is in its frame, and a rule
     * from it to a right edge, where the rows end. ImGui draws the rule to the right of the window's work area, past the
     * rows into the room of the scroll bar, so that edge is the heading's while it is drawn. The ranges `found` of the
     * text are underlined. It is as high as `height`, its text in the middle, or with no height as its text with
     * `headingPadding()` above and below.
     */
    void sectionHeading(const std::string_view text, const float right, const std::span<const TextRange> found, const float height)
    {
        const auto& theme = get().theme();
        const auto& colors = get().colors();
        auto* window = ImGui::GetCurrentWindow();
        const float workRight = window->WorkRect.Max.x;
        window->WorkRect.Max.x = right;
        // the heading's own look, whatever the panel's ImGui style
        const float padding = height > 0.0f ? (std::max)((height - ImGui::GetFontSize()) * 0.5f, 0.0f) : headingPadding();
        ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(headingTextInset(), padding));
        ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, theme.frameBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Separator, colors.rule);
        ImGui::PushStyleColor(ImGuiCol_Text, colors.muted);
        // by its end: ImGui would cut a text at a "##"
        ImGui::SeparatorTextEx(0, text.data(), text.data() + text.size(), 0.0f);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        window->WorkRect.Max.x = workRight;
        const auto min = ImGui::GetItemRectMin();
        const float textLeft = min.x + headingTextInset();
        drawUnderlines(textLeft, textLeft + textWidth(text), min.y, ImGui::GetItemRectSize().y, colors.muted, text, found);
    }

    /**
     * Rows at the cursor of which only those the window shows are drawn, for a list too long to add every row of, as
     * every item of the game: ImGui's list clipper over rows of a control's height with the spacing between them, so
     * the window scrolls as for all of them. `drawRow` adds exactly one row, as `addRow` or a labelled control does.
     * `keep` is a row drawn while it is scrolled out too, in its place, as one whose text is edited.
     */
    void clippedRows(const std::size_t count, const std::function<void(std::size_t index)>& drawRow, const std::optional<std::size_t> keep)
    {
        const float pitch = get().theme().controlHeight + ImGui::GetStyle().ItemSpacing.y;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(count), pitch);
        if (keep && *keep < count) {
            clipper.IncludeItemByIndex(static_cast<int>(*keep));
        }
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                drawRow(static_cast<std::size_t>(index));
            }
        }
        clipper.End();
    }
}
