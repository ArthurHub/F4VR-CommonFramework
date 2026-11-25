#include "Buttons.h"

#include "Frames.h"
#include "Text.h"

namespace f4cf::imgui::controls
{
    /**
     * A button in its box, with its label in the middle. The primary button is a solid accent fill, with its label in
     * the color on it, a glow under the ray, and an outline while it is pressed. The danger button is a solid error
     * fill, with a ring under the ray. The others have the frame of a button that is a part of a row, and a quiet one
     * has no outline while the ray is not on it. Inside `BeginDisabled` it is faded and is not pressed. Returns whether
     * it was pressed.
     */
    bool button(const char* label, const ImRect& box, const ButtonLook look)
    {
        ImGui::SetCursorScreenPos(box.Min);
        const bool pressed = ImGui::InvisibleButton(label, box.GetSize());
        const bool pointed = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const auto& colors = get().colors();
        const float rounding = get().style().frameRounding;
        auto* drawList = ImGui::GetWindowDrawList();
        if (look == ButtonLook::Primary) {
            drawList->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(pointed && !held ? colors.glow : colors.accent), rounding);
            if (held) {
                drawOutline(box.Min, box.Max, colors.glow);
            }
        } else if (look == ButtonLook::Danger) {
            drawList->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(colors.error), rounding);
            if (pointed || held) {
                drawButtonRing(box, colors.error);
            }
        } else if (look == ButtonLook::Secondary || pointed || held) {
            drawButtonFrame(box, pointed, held);
        }
        const auto textColor = look == ButtonLook::Primary ? colors.onAccent : look == ButtonLook::Danger ? colors.onError : colors.accent;
        drawTextIn(box, label, textColor, 0.5f);
        return pressed;
    }

    /**
     * Adds a button in its box and draws its frame; the caller draws the icon over it. Returns whether it was pressed,
     * and tells whether the ray is on it.
     */
    bool iconButton(const char* id, const ImRect& box, bool& pointed)
    {
        ImGui::SetCursorScreenPos(box.Min);
        const bool pressed = ImGui::InvisibleButton(id, box.GetSize());
        pointed = ImGui::IsItemHovered();
        drawButtonFrame(box, pointed, ImGui::IsItemActive());
        return pressed;
    }

    /**
     * A switcher that ends at the right: an off and an on cell, with the one of its value marked in its tone's color. On
     * is a solid fill, and off a ring, so a solid fill always says on. One with no value has neither marked. It only
     * draws: the caller takes the press. Returns its left edge.
     */
    float drawSwitcher(const float right, const float top, const std::optional<bool> on, const Tone tone, const char* offLabel, const char* onLabel)
    {
        const auto box = controlBox(right - SWITCHER_CELL_WIDTH * 2.0f, top, SWITCHER_CELL_WIDTH * 2.0f);
        const auto offCell = controlBox(box.Min.x, top, SWITCHER_CELL_WIDTH);
        const auto onCell = controlBox(offCell.Max.x, top, SWITCHER_CELL_WIDTH);
        const auto& colors = get().colors();
        const float rounding = get().style().frameRounding;
        const auto toneColors = get().tone(tone);
        drawFrame(box);
        if (on == true) {
            ImGui::GetWindowDrawList()->AddRectFilled(onCell.Min, onCell.Max, ImGui::GetColorU32(toneColors.color), rounding, ImDrawFlags_RoundCornersRight);
        } else if (on == false) {
            ImGui::GetWindowDrawList()->AddRectFilled(offCell.Min, offCell.Max, ImGui::GetColorU32(colors.pressed), rounding, ImDrawFlags_RoundCornersLeft);
            drawOutline(offCell.Min, offCell.Max, toneColors.color);
        }
        drawTextIn(offCell, offLabel, on == false ? toneColors.color : colors.muted, 0.5f);
        drawTextIn(onCell, onLabel, on == true ? toneColors.onColor : colors.muted, 0.5f);
        return box.Min.x;
    }
}
