#include "UIPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

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
        std::vector<UIPanel*>& panels()
        {
            static std::vector<UIPanel*> instances;
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
            static render::PrimitiveDrawRenderer instance("UIPanel", render::DRAW_ORDER_PANELS, true);
            return instance;
        }

        render::PrimitiveDrawRenderer& alwaysOnTopRenderer()
        {
            static render::PrimitiveDrawRenderer instance("UIPanelOnTop", render::DRAW_ORDER_PANELS + 1, false);
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
         * Fill the panel's rectangle into the frame, in the plane spanned by right/up and centred on
         * `center`, with the same rounded outline appendBorder traces.
         *
         * The radius and the corner centres are the border's own, so the fill ends exactly where the
         * border's outer edge begins: at a rounded corner there is no background left showing outside
         * the border, and no seam between the two either, since both are walked from the same angles.
         * With no border it is simply a rounded rectangle.
         *
         * Tessellated as one fan from the middle rather than as a band with caps, because every
         * triangle in a fan shares whole edges with its neighbours - splitting the rectangle into
         * slabs would leave T-junctions along the seams, and a half-transparent fill shows those as
         * hairline cracks.
         */
        void appendBackground(render::PrimitiveDraw& frame, const RE::NiPoint3& center, const RE::NiPoint3& right, const RE::NiPoint3& up, const float width, const float height,
            const float radius, const render::Color& color)
        {
            const auto at = [&](const float u, const float v) {
                return center + right * u + up * v;
            };

            const float halfW = width * 0.5f;
            const float halfH = height * 0.5f;
            if (halfW <= 0.0f || halfH <= 0.0f) {
                return;
            }

            const float outerR = std::clamp(radius, 0.0f, (std::min)(halfW, halfH));
            if (outerR <= 0.0f) {
                frame.addQuad(at(-halfW, halfH), at(halfW, halfH), at(halfW, -halfH), at(-halfW, -halfH), color);
                return;
            }

            // the same corner centres and sweep order appendBorder uses, so the outlines agree
            const std::array<std::pair<float, float>, 4> corners = { {
                { halfW - outerR, halfH - outerR },
                { -halfW + outerR, halfH - outerR },
                { -halfW + outerR, -halfH + outerR },
                { halfW - outerR, -halfH + outerR },
            } };

            // each corner's arc in turn; consecutive arcs are joined by the straight edge between
            // them, which the fan covers without needing a quad of its own
            constexpr std::size_t OUTLINE_POINTS = corners.size() * (BORDER_ARC_SEGMENTS + 1);
            std::array<RE::NiPoint3, OUTLINE_POINTS> outline{};
            std::size_t next = 0;
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                const auto [cu, cv] = corners[corner];
                const float base = static_cast<float>(corner) * HALF_PI;
                for (int segment = 0; segment <= BORDER_ARC_SEGMENTS; ++segment) {
                    const float angle = base + HALF_PI * static_cast<float>(segment) / BORDER_ARC_SEGMENTS;
                    outline[next++] = at(cu + outerR * std::cos(angle), cv + outerR * std::sin(angle));
                }
            }
            for (std::size_t point = 0; point < OUTLINE_POINTS; ++point) {
                frame.addTriangle(center, outline[point], outline[(point + 1) % OUTLINE_POINTS], color);
            }
        }

        /**
         * Collect every visible panel into one frame per layer and hand them across to the render
         * thread.
         *
         * Runs after the mod's onFrameUpdate, which is where vrui is pumped, so every panel's
         * transform is this frame's finished layout rather than the previous frame's. An empty frame
         * is still published: that is what puts the renderer dormant.
         */
        void onFrameEnd()
        {
            render::PrimitiveDraw occluded;
            render::PrimitiveDraw alwaysOnTop;
            for (const UIPanel* panel : panels()) {
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

    UIPanel::UIPanel(const std::string& name, const float width, const float height)
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

    UIPanel::~UIPanel()
    {
        std::erase(panels(), this);
    }

    void UIPanel::setStyle(const UIPanelStyle& style)
    {
        _style.color = style.color;
        setBackgroundColor(style.background);
        setBorder(style.borderColor, style.borderThicknessUnits, style.cornerRadiusUnits);
        setPadding(style.padding);
    }

    void UIPanel::setBackgroundColor(const render::Color& color)
    {
        _style.background = color;
    }

    void UIPanel::setBorder(const render::Color& color, const float thickness, const float cornerRadius)
    {
        _style.borderColor = color;
        _style.borderThicknessUnits = (std::max)(0.0f, thickness);
        _style.cornerRadiusUnits = (std::max)(0.0f, cornerRadius);
    }

    void UIPanel::clearBorder()
    {
        // the rounding is deliberately kept: it shapes the background, which is still there
        _style.borderThicknessUnits = 0.0f;
    }

    void UIPanel::setCornerRadius(const float units)
    {
        _style.cornerRadiusUnits = (std::max)(0.0f, units);
    }

    void UIPanel::setPadding(const UIPadding& padding)
    {
        // a negative side would pull the content out over the border instead of away from it
        _style.padding = { (std::max)(0.0f, padding.top), (std::max)(0.0f, padding.right), (std::max)(0.0f, padding.bottom), (std::max)(0.0f, padding.left) };
    }

    void UIPanel::setPadding(const float units)
    {
        setPadding(UIPadding::all(units));
    }

    void UIPanel::setOccluded(const bool occluded)
    {
        _occluded = occluded;
    }

    std::string UIPanel::toString() const
    {
        return std::format("{}({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            typeName(),
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
    }

    /**
     * Draw the chrome into the panel's own rectangle, then ask the subclass for the content inside
     * what the border and padding leave.
     *
     * The plane comes straight from the composed world transform, read with vrui's own axis
     * convention (+X right, +Z up, facing +Y) - the same one the widget meshes are authored with,
     * which is what lands the panel in the same plane and facing as its sibling widgets instead of
     * turning to face the viewer like a billboard.
     *
     * Drawing nothing is how the panel honours being hidden, hidden by a parent, or detached.
     */
    void UIPanel::appendTo(render::PrimitiveDraw& frame) const
    {
        if (!_attachNode || !calcVisibility()) {
            return;
        }

        const RE::NiTransform world = common::MatrixUtils::localToWorldTransform(_attachNode->world, calculateTransform());
        const RE::NiMatrix3 toWorld = world.rotate.Transpose(); // the codebase's local->world convention
        const RE::NiPoint3 right = toWorld * RE::NiPoint3(1.0f, 0.0f, 0.0f);
        const RE::NiPoint3 up = toWorld * RE::NiPoint3(0.0f, 0.0f, 1.0f);

        // the composed scale carries both the element's own scale chain and the attach node's, so the
        // panel grows and shrinks with its neighbours instead of drifting out of the layout
        const float worldWidth = _size.width * world.scale;
        const float worldHeight = _size.height * world.scale;

        // depth testing is off within the layer, so this order is what stacks them: the background
        // under the border (and the renderer puts both under the content)
        const UIPanelStyle style = resolveStyle();
        const float cornerRadius = style.cornerRadiusUnits * world.scale;
        if (style.background.a > 0.0f) {
            appendBackground(frame, world.translate, right, up, worldWidth, worldHeight, cornerRadius, style.background);
        }
        if (style.borderThicknessUnits > 0.0f) {
            appendBorder(frame, world.translate, right, up, worldWidth, worldHeight, style.borderThicknessUnits * world.scale, cornerRadius, style.borderColor);
        }

        // the content lives inside the border, not on it, and inside the padding as well - the two add
        // up rather than sharing, so setting one never silently moves the other
        const float border = style.borderThicknessUnits * world.scale;
        const float insetTop = border + style.padding.top * world.scale;
        const float insetRight = border + style.padding.right * world.scale;
        const float insetBottom = border + style.padding.bottom * world.scale;
        const float insetLeft = border + style.padding.left * world.scale;
        const float areaWidth = worldWidth - insetLeft - insetRight;
        const float areaHeight = worldHeight - insetTop - insetBottom;
        if (areaWidth <= 0.0f || areaHeight <= 0.0f) {
            logger::sample(5000,
                "Panel '{}' has no room for content inside its border and padding ({:.2f} x {:.2f} units); only its chrome is drawn",
                _name,
                _size.width - style.borderThicknessUnits * 2.0f - style.padding.left - style.padding.right,
                _size.height - style.borderThicknessUnits * 2.0f - style.padding.top - style.padding.bottom);
            return;
        }

        // the sides can differ, so the content area need not be centred on the panel: half the
        // difference between opposite insets is its offset from the panel's middle
        const UIPanelContentArea area{
            .center = world.translate + right * ((insetLeft - insetRight) * 0.5f) + up * ((insetBottom - insetTop) * 0.5f),
            .right = right,
            .up = up,
            .width = areaWidth,
            .height = areaHeight,
            .scale = world.scale,
        };
        appendContent(frame, area);
    }

    /**
     * Fit the texture into the rectangle and add it as one textured quad.
     *
     * Contain needs the image's proportions, which are only known once the texture has loaded - but
     * nothing is drawn before then anyway, so the fit never has to guess. A texture that reports no
     * size falls back to filling the rectangle.
     */
    void UIPanel::appendImage(render::PrimitiveDraw& frame, render::Texture& texture, const RE::NiPoint3& center, const RE::NiPoint3& right, const RE::NiPoint3& up,
        const float width, const float height, const UIImageFit fit, const render::Color& tint)
    {
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }
        render::TextureView view = texture.view(); // loads on first use, so it must run game-side
        if (!view) {
            return;
        }

        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        if (fit == UIImageFit::Contain && texture.width() > 0 && texture.height() > 0) {
            const float imageAspect = static_cast<float>(texture.width()) / static_cast<float>(texture.height());
            if (width > height * imageAspect) {
                halfW = halfH * imageAspect; // the rectangle is wider than the image: empty bands left and right
            } else {
                halfH = halfW / imageAspect; // taller: empty bands above and below
            }
        }

        const auto at = [&](const float u, const float v) {
            return center + right * u + up * v;
        };
        frame.addImage(std::move(view), texture.isSRGB(), at(-halfW, halfH), at(halfW, halfH), at(halfW, -halfH), at(-halfW, -halfH), tint);
    }
}
