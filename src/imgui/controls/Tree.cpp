#include "Tree.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "Controls.h"
#include "Frames.h"
#include "Marks.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // the room between a row's left edge and its arrow's column
    constexpr float ROW_INSET = 8.0f;

    // the column the arrow of a row is in the middle of, which every row keeps, and the gap from it to the name
    constexpr float ARROW_COLUMN = 16.0f;
    constexpr float ARROW_GAP = 6.0f;

    // the room between a name and what is after it: its dot, its count, its tag
    constexpr float NAME_GAP = 8.0f;

    // the room a row keeps at its right after its name or its count
    constexpr float ROW_RIGHT_INSET = 12.0f;

    // a tag's font size, the room between its text and its outline, and its gap from the row's right edge
    constexpr float TAG_FONT_SIZE = 10.0f;
    constexpr float TAG_PADDING = 1.0f;
    constexpr float TAG_GAP = 8.0f;

    // the room a group row's rule keeps at the least, and the room on each side of it
    constexpr float GROUP_RULE_MIN_WIDTH = 16.0f;
    constexpr float GROUP_RULE_GAP = 12.0f;

    /**
     * How far a row of a level is set in, from the top level at 0: a step for each level, up to the deepest.
     */
    float indentOf(const int level)
    {
        return TREE_INDENT * static_cast<float>((std::min)(level, TREE_MAX_INDENT_LEVEL));
    }

    /**
     * The arrow of a row, in the middle of its column, which starts at `left`: down while it is open, else right.
     */
    void drawArrow(const float left, const float rowTop, const bool open, const ImU32 color)
    {
        const float height = get().style().controlHeight;
        const ImVec2 box(left + (ARROW_COLUMN - FOLD_ARROW_SIZE) * 0.5f, rowTop + (height - FOLD_ARROW_SIZE) * 0.5f);
        drawFoldArrow(box, open, color);
    }

    /**
     * A row's tag, ending at `right` in the middle of the row: its text in the accent in an outline close around it,
     * in the rule's color, or both in the color on the accent on a selected row. Returns its left edge.
     */
    float drawRowTag(const float right, const float rowTop, const std::string_view tag, const bool onFill)
    {
        const auto& style = get().style();
        const auto& colors = get().colors();
        ImGui::PushFont(nullptr, TAG_FONT_SIZE);
        const float inside = TAG_PADDING + style.frameBorderSize;
        const float height = TAG_FONT_SIZE + inside * 2.0f;
        const float width = std::ceil(textWidth(tag) + inside * 2.0f);
        const ImVec2 min(std::floor(right - width), std::floor(rowTop + (style.controlHeight - height) * 0.5f));
        const ImVec2 max(min.x + width, min.y + height);
        drawOutline(min, max, onFill ? colors.onAccent : colors.rule);
        drawText(min.x + inside, max.x, min.y, height, onFill ? colors.onAccent : colors.accent, tag);
        ImGui::PopFont();
        return min.x;
    }

    /**
     * A name in a row from its left up to `right`, with the dot after it: the name ends in an ellipsis before the dot
     * does, so the dot always shows. The dot is in the warning color, or the color on the accent on a selected row.
     * Returns the right edge of what it drew.
     */
    float drawNameWithDot(const float left, const float right, const float top, const ImU32 color, const std::string_view name, const bool dot, const bool onFill)
    {
        const float height = get().style().controlHeight;
        if (!dot) {
            drawText(left, right, top, height, color, name);
            return (std::min)(left + textWidth(name), right);
        }
        const float nameRight = right - NAME_GAP - DOT_WIDTH;
        drawText(left, nameRight, top, height, color, name);
        const float dotLeft = (std::min)(left + textWidth(name), nameRight) + NAME_GAP;
        drawDot(ImVec2(dotLeft + DOT_WIDTH * 0.5f, top + height * 0.5f), onFill ? get().colors().onAccent : get().colors().warning);
        return dotLeft + DOT_WIDTH;
    }

    /**
     * The name of a row after its arrow's column, and its tag at the row's right: the tag is inside the row, so the
     * row's look under the ray has it, and the name ends in an ellipsis before it, so the tags of the rows line up. The
     * count is before the tag in the detail size, and the ranges found are underlined in the name.
     */
    void drawName(const float left, const ImVec2& min, const ImVec2& max, const ImU32 color, const TreeRow& row, const bool onFill)
    {
        const auto& style = get().style();
        float right = max.x - ROW_RIGHT_INSET;
        if (!row.tag.empty()) {
            right = drawRowTag(max.x - TAG_GAP, min.y, row.tag, onFill) - NAME_GAP;
        }
        if (row.count > 0) {
            ImGui::PushFont(nullptr, style.detailFontSize);
            const auto count = std::to_string(row.count);
            const float countLeft = right - textWidth(count);
            drawText(countLeft, right, min.y, style.controlHeight, onFill ? get().colors().onAccent : get().colors().muted, count);
            ImGui::PopFont();
            right = countLeft - NAME_GAP;
        }
        const float nameLeft = left + ARROW_COLUMN + ARROW_GAP;
        drawNameWithDot(nameLeft, right, min.y, color, row.name, row.dot, onFill);
        drawUnderlines(nameLeft, right, min.y, style.controlHeight, color, row.name, row.found);
    }

    /**
     * A disabled row at the cursor: its room, with the look of a row under the ray, so a help shown for it is seen to be
     * its, and none of a press. Its arrow points right, and it is faded. Returns whether the ray is on it.
     */
    bool disabledRow(const TreeRow& row, const float width)
    {
        const float height = get().style().controlHeight;
        const auto min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, height));
        if (!ImGui::IsItemVisible()) {
            return false;
        }
        const bool pointed = ImGui::IsItemHovered();
        drawRowState();

        ImGui::BeginDisabled();
        const float left = min.x + ROW_INSET + indentOf(row.level);
        if (row.arrow != TreeArrow::None) {
            drawArrow(left, min.y, false, get().colors().accent);
        }
        drawName(left, min, ImVec2(min.x + width, min.y + height), get().colors().accent, row, false);
        ImGui::EndDisabled();
        return pointed;
    }
}

