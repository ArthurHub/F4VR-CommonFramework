#include "UITextPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

#include "../ModBase.h"
#include "../common/MatrixUtils.h"
#include "../render/PrimitiveDrawRenderer.h"

namespace f4cf::vrui
{
    namespace
    {
        /**
         * Every live panel. A function-local static rather than a namespace-scope one so it cannot
         * be used before it is constructed: panels register from their own constructors, which mods
         * are free to run from their own static initializers.
         */
        std::vector<UITextPanel*>& panels()
        {
            static std::vector<UITextPanel*> instances;
            return instances;
        }

        /**
         * Two shared layers rather than one, because occlusion is a pipeline state for a whole draw
         * rather than a property of a shape: panels that the world may hide and panels that are
         * always readable cannot be batched together. Panels of each kind still share a layer, so
         * the usual case is one draw callback, not one per panel.
         *
         * The always-on-top layer is ordered just above the occluded one, so a panel that opted out
         * of being hidden is not then hidden by a panel that did not.
         */
        render::PrimitiveDrawRenderer& occludedRenderer()
        {
            static render::PrimitiveDrawRenderer instance("UITextPanel", render::DRAW_ORDER_PANELS, true);
            return instance;
        }

        render::PrimitiveDrawRenderer& alwaysOnTopRenderer()
        {
            static render::PrimitiveDrawRenderer instance("UITextPanelOnTop", render::DRAW_ORDER_PANELS + 1, false);
            return instance;
        }

        // Segments per 90-degree corner. Six is already smooth at the size a panel corner is drawn,
        // and every segment is a quad, so this is the arc's whole cost.
        constexpr int BORDER_ARC_SEGMENTS = 6;
        constexpr float HALF_PI = 1.57079633f;

        /**
         * Tessellate a rounded rectangular outline into the frame, in the plane spanned by right/up
         * and centred on `center`, growing inward from the width x height rectangle.
         *
         * Four straight quads make the edges, each shortened at both ends by the corner radius, and
         * four annular sectors fill the corners between the outer radius and the inner one. With a
         * zero radius the sectors vanish and the edges run corner to corner, which is the square case
         * falling out of the same code rather than needing its own.
         */
        void appendBorder(render::PrimitiveDraw& frame, const RE::NiPoint3& center, const RE::NiPoint3& right, const RE::NiPoint3& up, const float width, const float height,
            const float thickness, const float radius, const render::Color& color)
        {
            const auto at = [&](const float u, const float v) {
                return center + right * u + up * v;
            };

            const float halfW = width * 0.5f;
            const float halfH = height * 0.5f;
            const float limit = (std::min)(halfW, halfH);
            const float band = std::clamp(thickness, 0.0f, limit);
            const float outerR = std::clamp(radius, 0.0f, limit);
            // a radius smaller than the band leaves a square inner corner rather than a negative one
            const float innerR = (std::max)(0.0f, outerR - band);
            if (band <= 0.0f) {
                return;
            }

            const float innerW = halfW - band;
            const float innerH = halfH - band;
            frame.addQuad(at(-halfW + outerR, halfH), at(halfW - outerR, halfH), at(halfW - outerR, innerH), at(-halfW + outerR, innerH), color);
            frame.addQuad(at(-halfW + outerR, -innerH), at(halfW - outerR, -innerH), at(halfW - outerR, -halfH), at(-halfW + outerR, -halfH), color);
            frame.addQuad(at(-halfW, halfH - outerR), at(-innerW, halfH - outerR), at(-innerW, -halfH + outerR), at(-halfW, -halfH + outerR), color);
            frame.addQuad(at(innerW, halfH - outerR), at(halfW, halfH - outerR), at(halfW, -halfH + outerR), at(innerW, -halfH + outerR), color);

            if (outerR <= 0.0f) {
                return;
            }

            // corner centres, in the order their arcs sweep: top-right 0-90 degrees, then anticlockwise
            const std::array<std::pair<float, float>, 4> corners = { {
                { halfW - outerR, halfH - outerR },
                { -halfW + outerR, halfH - outerR },
                { -halfW + outerR, -halfH + outerR },
                { halfW - outerR, -halfH + outerR },
            } };
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                const auto [cu, cv] = corners[corner];
                const float base = static_cast<float>(corner) * HALF_PI;
                for (int segment = 0; segment < BORDER_ARC_SEGMENTS; ++segment) {
                    const float a0 = base + HALF_PI * static_cast<float>(segment) / BORDER_ARC_SEGMENTS;
                    const float a1 = base + HALF_PI * static_cast<float>(segment + 1) / BORDER_ARC_SEGMENTS;
                    const float cos0 = std::cos(a0);
                    const float sin0 = std::sin(a0);
                    const float cos1 = std::cos(a1);
                    const float sin1 = std::sin(a1);
                    frame.addQuad(at(cu + outerR * cos0, cv + outerR * sin0),
                        at(cu + outerR * cos1, cv + outerR * sin1),
                        at(cu + innerR * cos1, cv + innerR * sin1),
                        at(cu + innerR * cos0, cv + innerR * sin0),
                        color);
                }
            }
        }

