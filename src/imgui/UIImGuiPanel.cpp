#include "UIImGuiPanel.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "../common/MatrixUtils.h"

namespace f4cf::imgui
{
    namespace
    {
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
            const float requestedWidth = (std::max)(1.0f, width * CANVAS_PIXELS_PER_UNIT);
            const float requestedHeight = (std::max)(1.0f, height * CANVAS_PIXELS_PER_UNIT);
            constexpr float limit = static_cast<float>(MAX_CANVAS_PIXEL_SIZE);
            const float fit = (std::min)(1.0f, (std::min)(limit / requestedWidth, limit / requestedHeight));
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
            return { static_cast<int>(std::lround(requestedWidth * fit)), static_cast<int>(std::lround(requestedHeight * fit)) };
        }
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
     * Runs before the ImGui layer builds its frame (vrui is pumped from the mod's onFrameUpdate, the
     * layer after it), so the canvas is in step by the time it is packed.
     */
    void UIImGuiPanel::onFrameUpdate(vrui::UIFrameUpdateContext*)
    {
        refreshCanvas();
    }

    /**
     * Sync the canvas with the panel's size and style.
     *
     * The resolution comes from the element's own size, not the scale chain above it: the content
     * renders at scale 1 and the quad stretches it with the layout, so everything inside scales
     * together. The style is converted with the ratio of that resolution rather than a fixed
     * CANVAS_PIXELS_PER_UNIT, which keeps it right for a panel shrunk to fit the atlas.
     */
    void UIImGuiPanel::refreshCanvas()
    {
        const auto [pixelWidth, pixelHeight] = pixelSizeFor(_name, _size.width, _size.height);
        if (pixelWidth != _canvas->pixelWidth() || pixelHeight != _canvas->pixelHeight()) {
            _canvas->setPixelSize(pixelWidth, pixelHeight);
        }

        const float pixelsPerUnit = _size.width > 0.0f ? static_cast<float>(_canvas->pixelWidth()) / _size.width : CANVAS_PIXELS_PER_UNIT;
        _canvas->setTextColor(_style.color);
        _canvas->setBackgroundColor(_style.background);
        _canvas->setBorder(_style.borderColor, _style.borderThicknessUnits * pixelsPerUnit, _style.cornerRadiusUnits * pixelsPerUnit);
        _canvas->setPadding(CanvasPadding{ .top = _style.padding.top * pixelsPerUnit,
            .right = _style.padding.right * pixelsPerUnit,
            .bottom = _style.padding.bottom * pixelsPerUnit,
            .left = _style.padding.left * pixelsPerUnit });
    }

    void UIImGuiPanel::setContent(ContentCallback content)
    {
        _canvas->setContent(std::move(content));
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

    std::string UIImGuiPanel::toString() const
    {
        return std::format("UIImGuiPanel({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
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
     * canvas is skipped entirely for the frame rather than drawn somewhere stale.
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
        return true;
    }
}
