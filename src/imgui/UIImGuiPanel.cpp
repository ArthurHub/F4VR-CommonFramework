#include "UIImGuiPanel.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

#include "../common/MatrixUtils.h"

namespace f4cf::imgui
{
    namespace
    {
        /**
         * How far a panel of this size is scaled down to fit the shared atlas: 1 when it fits, otherwise
         * one factor for both dimensions.
         */
        float atlasFitFor(const float width, const float height)
        {
            const float requestedWidth = (std::max)(1.0f, width * CANVAS_PIXELS_PER_UNIT);
            const float requestedHeight = (std::max)(1.0f, height * CANVAS_PIXELS_PER_UNIT);
            constexpr float limit = static_cast<float>(MAX_CANVAS_PIXEL_SIZE);
            return (std::min)(1.0f, (std::min)(limit / requestedWidth, limit / requestedHeight));
        }

        /**
         * Resolution for a panel of this size, scaled down to fit the shared atlas if need be.
         *
         * Both dimensions are scaled by the same factor, so an oversized panel loses detail rather
         * than shape - the aspect ratio is derived from the vrui size and stays derived, which is the
         * whole point of not asking the caller for pixels. It does cost the text its size relative to
         * the panel, since the font's pixel size is not scaled with it: past this size the text reads
         * larger against the panel. A panel that big is asking for more than one atlas can serve.
         */
        std::pair<int, int> pixelSizeFor(const std::string& name, const float width, const float height)
        {
            const float fit = atlasFitFor(width, height);
            if (fit < 1.0f) {
                // sampled: the size is re-evaluated every frame, so a persistently oversized panel
                // would otherwise say so on every one of them
                logger::sample(5000,
                    "ImGui panel '{}' at {:.1f}x{:.1f} units needs more than the {}px atlas; resolution reduced to {:.0f}%",
                    name,
                    width,
                    height,
                    MAX_CANVAS_PIXEL_SIZE,
                    fit * 100.0f);
            }
            const float requestedWidth = (std::max)(1.0f, width * CANVAS_PIXELS_PER_UNIT);
            const float requestedHeight = (std::max)(1.0f, height * CANVAS_PIXELS_PER_UNIT);
            return { static_cast<int>(std::lround(requestedWidth * fit)), static_cast<int>(std::lround(requestedHeight * fit)) };
        }

        /**
         * The canvas pixels a panel of this size has per vrui unit - CANVAS_PIXELS_PER_UNIT, less for a
         * panel scaled down to fit the atlas.
         */
        float pixelsPerUnitFor(const float width, const float height)
        {
            return CANVAS_PIXELS_PER_UNIT * atlasFitFor(width, height);
        }

        bool widthFollowsContent(const vrui::UIPanelSizing sizing)
        {
            return sizing == vrui::UIPanelSizing::FitContent || sizing == vrui::UIPanelSizing::FixedHeight;
        }

        bool heightFollowsContent(const vrui::UIPanelSizing sizing)
        {
            return sizing == vrui::UIPanelSizing::FitContent || sizing == vrui::UIPanelSizing::FixedWidth;
        }

        // what the border and padding take off each dimension, in vrui units - the same insets the
        // canvas takes off its pixels
        float chromeWidthOf(const vrui::UIPanelStyle& style)
        {
            return style.borderThicknessUnits * 2.0f + style.padding.left + style.padding.right;
        }

        float chromeHeightOf(const vrui::UIPanelStyle& style)
        {
            return style.borderThicknessUnits * 2.0f + style.padding.top + style.padding.bottom;
        }
    }

    UIImGuiPanel::UIImGuiPanel(const std::string& name)
        : UIImGuiPanel(name, 0.0f, 0.0f)
    {
        _sizing = vrui::UIPanelSizing::FitContent;
    }

    UIImGuiPanel::UIImGuiPanel(const std::string& name, const float width)
        : UIImGuiPanel(name, width, 0.0f)
    {
        _sizing = vrui::UIPanelSizing::FixedWidth;
    }

    UIImGuiPanel::UIImGuiPanel(const std::string& name, const float width, const float height)
        : UIElement(name),
          _canvas(nullptr)
    {
        const auto [pixelWidth, pixelHeight] = pixelSizeFor(name, width, height);
        _canvas = std::make_unique<Canvas>(name, pixelWidth, pixelHeight);

        setSize(width, height);
        _canvas->setPlacement([this](CanvasPlacement& out) {
            return resolvePlacement(out);
        });

        // hand the canvas the default look straight away, so it is never in a state no style describes
        refreshCanvas();
    }