        /**
         * Collect every visible panel's rows into one frame and hand it across to the render thread.
         *
         * Runs after the mod's onFrameUpdate, which is where vrui is pumped, so every panel's
         * transform is this frame's finished layout rather than the previous frame's. An empty frame
         * is still published: that is what puts the renderer dormant.
         */
        void onFrameEnd()
        {
            render::PrimitiveDraw occluded;
            render::PrimitiveDraw alwaysOnTop;
            for (const UITextPanel* panel : panels()) {
                panel->appendTo(panel->isOccluded() ? occluded : alwaysOnTop);
            }

            if (!occluded.empty()) {
                occludedRenderer().ensureInstalled();
            }
            if (!alwaysOnTop.empty()) {
                alwaysOnTopRenderer().ensureInstalled();
            }
            // published even when empty: that is what puts a layer dormant
            occludedRenderer().publish(std::move(occluded));
            alwaysOnTopRenderer().publish(std::move(alwaysOnTop));
        }
    }

    UITextPanel::UITextPanel(const std::string& name, const float width, const float height)
        : UIElement(name)
    {
        // Registered here, on the first panel ever built, rather than from a static initializer: a
        // mod that never creates one registers no pump and never builds the renderer.
        static const bool pumpRegistered = [] {
            registerFrameEndCallback(&onFrameEnd);
            return true;
        }();
        static_cast<void>(pumpRegistered);

        setSize(width, height);
        panels().push_back(this);
    }

    UITextPanel::~UITextPanel()
    {
        std::erase(panels(), this);
    }

    void UITextPanel::setContent(TextRowsCallback content)
    {
        _content = std::move(content);
    }

    void UITextPanel::setColor(const render::Color& color)
    {
        _color = color;
    }

    void UITextPanel::setAlign(const render::TextAlign align)
    {
        _align = align;
    }

    void UITextPanel::setTextHeight(const float units)
    {
        // a non-positive height would divide by zero in the row-count maths and draw nothing at all
        _textHeightUnits = (std::max)(0.01f, units);
    }

    void UITextPanel::setLineSpacing(const float multiplier)
    {
        // below 1.0 consecutive rows would overlap, which is never what a caller means by spacing
        _lineSpacing = (std::max)(1.0f, multiplier);
    }

    void UITextPanel::setBorder(const render::Color& color, const float thickness, const float cornerRadius)
    {
        _borderColor = color;
        _borderThicknessUnits = (std::max)(0.0f, thickness);
        _borderCornerRadiusUnits = (std::max)(0.0f, cornerRadius);
    }

    void UITextPanel::clearBorder()
    {
        _borderThicknessUnits = 0.0f;
    }

    void UITextPanel::setPadding(const float units)
    {
        _paddingUnits = (std::max)(0.0f, units);
    }

    void UITextPanel::setOccluded(const bool occluded)
    {
        _occluded = occluded;
    }

    std::string UITextPanel::toString() const
    {
        return std::format("UITextPanel({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
    }

    /**
     * Turn this frame's rows into plane-welded text runs filling the panel's own rectangle.
     *
     * The plane comes straight from the composed world transform, read with vrui's own axis
     * convention (+X right, +Z up, facing +Y) - the same one the widget meshes are authored with,
     * which is what lands the text in the same plane and facing as its sibling widgets instead of
     * turning to face the viewer like a billboard.
     *
     * Rows are laid out downward from the top edge and clipped in both directions, since the font
     * has neither wrapping nor scrolling: a row past the bottom is dropped, a row wider than the
     * panel is truncated. Silently spilling over the edges would cover the neighbouring widgets.
     *
     * Drawing nothing is how the panel honours being hidden, hidden by a parent, or detached.
     */
    void UITextPanel::appendTo(render::PrimitiveDraw& frame) const
    {
        if (!_content || !_attachNode || !calcVisibility()) {
            return;
        }

        std::vector<TextRow> rows;
        _content(rows);

        const RE::NiTransform world = common::MatrixUtils::localToWorldTransform(_attachNode->world, calculateTransform());
        const RE::NiMatrix3 toWorld = world.rotate.Transpose(); // the codebase's local->world convention
        const RE::NiPoint3 right = toWorld * RE::NiPoint3(1.0f, 0.0f, 0.0f);
        const RE::NiPoint3 up = toWorld * RE::NiPoint3(0.0f, 0.0f, 1.0f);

        // the composed scale carries both the element's own scale chain and the attach node's, so the
        // text grows and shrinks with its neighbours instead of drifting out of the layout
        const float worldWidth = _size.width * world.scale;
        const float worldHeight = _size.height * world.scale;

        // before the rows, so it is painted under them rather than over
        if (_borderThicknessUnits > 0.0f) {
            appendBorder(frame, world.translate, right, up, worldWidth, worldHeight, _borderThicknessUnits * world.scale, _borderCornerRadiusUnits * world.scale, _borderColor);
        }
        if (rows.empty()) {
            return;
        }

        const float glyphHeight = _textHeightUnits * world.scale;
        const float rowPitch = glyphHeight * _lineSpacing;
        if (rowPitch <= 0.0f) {
            return;
        }

        // the rows live inside the border, not on it, and inside the padding as well - the two add up
        // rather than sharing, so setting one never silently moves the other
        const float inset = (_borderThicknessUnits + _paddingUnits) * world.scale;
        const float areaWidth = worldWidth - inset * 2.0f;
        const float areaHeight = worldHeight - inset * 2.0f;
        if (areaWidth <= 0.0f || areaHeight < rowPitch) {
            logger::sample(5000,
                "Text panel '{}' has no room for text inside its border and padding ({:.2f} x {:.2f} units); nothing drawn",
                _name,
                _size.width - (_borderThicknessUnits + _paddingUnits) * 2.0f,
                _size.height - (_borderThicknessUnits + _paddingUnits) * 2.0f);
            return;
        }

        // the scale cancels out of these divisions, so what fits follows the layout rather than the
        // size the container happens to have been given
        const auto maxRows = static_cast<std::size_t>(areaHeight / rowPitch);
        const auto maxChars = static_cast<std::size_t>(areaWidth / (glyphHeight * render::GLYPH_ASPECT));

        // half the leading above the first row, so the block sits evenly inside the text area
        const float topOffset = areaHeight * 0.5f - (rowPitch - glyphHeight) * 0.5f;
        const float alignOffset = _align == render::TextAlign::Left ? areaWidth * -0.5f : _align == render::TextAlign::Right ? areaWidth * 0.5f : 0.0f;

        for (std::size_t row = 0; row < rows.size() && row < maxRows; ++row) {
            std::string_view text = rows[row].text;
            if (text.size() > maxChars) {
                text = text.substr(0, maxChars);
            }
            const RE::NiPoint3 anchor = world.translate + right * alignOffset + up * (topOffset - static_cast<float>(row) * rowPitch);
            frame.addOrientedText(text, anchor, right, up, glyphHeight, rows[row].color.value_or(_color), _align);
        }
    }
}
