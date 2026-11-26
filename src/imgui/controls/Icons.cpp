#include "Icons.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

#include <imgui_internal.h>

#include "Marks.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // the boxes the icons are designed in: a button's, and the others'
    constexpr float BUTTON_DESIGN_BOX = 20.0f;
    constexpr float DESIGN_BOX = 24.0f;

    // the strokes of a button's icon, in its box of 20
    constexpr float BUTTON_STROKE = 2.0f;

    /**
     * Draws the shapes of a button's icon by the coordinates of its design box of 20, scaled to the box it is drawn in.
     * ImGui's strokes have square ends and joins, so an opaque stroke has a dot on each of its points, which makes them
     * round.
     */
    class Pen
    {
    public:
        Pen(const ImVec2& center, const ImU32 color, const float scale)
            : _center(center),
              _color(color),
              _scale(scale),
              _half(BUTTON_DESIGN_BOX * 0.5f * scale),
              _stroke(BUTTON_STROKE * scale),
              _drawList(ImGui::GetWindowDrawList())
        {}

        void lineTo(const float x, const float y) const
        {
            _drawList->PathLineTo(at(x, y));
        }

        void curveTo(const float controlX, const float controlY, const float x, const float y) const
        {
            _drawList->PathBezierQuadraticCurveTo(at(controlX, controlY), at(x, y));
        }

        /**
         * An arc from the end of the path to a point, clockwise and the short way, as SVG's `A r r 0 0 1`: a radius
         * too small to reach the point is made as large as half the way.
         */
        void arcTo(const float x, const float y, const float designRadius) const
        {
            const float radius = designRadius * _scale;
            const auto from = _drawList->_Path.back();
            const auto to = at(x, y);
            const ImVec2 half((to.x - from.x) * 0.5f, (to.y - from.y) * 0.5f);
            const float length = std::hypot(half.x, half.y);
            const float rise = std::sqrt((std::max)(radius * radius - length * length, 0.0f));
            const ImVec2 center(from.x + half.x - half.y / length * rise, from.y + half.y + half.x / length * rise);
            const float start = std::atan2(from.y - center.y, from.x - center.x);
            const float end = std::atan2(to.y - center.y, to.x - center.x);
            // the arc starts with this point again, and a stroke makes a spike at two points that are one
            _drawList->_Path.pop_back();
            _drawList->PathArcTo(center, (std::max)(radius, length), start, end < start ? end + IM_PI * 2.0f : end);
        }

        void stroke(const ImDrawFlags flags = ImDrawFlags_None) const
        {
            // a filled circle draws through the path, so the dots wait for the stroke. A faded stroke has none, as
            // a dot over it would show
            const auto points = _drawList->_Path;
            _drawList->PathStroke(_color, _stroke, flags);
            if ((_color & IM_COL32_A_MASK) != IM_COL32_A_MASK) {
                return;
            }
            for (const auto& point : points) {
                _drawList->AddCircleFilled(point, _stroke * 0.5f, _color);
            }
        }

        /**
         * Fills the path, whose points have to go clockwise: ImGui's soft edge is on the wrong side of any other.
         */
        void fill() const
        {
            _drawList->PathFillConcave(_color);
        }

        void line(const std::initializer_list<ImVec2> points) const
        {
            for (const auto& point : points) {
                lineTo(point.x, point.y);
            }
            stroke();
        }

        void rect(const float left, const float top, const float right, const float bottom, const float rounding) const
        {
            _drawList->PathRect(at(left, top), at(right, bottom), rounding * _scale);
            stroke(ImDrawFlags_Closed);
        }

        /**
         * The outline of a circle. ImGui draws its circles half a pixel smaller than their radius.
         */
        void circle(const float x, const float y, const float radius) const
        {
            _drawList->AddCircle(at(x, y), radius * _scale + 0.5f, _color, 0, _stroke);
        }

        void disc(const float x, const float y, const float radius) const
        {
            _drawList->AddCircleFilled(at(x, y), radius * _scale, _color);
        }

    private:
        ImVec2 at(const float x, const float y) const
        {
            return { _center.x + x * _scale - _half, _center.y + y * _scale - _half };
        }

        ImVec2 _center;
        ImU32 _color;
        float _scale;
        float _half;
        float _stroke;
        ImDrawList* _drawList;
    };

    /**
     * A button's icon around a point, in a color, by its design in a box of 20 scaled by `scale`: strokes with round ends
     * and joins, or a fill.
     */
    void drawButtonIcon(const Icon icon, const ImVec2& center, const ImU32 color, const float scale)
    {
        const Pen pen(center, color, scale);
        switch (icon) {
        case Icon::Play:
            pen.lineTo(5.0f, 3.0f);
            pen.lineTo(16.0f, 10.0f);
            pen.lineTo(5.0f, 17.0f);
            pen.fill();
            break;
        case Icon::Stop:
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(center.x - 5.5f * scale, center.y - 5.5f * scale),
                ImVec2(center.x + 5.5f * scale, center.y + 5.5f * scale),
                color,
                1.5f * scale);
            break;
        case Icon::Bolt:
            pen.lineTo(11.0f, 2.0f);
            pen.lineTo(11.0f, 8.0f);
            pen.lineTo(16.0f, 8.0f);
            pen.lineTo(8.0f, 18.0f);
            pen.lineTo(9.0f, 11.0f);
            pen.lineTo(4.0f, 11.0f);
            pen.fill();
            break;
        case Icon::Target:
            pen.circle(10.0f, 10.0f, 6.5f);
            pen.disc(10.0f, 10.0f, 2.0f);
            pen.line({ { 10.0f, 1.0f }, { 10.0f, 4.0f } });
            pen.line({ { 10.0f, 16.0f }, { 10.0f, 19.0f } });
            pen.line({ { 1.0f, 10.0f }, { 4.0f, 10.0f } });
            pen.line({ { 16.0f, 10.0f }, { 19.0f, 10.0f } });
            break;
        case Icon::Check:
            pen.line({ { 3.5f, 10.5f }, { 8.0f, 15.0f }, { 16.5f, 5.5f } });
            break;
        case Icon::Window:
            pen.rect(2.5f, 3.5f, 17.5f, 16.5f, 1.5f);
            pen.line({ { 2.5f, 7.5f }, { 17.5f, 7.5f } });
            pen.line({ { 5.0f, 5.5f }, { 6.0f, 5.5f } });
            pen.line({ { 7.5f, 5.5f }, { 8.5f, 5.5f } });
            break;
        case Icon::External:
            pen.line({ { 11.0f, 3.0f }, { 17.0f, 3.0f }, { 17.0f, 9.0f } });
            pen.line({ { 17.0f, 3.0f }, { 9.0f, 11.0f } });
            pen.line({ { 14.0f, 12.0f }, { 14.0f, 17.0f }, { 3.0f, 17.0f }, { 3.0f, 6.0f }, { 8.0f, 6.0f } });
            break;
        case Icon::Link:
            pen.line({ { 8.5f, 11.5f }, { 11.5f, 8.5f } });
            pen.lineTo(9.5f, 5.5f);
            pen.lineTo(11.0f, 4.0f);
            pen.arcTo(15.5f, 8.5f, 3.2f);
            pen.lineTo(14.0f, 10.0f);
            pen.stroke();
            pen.lineTo(10.5f, 14.5f);
            pen.lineTo(9.0f, 16.0f);
            pen.arcTo(4.5f, 11.5f, 3.2f);
            pen.lineTo(6.0f, 10.0f);
            pen.stroke();
            break;
        case Icon::Gear:
            pen.circle(10.0f, 10.0f, 2.6f);
            pen.circle(10.0f, 10.0f, 5.5f);
            pen.line({ { 10.0f, 2.5f }, { 10.0f, 4.5f } });
            pen.line({ { 10.0f, 15.5f }, { 10.0f, 17.5f } });
            pen.line({ { 2.5f, 10.0f }, { 4.5f, 10.0f } });
            pen.line({ { 15.5f, 10.0f }, { 17.5f, 10.0f } });
            pen.line({ { 4.7f, 4.7f }, { 6.1f, 6.1f } });
            pen.line({ { 13.9f, 13.9f }, { 15.3f, 15.3f } });
            pen.line({ { 4.7f, 15.3f }, { 6.1f, 13.9f } });
            pen.line({ { 13.9f, 6.1f }, { 15.3f, 4.7f } });
            break;
        case Icon::Import:
            pen.line({ { 10.0f, 3.0f }, { 10.0f, 12.0f } });
            pen.line({ { 6.0f, 8.5f }, { 10.0f, 12.5f }, { 14.0f, 8.5f } });
            pen.line({ { 3.5f, 13.5f }, { 3.5f, 16.5f }, { 16.5f, 16.5f }, { 16.5f, 13.5f } });
            break;
        case Icon::Export:
            pen.line({ { 10.0f, 13.0f }, { 10.0f, 3.5f } });
            pen.line({ { 6.0f, 7.0f }, { 10.0f, 3.0f }, { 14.0f, 7.0f } });
            pen.line({ { 3.5f, 13.5f }, { 3.5f, 16.5f }, { 16.5f, 16.5f }, { 16.5f, 13.5f } });
            break;
        case Icon::Copy:
            pen.rect(6.5f, 6.5f, 16.5f, 16.5f, 1.5f);
            pen.line({ { 13.5f, 4.0f }, { 13.5f, 3.5f }, { 3.5f, 3.5f }, { 3.5f, 13.5f }, { 4.0f, 13.5f } });
            break;
        case Icon::Eye:
            pen.lineTo(1.5f, 10.0f);
            pen.curveTo(10.0f, 2.0f, 18.5f, 10.0f);
            pen.curveTo(10.0f, 18.0f, 1.5f, 10.0f);
            pen.stroke();
            pen.disc(10.0f, 10.0f, 2.5f);
            break;
        case Icon::Hand:
            pen.lineTo(6.0f, 11.0f);
            pen.lineTo(6.0f, 5.0f);
            pen.arcTo(8.6f, 5.0f, 1.3f);
            pen.lineTo(8.6f, 9.0f);
            pen.stroke();
            pen.lineTo(8.6f, 9.0f);
            pen.lineTo(8.6f, 3.8f);
            pen.arcTo(11.2f, 3.8f, 1.3f);
            pen.lineTo(11.2f, 9.0f);
            pen.stroke();
            pen.lineTo(11.2f, 9.0f);
            pen.lineTo(11.2f, 4.8f);
            pen.arcTo(13.8f, 4.8f, 1.3f);
            pen.lineTo(13.8f, 10.0f);
            pen.stroke();
            pen.lineTo(13.8f, 10.0f);
            pen.lineTo(13.8f, 7.5f);
            pen.arcTo(16.4f, 7.5f, 1.3f);
            pen.lineTo(16.4f, 12.0f);
            pen.curveTo(16.4f, 17.5f, 11.0f, 17.5f);
            pen.curveTo(7.5f, 17.5f, 5.5f, 14.5f);
            pen.lineTo(3.4f, 11.4f);
            pen.arcTo(5.6f, 10.0f, 1.3f);
            pen.lineTo(6.0f, 11.0f);
            pen.stroke();
            break;
        case Icon::Sound:
            pen.lineTo(3.0f, 7.5f);
            pen.lineTo(6.0f, 7.5f);
            pen.lineTo(10.5f, 4.0f);
            pen.lineTo(10.5f, 16.0f);
            pen.lineTo(6.0f, 12.5f);
            pen.lineTo(3.0f, 12.5f);
            pen.fill();
            pen.lineTo(13.5f, 7.0f);
            pen.curveTo(15.5f, 10.0f, 13.5f, 13.0f);
            pen.stroke();
            pen.lineTo(15.5f, 5.0f);
            pen.curveTo(19.0f, 10.0f, 15.5f, 15.0f);
            pen.stroke();
            break;
        case Icon::Pin:
            pen.lineTo(10.0f, 18.0f);
            pen.curveTo(4.0f, 11.5f, 4.0f, 8.0f);
            pen.arcTo(16.0f, 8.0f, 6.0f);
            pen.curveTo(16.0f, 11.5f, 10.0f, 18.0f);
            pen.stroke();
            pen.circle(10.0f, 8.0f, 2.0f);
            break;
        case Icon::Refresh:
            pen.lineTo(4.0f, 8.0f);
            pen.arcTo(15.5f, 6.0f, 6.5f);
            pen.stroke();
            pen.line({ { 16.0f, 2.5f }, { 16.0f, 6.5f }, { 12.0f, 6.5f } });
            pen.lineTo(16.0f, 12.0f);
            pen.arcTo(4.5f, 14.0f, 6.5f);
            pen.stroke();
            pen.line({ { 4.0f, 17.5f }, { 4.0f, 13.5f }, { 8.0f, 13.5f } });
            break;
        case Icon::Trash:
            pen.line({ { 3.5f, 5.5f }, { 16.5f, 5.5f } });
            pen.line({ { 8.0f, 5.5f }, { 8.0f, 3.5f }, { 12.0f, 3.5f }, { 12.0f, 5.5f } });
            pen.line({ { 5.5f, 5.5f }, { 6.5f, 17.0f }, { 13.5f, 17.0f }, { 14.5f, 5.5f } });
            break;
        default:
            break;
        }
    }
}

