#include "Frames.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Text.h"

namespace
{
    // the ring around a button, outside its box
    constexpr float BUTTON_RING = 2.0f;

    // a tag's text line, inside its outline
    constexpr float TAG_LINE_HEIGHT = 22.0f;

    // the room between a tag's outline and its text
    constexpr float TAG_PADDING = 8.0f;
}

namespace f4cf::imgui::controls
{
    /**
     * The box of a control that is as high as a row, from its left edge.
     */
    ImRect controlBox(const float left, const float top, const float width)
    {
        return { left, top, left + width, top + get().style().controlHeight };
    }

    /**
     * The top of a popup of a height beside a box it is of, in a window: under the box, or over it where the window has
     * no room under it, or else at the bottom of the window, never above its top. A popup in a page so never covers what
     * is above and below the page.
     */
    float popupTop(const ImRect& box, const float height, const ImRect& window)
    {
        if (box.Max.y + height <= window.Max.y) {
            return box.Max.y;
        }
        if (box.Min.y - height >= window.Min.y) {
            return box.Min.y - height;
        }
        return (std::max)(window.Min.y, window.Max.y - height);
    }

    /**
     * An outline along the inside of a rectangle's edge, as a control and a row under the ray have.
     */
    void drawOutline(const ImVec2& min, const ImVec2& max, const ImU32 color)
    {
        const auto& style = get().style();
        const float inset = style.frameBorderSize * 0.5f;
        ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x + inset, min.y + inset),
            ImVec2(max.x - inset, max.y - inset),
            ImGui::GetColorU32(color),
            style.frameRounding,
            style.frameBorderSize);
    }

    /**
     * A dashed outline inside a rectangle, as the box of an image that is not loaded has it: dashes three times as long
     * as the outline is thick, with gaps of twice it, from each corner along its side.
     */
    void drawDashedOutline(const ImVec2& min, const ImVec2& max, const ImU32 color)
    {
        const float thickness = get().style().frameBorderSize;
        const float inset = thickness * 0.5f;
        const std::array corners{ ImVec2(min.x + inset, min.y + inset),
            ImVec2(max.x - inset, min.y + inset),
            ImVec2(max.x - inset, max.y - inset),
            ImVec2(min.x + inset, max.y - inset) };
        const float dash = thickness * 3.0f;
        const float gap = thickness * 2.0f;
        auto* drawList = ImGui::GetWindowDrawList();
        for (std::size_t side = 0; side < corners.size(); ++side) {
            const auto& from = corners[side];
            const auto& to = corners[(side + 1) % corners.size()];
            const float length = std::hypot(to.x - from.x, to.y - from.y);
            for (float at = 0.0f; at < length; at += dash + gap) {
                const float end = (std::min)(at + dash, length);
                drawList->AddLine(ImLerp(from, to, at / length), ImLerp(from, to, end / length), ImGui::GetColorU32(color), thickness);
            }
        }
    }

    /**
     * The frame of a control: the fill of a track, with an outline, which is in the error color around a value of the
     * error tone. A value of the warning tone shows it in its own color, so its frame keeps the plain outline.
     */
    void drawFrame(const ImRect& box, const Tone tone)
    {
        const auto& colors = get().colors();
        ImGui::GetWindowDrawList()->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(colors.track), get().style().frameRounding);
        drawOutline(box.Min, box.Max, tone == Tone::Error ? colors.error : colors.frameBorder);
    }

    /**
     * The frame of a button that is a part of a row, as a button of a stepper: an outline, and the look of a control
     * under the ray while the ray is on it, with a stronger one while the trigger is held on it.
     */
    void drawButtonFrame(const ImRect& box, const bool pointed, const bool held)
    {
        const auto& colors = get().colors();
        if (pointed || held) {
            ImGui::GetWindowDrawList()->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(held ? colors.pressed : colors.hover), get().style().frameRounding);
            drawOutline(box.Min, box.Max, held ? colors.glow : colors.accent);
        } else {
            drawOutline(box.Min, box.Max, colors.frameBorder);
        }
    }

    /**
     * The ring around a button, outside its box, as a dangerous one has it under the ray.
     */
    void drawButtonRing(const ImRect& box, const ImU32 color)
    {
        ImGui::GetWindowDrawList()->AddRect(ImVec2(box.Min.x - BUTTON_RING, box.Min.y - BUTTON_RING),
            ImVec2(box.Max.x + BUTTON_RING, box.Max.y + BUTTON_RING),
            ImGui::GetColorU32(color),
            get().style().frameRounding + BUTTON_RING,
            0,
            BUTTON_RING);
    }

    /**
     * A button with an arrow, as a stepper and a slider have at their ends. One that goes nowhere is disabled. Any other
     * has the look of a control under the ray while the ray is on it, and a stronger one while the trigger is held on it.
     */
    void drawStepButton(const ImRect& box, const Arrow arrow, const bool enabled, const bool pointed, const bool held, const float arrowScale)
    {
        ImGui::BeginDisabled(!enabled);
        drawButtonFrame(box, enabled && pointed, enabled && held);
        drawControlArrow(box.GetCenter(), arrow, arrowScale);
        ImGui::EndDisabled();
    }

    /**
     * The bar of the selected row: a solid accent fill that ends in a tip inside the row, which starts
     * `SELECTED_BAR_TIP_DEPTH` from its right edge and points at the edge's middle. It is one shape, so no seam shows
     * between the bar and its tip.
     */
    void drawSelectedBar(const ImVec2& min, const ImVec2& max)
    {
        const float rounding = get().style().frameRounding;
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->PathArcToFast(ImVec2(min.x + rounding, min.y + rounding), rounding, 6, 9);
        drawList->PathLineTo(ImVec2(max.x - SELECTED_BAR_TIP_DEPTH, min.y));
        drawList->PathLineTo(ImVec2(max.x, (min.y + max.y) * 0.5f));
        drawList->PathLineTo(ImVec2(max.x - SELECTED_BAR_TIP_DEPTH, max.y));
        drawList->PathArcToFast(ImVec2(min.x + rounding, max.y - rounding), rounding, 3, 6);
        drawList->PathFillConvex(get().colors().accent);
    }

    /**
     * How the item that was just added looks while the ray is on it: a tint with an outline, and a stronger one while
     * it is pressed. Call it before the item's content is drawn, which then lies over it.
     */
    void drawRowState()
    {
        const bool pressed = ImGui::IsItemActive();
        if (!pressed && !ImGui::IsItemHovered()) {
            return;
        }
        const auto& colors = get().colors();
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(pressed ? colors.pressed : colors.hover), get().style().frameRounding);
        drawOutline(min, max, pressed ? colors.glow : colors.frameBorder);
    }

    /**
     * The line of a section in a section, along the left of its rows: from under its heading down to the bottom of its
     * last row, where it turns toward the rows with a rounded corner, and stops at a right edge, short of the rows. It
     * shows what the section covers, so a row after it is seen to be the outer section's.
     */
    void drawSectionLine(const float left, const float top, const float bottom, const float right)
    {
        const auto& style = get().style();
        const float x = left + style.frameBorderSize * 0.5f;
        const float y = bottom - style.frameBorderSize * 0.5f;
        const float radius = std::clamp(right - x, 0.0f, style.frameRounding);
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->PathLineTo(ImVec2(x, top));
        drawList->PathArcTo(ImVec2(x + radius, y - radius), radius, IM_PI, IM_PI * 0.5f);
        drawList->PathLineTo(ImVec2(right, y));
        drawList->PathStroke(ImGui::GetColorU32(get().colors().rule), ImDrawFlags_None, style.frameBorderSize);
    }

    /**
     * The height of a tag: a line of its text inside its outline.
     */
    float tagHeight()
    {
        return TAG_LINE_HEIGHT + get().style().frameBorderSize * 2.0f;
    }

    /**
     * The width of a tag: its text in the detail size, with the room inside its outline.
     */
    float tagWidth(const std::string_view text)
    {
        const auto& style = get().style();
        ImGui::PushFont(nullptr, style.detailFontSize);
        const float width = textWidth(text);
        ImGui::PopFont();
        return std::ceil(width + (TAG_PADDING + style.frameBorderSize) * 2.0f);
    }

    /**
     * A tag, from its left in the middle of a height: a short text in the detail size and the muted color, in an
     * outline of a frame's border.
     */
    void drawTag(const float left, const float middle, const std::string_view text)
    {
        const auto& style = get().style();
        const float height = tagHeight();
        const ImVec2 min(std::floor(left), std::floor(middle - height * 0.5f));
        const ImVec2 max(min.x + tagWidth(text), min.y + height);
        drawOutline(min, max, get().colors().frameBorder);
        ImGui::PushFont(nullptr, style.detailFontSize);
        drawText(min.x + style.frameBorderSize + TAG_PADDING, max.x, min.y, height, get().colors().muted, text);
        ImGui::PopFont();
    }
}
