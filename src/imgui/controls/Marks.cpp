#include "Marks.h"

#include <array>
#include <cmath>

#include <imgui_internal.h>

#include "Controls.h"

namespace f4cf::imgui::controls
{
    /**
     * The mark of an error, around a point: a triangle filled in the error color with an `!` in it in the color on
     * it, drawn in a box of 20 by 18.
     */
    void drawErrorMark(const ImVec2& center)
    {
        const auto& colors = get().colors();
        const ImVec2 origin(std::floor(center.x - 10.0f), std::floor(center.y - 9.0f));
        const auto at = [&origin](const float x, const float y) {
            return ImVec2(origin.x + x, origin.y + y);
        };
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddTriangleFilled(at(10.0f, 1.0f), at(19.0f, 17.0f), at(1.0f, 17.0f), ImGui::GetColorU32(colors.error));
        const auto bang = ImGui::GetColorU32(colors.onError);
        drawList->AddRectFilled(at(9.0f, 6.0f), at(11.0f, 12.0f), bang);
        drawList->AddRectFilled(at(9.0f, 13.4f), at(11.0f, 15.4f), bang);
    }

    /**
     * The mark of a warning, around a point: a hollow triangle in the warning color with an `!` in it, drawn in a box
     * of 20 by 18. It is not the error mark, which is filled.
     */
    void drawWarningMark(const ImVec2& center)
    {
        constexpr float THICKNESS = 2.0f;
        const ImVec2 origin(std::floor(center.x - 10.0f), std::floor(center.y - 9.0f));
        const auto at = [&origin](const float x, const float y) {
            return ImVec2(origin.x + x, origin.y + y);
        };
        auto* drawList = ImGui::GetWindowDrawList();
        const auto color = ImGui::GetColorU32(get().colors().warning);
        drawList->AddTriangle(at(10.0f, 1.5f), at(18.5f, 16.5f), at(1.5f, 16.5f), color, THICKNESS);
        drawList->AddRectFilled(at(9.0f, 6.0f), at(11.0f, 12.0f), color);
        drawList->AddRectFilled(at(9.0f, 13.0f), at(11.0f, 15.0f), color);
    }

    /**
     * The mark of an info, around a point: a ring in the accent color with an `i` in it, drawn in a box of 20.
     */
    void drawInfoMark(const ImVec2& center)
    {
        const ImVec2 origin(std::floor(center.x - 10.0f), std::floor(center.y - 10.0f));
        const auto at = [&origin](const float x, const float y) {
            return ImVec2(origin.x + x, origin.y + y);
        };
        auto* drawList = ImGui::GetWindowDrawList();
        const auto color = ImGui::GetColorU32(get().colors().accent);
        drawList->AddCircle(at(10.0f, 10.0f), 8.5f, color, 0, 2.0f);
        drawList->AddRectFilled(at(9.0f, 8.5f), at(11.0f, 15.0f), color);
        drawList->AddCircleFilled(at(10.0f, 5.8f), 1.3f, color);
    }

    /**
     * A mark around a point: the warning, the error or the info mark, and nothing for none.
     */
    void drawMark(const Mark mark, const ImVec2& center)
    {
        switch (mark) {
        case Mark::Warning:
            drawWarningMark(center);
            break;
        case Mark::Error:
            drawErrorMark(center);
            break;
        case Mark::Info:
            drawInfoMark(center);
            break;
        case Mark::None:
            break;
        }
    }

    /**
     * A dot around a point, in a color, as a mark of a value that waits for something.
     */
    void drawDot(const ImVec2& center, const ImU32 color)
    {
        constexpr float RADIUS = DOT_WIDTH * 0.5f;
        ImGui::GetWindowDrawList()->AddCircleFilled(center, RADIUS, ImGui::GetColorU32(color));
    }

    /**
     * The mark of a list's chosen option, in its box of `CHECK_SIZE` from a corner.
     */
    void drawCheck(const ImVec2& min, const ImU32 color)
    {
        constexpr float THICKNESS = 2.5f;
        // as its design draws it, in a box of 24
        constexpr float SCALE = CHECK_SIZE / 24.0f;
        const std::array points{ ImVec2(min.x + 4.0f * SCALE, min.y + 12.5f * SCALE),
            ImVec2(min.x + 9.5f * SCALE, min.y + 18.0f * SCALE),
            ImVec2(min.x + 20.0f * SCALE, min.y + 6.5f * SCALE) };
        ImGui::GetWindowDrawList()->AddPolyline(points.data(), static_cast<int>(points.size()), ImGui::GetColorU32(color), THICKNESS);
    }

