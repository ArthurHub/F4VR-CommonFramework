#pragma once

#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "Controls.h"
#include "Marks.h"

// The frames, outlines and boxes the controls are drawn in.
namespace f4cf::imgui::controls
{
    // how far from the selected-row bar's right edge its tip starts, inside the row
    constexpr float SELECTED_BAR_TIP_DEPTH = 10.0f;

    ImRect controlBox(float left, float top, float width);
    float popupTop(const ImRect& box, float height, const ImRect& window);
    void drawOutline(const ImVec2& min, const ImVec2& max, ImU32 color);
    void drawDashedOutline(const ImVec2& min, const ImVec2& max, ImU32 color);
    void drawFrame(const ImRect& box, Tone tone = Tone::Normal);
    void drawButtonFrame(const ImRect& box, bool pointed, bool held);
    void drawButtonRing(const ImRect& box, ImU32 color);
    void drawStepButton(const ImRect& box, Arrow arrow, bool enabled, bool pointed, bool held, float arrowScale = 1.0f);
    void drawSelectedBar(const ImVec2& min, const ImVec2& max);
    void drawRowState();
    void drawSectionLine(float left, float top, float bottom, float right);
    float tagHeight();
    float tagWidth(std::string_view text);
    void drawTag(float left, float middle, std::string_view text);
}