namespace f4cf::imgui::controls
{
    /**
     * The box an icon is designed in: 24 for Search, Cross, Minimize and Vibrate, 20 for a button's icon. Drawn in a box
     * of that size, it is as designed.
     */
    float iconDesignBox(const Icon icon)
    {
        switch (icon) {
        case Icon::Search:
        case Icon::Cross:
        case Icon::Minimize:
        case Icon::Vibrate:
            return DESIGN_BOX;
        default:
            return BUTTON_DESIGN_BOX;
        }
    }

    /**
     * An icon around a point, in a color, in a box of a size: its design scaled from its design box, strokes too.
     * Nothing for none. Inside `BeginDisabled` it is faded.
     */
    void drawIcon(const Icon icon, const ImVec2& center, const ImU32 color, const float size)
    {
        const float scale = size / iconDesignBox(icon);
        const auto ink = ImGui::GetColorU32(color);
        auto* drawList = ImGui::GetWindowDrawList();
        switch (icon) {
        case Icon::None:
            break;
        case Icon::Search: {
            // a circle with a handle, from the corner of its box
            const ImVec2 min(center.x - size * 0.5f, center.y - size * 0.5f);
            drawList->AddCircle(ImVec2(min.x + 10.5f * scale, min.y + 10.5f * scale), 6.0f * scale, ink, 0, ICON_THICKNESS * scale);
            drawList->AddLine(ImVec2(min.x + 15.0f * scale, min.y + 15.0f * scale), ImVec2(min.x + 20.0f * scale, min.y + 20.0f * scale), ink, ICON_THICKNESS * scale);
            break;
        }
        case Icon::Cross: {
            const float half = 6.0f * scale;
            const float thickness = ICON_THICKNESS * scale;
            drawList->AddLine(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), ink, thickness);
            drawList->AddLine(ImVec2(center.x + half, center.y - half), ImVec2(center.x - half, center.y + half), ink, thickness);
            break;
        }
        case Icon::Minimize:
            drawList->AddLine(ImVec2(center.x - 6.0f * scale, center.y + 5.0f * scale), ImVec2(center.x + 6.0f * scale, center.y + 5.0f * scale), ink, ICON_THICKNESS * scale);
            break;
        case Icon::Vibrate: {
            // a dot, with two arcs on each side of it
            constexpr float QUARTER = std::numbers::pi_v<float> / 4.0f;
            drawList->AddCircleFilled(center, 2.5f * scale, ink);
            for (const float radius : { 5.0f * scale, 9.0f * scale }) {
                drawList->PathArcTo(center, radius, 3.0f * QUARTER, 5.0f * QUARTER);
                drawList->PathStroke(ink, ICON_THICKNESS * scale);
                drawList->PathArcTo(center, radius, -QUARTER, QUARTER);
                drawList->PathStroke(ink, ICON_THICKNESS * scale);
            }
            break;
        }
        default:
            drawButtonIcon(icon, center, ink, scale);
            break;
        }
    }
}