    /**
     * For each dimension that follows the content, set the size to what the content took up plus the
     * border and padding, before the container sums its children.
     *
     * What the content took up is last frame's - see setSizing - so it is converted at the pixels per
     * unit it was drawn at, which are this size's until it is changed here. It is capped at the room it
     * was laid out in, so content that ran past it, like a line wider than the max width, is clipped
     * rather than growing the panel past what it may. It is floored at one pixel, and so is a panel
     * that has not measured anything yet: a canvas with no room inside its chrome does not run its
     * content, which would then never be measured to grow it.
     *
     * A hidden panel is skipped: its container leaves it out of the layout, and nothing new was
     * measured while it was hidden.
     */
    void UIImGuiPanel::onLayoutUpdate(vrui::UIFrameUpdateContext*)
    {
        if (_sizing == vrui::UIPanelSizing::Fixed || !calcVisibility()) {
            return;
        }

        const float pixelsPerUnit = pixelsPerUnitFor(_size.width, _size.height);
        const CanvasSize room = contentRoomPixels(pixelsPerUnit);
        const CanvasSize content = _canvas->measuredContentSize().value_or(CanvasSize{});
        if (widthFollowsContent(_sizing)) {
            _size.width = std::clamp(content.width, 1.0f, room.width) / pixelsPerUnit + chromeWidthOf(_style);
        }
        if (heightFollowsContent(_sizing)) {
            _size.height = std::clamp(content.height, 1.0f, room.height) / pixelsPerUnit + chromeHeightOf(_style);
        }
    }

    /**
     * Runs before the ImGui layer builds its frame (vrui is pumped from the mod's onFrameUpdate, the
     * layer after it), so the canvas is in step by the time it is packed.
     */
    void UIImGuiPanel::onFrameUpdate(vrui::UIFrameUpdateContext*)
    {
        refreshCanvas();
    }

    /**
     * Sync the canvas with the panel's size, sizing and style.
     *
     * The resolution comes from the element's own size, not the scale chain above it: the content
     * renders at scale 1 and the quad stretches it with the layout, so everything inside scales
     * together. The style and the content's room are converted at the panel's own pixels per unit
     * rather than a fixed CANVAS_PIXELS_PER_UNIT, which keeps them right for a panel shrunk to fit the
     * atlas.
     */
    void UIImGuiPanel::refreshCanvas()
    {
        const auto [pixelWidth, pixelHeight] = pixelSizeFor(_name, _size.width, _size.height);
        if (pixelWidth != _canvas->pixelWidth() || pixelHeight != _canvas->pixelHeight()) {
            _canvas->setPixelSize(pixelWidth, pixelHeight);
        }

        const float pixelsPerUnit = pixelsPerUnitFor(_size.width, _size.height);
        _canvas->setAvailableContentSize(contentRoomPixels(pixelsPerUnit));
        _canvas->setTextColor(_style.color);
        _canvas->setBackgroundColor(_style.background);
        _canvas->setBorder(_style.borderColor, _style.borderThicknessUnits * pixelsPerUnit, _style.cornerRadiusUnits * pixelsPerUnit);
        _canvas->setPadding(CanvasPadding{ .top = _style.padding.top * pixelsPerUnit,
            .right = _style.padding.right * pixelsPerUnit,
            .bottom = _style.padding.bottom * pixelsPerUnit,
            .left = _style.padding.left * pixelsPerUnit });
    }

    /**
     * The room the content is laid out in, in canvas pixels: for each dimension that follows it, as far
     * as that dimension may grow - the max width, or the atlas - less the border and padding; 0, the
     * canvas's own room, for a dimension the caller sized.
     *
     * The atlas is measured in this panel's own pixels, so a panel is only ever scaled down to fit it
     * by a size its caller chose. Were the content to scale it down, the content would take up more of
     * the smaller canvas, the panel would grow to hold it and scale down further, without end.
     */
    CanvasSize UIImGuiPanel::contentRoomPixels(const float pixelsPerUnit) const
    {
        constexpr float atlas = static_cast<float>(MAX_CANVAS_PIXEL_SIZE);
        CanvasSize room;
        if (widthFollowsContent(_sizing)) {
            const float maxWidth = _maxWidthUnits > 0.0f ? (std::min)(_maxWidthUnits * pixelsPerUnit, atlas) : atlas;
            room.width = (std::max)(1.0f, maxWidth - chromeWidthOf(_style) * pixelsPerUnit);
        }
        if (heightFollowsContent(_sizing)) {
            room.height = (std::max)(1.0f, atlas - chromeHeightOf(_style) * pixelsPerUnit);
        }
        return room;
    }

    void UIImGuiPanel::setContent(ContentCallback content)
    {
        _canvas->setContent(std::move(content));
    }

    void UIImGuiPanel::setSizing(const vrui::UIPanelSizing sizing)
    {
        _sizing = sizing;
    }

    void UIImGuiPanel::setMaxWidth(const float units)
    {
        _maxWidthUnits = (std::max)(0.0f, units);
    }

    void UIImGuiPanel::setOccluded(const bool occluded)
    {
        _canvas->setOccluded(occluded);
    }