namespace f4cf::imgui::controls
{
    /**
     * A row of a tree at the cursor, in the width given, which every row of the tree has: its name is set in by its
     * level, after the column of its arrow, which every row keeps, so the names of a level line up whether or not they
     * have one. The selected row is a solid bar, whose tip is inside the row, and any other has the look of a row under
     * the ray. The row is the last item, so the caller can take its rect.
     */
    TreeRowDrawn treeRow(const TreeRow& row, const float width)
    {
        if (row.disabled) {
            return { .pointed = disabledRow(row, width) };
        }
        const float height = get().style().controlHeight;
        const auto min = ImGui::GetCursorScreenPos();
        const auto max = ImVec2(min.x + width, min.y + height);
        TreeRowDrawn drawn;
        drawn.pressed = ImGui::InvisibleButton("row", ImVec2(width, height));
        drawn.pointed = ImGui::IsItemHovered();

        const float shownTop = ImGui::GetWindowPos().y;
        if (row.selected && row.show && (min.y < shownTop || max.y > shownTop + ImGui::GetWindowHeight())) {
            ImGui::SetScrollHereY();
        }
        if (!ImGui::IsItemVisible()) {
            return drawn;
        }

        if (row.selected) {
            drawSelectedBar(min, max);
        } else {
            drawRowState();
        }
        const auto color = row.selected ? get().colors().onAccent : get().colors().accent;
        const float left = min.x + ROW_INSET + indentOf(row.level);
        if (row.arrow != TreeArrow::None) {
            drawArrow(left, min.y, row.arrow == TreeArrow::Open, color);
        }
        drawName(left, min, max, color, row, row.selected);
        return drawn;
    }

    /**
     * The row of a group of a tree's nodes at the cursor, in the width of every row: its arrow, down while its nodes are
     * the rows under it, its name, a rule from it to the count of its nodes, and the count in the detail size. It is
     * muted, also under the ray, where it has the look of a row. It is never selected: a press opens or closes it, which
     * the caller keeps. With `dot`, the dot follows the name.
     */
    TreeRowDrawn treeGroupRow(const std::string_view name, const std::size_t count, const bool open, const bool dot, const float width)
    {
        const auto& style = get().style();
        const auto& colors = get().colors();
        const auto min = ImGui::GetCursorScreenPos();
        const auto max = ImVec2(min.x + width, min.y + style.controlHeight);
        TreeRowDrawn drawn;
        drawn.pressed = ImGui::InvisibleButton("group", ImVec2(width, style.controlHeight));
        drawn.pointed = ImGui::IsItemHovered();
        if (!ImGui::IsItemVisible()) {
            return drawn;
        }
        const auto color = colors.muted;
        drawRowState();
        const float left = min.x + ROW_INSET;
        drawArrow(left, min.y, open, color);

        ImGui::PushFont(nullptr, style.detailFontSize);
        const auto countText = std::to_string(count);
        const float countRight = max.x - ROW_RIGHT_INSET;
        const float countLeft = countRight - textWidth(countText);
        drawText(countLeft, countRight, min.y, style.controlHeight, color, countText);
        ImGui::PopFont();

        const float ruleRight = countLeft - NAME_GAP;
        const float nameLeft = left + ARROW_COLUMN + ARROW_GAP;
        const float dotWidth = dot ? NAME_GAP + DOT_WIDTH : 0.0f;
        const float nameRight = (std::min)(nameLeft + textWidth(name) + dotWidth, ruleRight - GROUP_RULE_MIN_WIDTH - GROUP_RULE_GAP);
        const float drawnRight = drawNameWithDot(nameLeft, nameRight, min.y, color, name, dot, false);
        const float ruleLeft = std::ceil(drawnRight) + GROUP_RULE_GAP;
        if (ruleLeft < ruleRight) {
            const float ruleTop = std::floor(min.y + (style.controlHeight - style.frameBorderSize) * 0.5f);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ruleLeft, ruleTop), ImVec2(ruleRight, ruleTop + style.frameBorderSize), ImGui::GetColorU32(colors.rule));
        }
        return drawn;
    }
}