    /**
     * The Reset icon around a point, in the accent color: most of a circle, open at its left, with an arrowhead at its
     * upper end, drawn in a box of 24 at a part of that size. The stroke keeps its width.
     */
    void drawResetIcon(const ImVec2& center, const float scale)
    {
        // the circle from its lower left end around its right to its upper left end, in radians
        constexpr float RADIUS = 7.5f;
        constexpr float ARC_START = 2.744f;
        constexpr float ARC_END = -2.396f;
        const auto at = [&center, scale](const float x, const float y) {
            return ImVec2(center.x + (x - 12.0f) * scale, center.y + (y - 12.0f) * scale);
        };
        const auto color = ImGui::GetColorU32(get().colors().accent);
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->PathArcTo(at(12.1f, 12.1f), RADIUS * scale, ARC_START, ARC_END);
        drawList->PathLineTo(at(4.0f, 10.0f));
        drawList->PathStroke(color, ICON_THICKNESS);
        const std::array head{ at(4.0f, 5.0f), at(4.0f, 10.0f), at(9.0f, 10.0f) };
        drawList->AddPolyline(head.data(), static_cast<int>(head.size()), color, ICON_THICKNESS);
    }

    /**
     * The Undo icon around a point, in the warning color, as the change it takes back: an arrowhead to the left, whose
     * tail turns back down in a half circle, drawn in a box of 24 at a part of that size. The stroke keeps its width.
     */
    void drawUndoIcon(const ImVec2& center, const float scale)
    {
        const auto at = [&center, scale](const float x, const float y) {
            return ImVec2(center.x + (x - 12.0f) * scale, center.y + (y - 12.0f) * scale);
        };
        const auto color = ImGui::GetColorU32(get().colors().warning);
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->PathLineTo(at(4.0f, 9.0f));
        drawList->PathArcTo(at(14.0f, 14.0f), 5.0f * scale, -IM_PI / 2.0f, IM_PI / 2.0f);
        drawList->PathLineTo(at(8.0f, 19.0f));
        drawList->PathStroke(color, ICON_THICKNESS);
        const std::array head{ at(9.0f, 4.0f), at(4.0f, 9.0f), at(9.0f, 14.0f) };
        drawList->AddPolyline(head.data(), static_cast<int>(head.size()), color, ICON_THICKNESS);
    }

    /**
     * The arrow of a row that opens and closes, in its box from its top left corner: it points down while the row is
     * open, and right while it is closed, drawn in a box of 14.
     */
    void drawFoldArrow(const ImVec2& topLeft, const bool open, const ImU32 color)
    {
        const auto at = [&topLeft](const float x, const float y) {
            return ImVec2(topLeft.x + x, topLeft.y + y);
        };
        if (open) {
            ImGui::GetWindowDrawList()->AddTriangleFilled(at(2.0f, 4.0f), at(12.0f, 4.0f), at(7.0f, 11.0f), ImGui::GetColorU32(color));
        } else {
            ImGui::GetWindowDrawList()->AddTriangleFilled(at(4.0f, 2.0f), at(11.0f, 7.0f), at(4.0f, 12.0f), ImGui::GetColorU32(color));
        }
    }

    /**
     * A filled arrow around a point in the accent color, as the buttons of a stepper and a dropdown have, and a
     * slider's smaller.
     */
    void drawControlArrow(const ImVec2& center, const Arrow arrow, const float scale)
    {
        // the corners of each arrow in its box of ARROW_SIZE
        static constexpr std::array<std::array<ImVec2, 3>, 3> CORNERS{ {
            { ImVec2(10.0f, 2.0f), ImVec2(3.0f, 7.0f), ImVec2(10.0f, 12.0f) },
            { ImVec2(4.0f, 2.0f), ImVec2(11.0f, 7.0f), ImVec2(4.0f, 12.0f) },
            { ImVec2(2.0f, 4.0f), ImVec2(12.0f, 4.0f), ImVec2(7.0f, 11.0f) },
        } };
        const auto& corners = CORNERS[static_cast<std::size_t>(arrow)];
        const ImVec2 origin(center.x - ARROW_SIZE * scale * 0.5f, center.y - ARROW_SIZE * scale * 0.5f);
        const auto at = [&origin, scale](const ImVec2& corner) {
            return ImVec2(origin.x + corner.x * scale, origin.y + corner.y * scale);
        };
        ImGui::GetWindowDrawList()->AddTriangleFilled(at(corners[0]), at(corners[1]), at(corners[2]), ImGui::GetColorU32(get().colors().accent));
    }
}