    void UIImGuiPanel::setStyle(const vrui::UIPanelStyle& style)
    {
        setTextColor(style.color);
        setBackgroundColor(style.background);
        setBorder(style.borderColor, style.borderThicknessUnits, style.cornerRadiusUnits);
        setPadding(style.padding);
    }

    void UIImGuiPanel::setTextColor(const render::Color& color)
    {
        _style.color = color;
    }

    void UIImGuiPanel::setBackgroundColor(const render::Color& color)
    {
        _style.background = color;
    }

    void UIImGuiPanel::setBorder(const render::Color& color, const float thicknessUnits, const float cornerRadiusUnits)
    {
        _style.borderColor = color;
        _style.borderThicknessUnits = (std::max)(0.0f, thicknessUnits);
        _style.cornerRadiusUnits = (std::max)(0.0f, cornerRadiusUnits);
    }

    void UIImGuiPanel::clearBorder()
    {
        // the rounding is deliberately kept: it shapes the background, which is still there
        _style.borderThicknessUnits = 0.0f;
    }

    void UIImGuiPanel::setCornerRadius(const float units)
    {
        _style.cornerRadiusUnits = (std::max)(0.0f, units);
    }

    void UIImGuiPanel::setPadding(const vrui::UIPadding& padding)
    {
        // a negative side would push the content out over the border instead of away from it
        _style.padding = { (std::max)(0.0f, padding.top), (std::max)(0.0f, padding.right), (std::max)(0.0f, padding.bottom), (std::max)(0.0f, padding.left) };
    }

    void UIImGuiPanel::setPadding(const float units)
    {
        setPadding(vrui::UIPadding::all(units));
    }

    /**
     * One line in the vrui panels' shape, a size that follows the content tagged the same way: "auto"
     * for both dimensions, "auto-w" or "auto-h" for one.
     */
    std::string UIImGuiPanel::toString() const
    {
        const std::string_view sizing = _sizing == vrui::UIPanelSizing::FitContent    ? " auto"
                                        : _sizing == vrui::UIPanelSizing::FixedWidth  ? " auto-h"
                                        : _sizing == vrui::UIPanelSizing::FixedHeight ? " auto-w"
                                                                                      : "";
        return std::format("UIImGuiPanel({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f}{})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height,
            sizing);
    }

    /**
     * The element's fields, then the ones a vrui::UIPanel adds: the padding, as Pad:(top,right,bottom,left),
     * and while the width follows the content its cap, as MaxW:(width).
     */
    void UIImGuiPanel::writeDevLayoutFields(std::string& line) const
    {
        UIElement::writeDevLayoutFields(line);
        const vrui::UIPadding& padding = _style.padding;
        line += std::format(", Pad:({:.2f},{:.2f},{:.2f},{:.2f})", padding.top, padding.right, padding.bottom, padding.left);
        if (widthFollowsContent(_sizing)) {
            line += std::format(", MaxW:({:.2f})", _maxWidthUnits);
        }
    }

    void UIImGuiPanel::readDevLayoutFields(const DevLayoutFields& fields)
    {
        UIElement::readDevLayoutFields(fields);
        if (const auto padding = fields.find("Pad"); padding != fields.end() && padding->second.size() == 4) {
            const std::vector<float>& sides = padding->second;
            setPadding(vrui::UIPadding{ .top = sides[0], .right = sides[1], .bottom = sides[2], .left = sides[3] });
        }
        if (const auto maxWidth = fields.find("MaxW"); maxWidth != fields.end() && maxWidth->second.size() == 1) {
            setMaxWidth(maxWidth->second[0]);
        }
    }

    /**
     * Hand vrui's placement to the canvas, pulled by the ImGui layer once the frame's layout is done.
     *
     * vrui positions elements relative to the node its tree is attached to and applies no rotation of
     * its own, so composing the accumulated local transform onto that node's world transform lands
     * the panel in the same plane and facing as its sibling widgets - which is what makes it read
     * as one of them. The canvas quad's local axes (+X right, +Z up, facing along +Y) are the same
     * ones vrui's widget meshes are authored with, so the orientation carries over unchanged.
     *
     * Returning false is how the panel honours being hidden, hidden by a parent, or detached: the
     * canvas is skipped entirely for the frame rather than drawn somewhere stale. A panel sized to
     * content it has not measured yet is laid out without being shown, so its first frame on screen
     * is already at its size.
     */
    bool UIImGuiPanel::resolvePlacement(CanvasPlacement& out) const
    {
        if (!_attachNode || !calcVisibility()) {
            return false;
        }

        out.transform = common::MatrixUtils::localToWorldTransform(_attachNode->world, calculateTransform());
        // the composed scale carries both the element's own scale chain and the attach node's, so the
        // panel grows and shrinks with its neighbours instead of drifting out of the layout
        out.worldWidth = _size.width * out.transform.scale;
        out.worldHeight = _size.height * out.transform.scale;
        out.show = _sizing == vrui::UIPanelSizing::Fixed || _canvas->measuredContentSize().has_value();
        return true;
    }
}
