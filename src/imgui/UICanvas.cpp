#include "UICanvas.h"

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
         * Resolution for a canvas of this size, scaled down to fit the shared atlas if need be.
         *
         * Both dimensions are scaled by the same factor, so an oversized canvas loses detail rather
         * than shape - the aspect ratio is derived from the vrui size and stays derived, which is the
         * whole point of not asking the caller for pixels. It does cost the one-unit-per-line
         * relation, since the font's pixel size is not scaled with it: past this size the text reads
         * larger against the canvas. A canvas that big is asking for more than one atlas can serve.
         */
        std::pair<int, int> pixelSizeFor(const std::string& name, const float width, const float height)
        {
            const float requestedWidth = (std::max)(1.0f, width * CANVAS_PIXELS_PER_UNIT);
            const float requestedHeight = (std::max)(1.0f, height * CANVAS_PIXELS_PER_UNIT);
            const float limit = static_cast<float>(MAX_PANEL_PIXEL_SIZE);
            const float fit = (std::min)(1.0f, (std::min)(limit / requestedWidth, limit / requestedHeight));
            if (fit < 1.0f) {
                // sampled: the size is re-evaluated every frame, so a persistently oversized canvas
                // would otherwise say so on every one of them
                logger::sample(5000,
                    "Canvas '{}' at {:.1f}x{:.1f} units needs more than the {}px atlas; resolution reduced to {:.0f}%",
                    name,
                    width,
                    height,
                    MAX_PANEL_PIXEL_SIZE,
                    fit * 100.0f);
            }
            return { static_cast<int>(std::lround(requestedWidth * fit)), static_cast<int>(std::lround(requestedHeight * fit)) };
        }
    }

    UICanvas::UICanvas(const std::string& name, const float width, const float height)
        : UIElement(name),
          _panel(nullptr)
    {
        // the initial resolution ignores the scale chain because the element has no parent yet;
        // onFrameUpdate corrects it once it is laid out
        const auto [pixelWidth, pixelHeight] = pixelSizeFor(name, width, height);
        _panel = std::make_unique<Panel>(name, pixelWidth, pixelHeight);

        setSize(width, height);
        _panel->setPlacement([this](PanelPlacement& out) {
            return resolvePlacement(out);
        });
    }

    /**
     * Re-derive the resolution from the size vrui actually laid out this frame, so resizing the
     * element or rescaling a parent container keeps the pixel density - and therefore the sharpness
     * of the text - rather than stretching what was rasterized for the old size.
     *
     * Runs before the ImGui layer builds its frame (vrui is pumped from the mod's onFrameUpdate, the
     * layer after it), so the new size is in place by the time the panel is packed.
     */
    void UICanvas::onFrameUpdate(vrui::UIFrameUpdateContext*)
    {
        refreshPixelSize();
    }

    void UICanvas::refreshPixelSize()
    {
        const auto size = calcSize();
        const auto [pixelWidth, pixelHeight] = pixelSizeFor(_name, size.width, size.height);
        if (pixelWidth != _panel->pixelWidth() || pixelHeight != _panel->pixelHeight()) {
            _panel->setPixelSize(pixelWidth, pixelHeight);
        }
    }

    void UICanvas::setContent(ContentCallback content)
    {
        _panel->setContent(std::move(content));
    }

    std::string UICanvas::toString() const
    {
        return std::format("UICanvas({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
    }

    /**
     * Hand vrui's placement to the panel, pulled by the ImGui layer once the frame's layout is done.
     *
     * vrui positions elements relative to the node its tree is attached to and applies no rotation of
     * its own, so composing the accumulated local transform onto that node's world transform lands
     * the canvas in the same plane and facing as its sibling widgets - which is what makes it read
     * as one of them. The panel quad's local axes (+X right, +Z up, facing along +Y) are the same
     * ones vrui's widget meshes are authored with, so the orientation carries over unchanged.
     *
     * Returning false is how the canvas honours being hidden, hidden by a parent, or detached: the
     * panel is skipped entirely for the frame rather than drawn somewhere stale.
     */
    bool UICanvas::resolvePlacement(PanelPlacement& out) const
    {
        if (!_attachNode || !calcVisibility()) {
            return false;
        }

        out.transform = common::MatrixUtils::localToWorldTransform(_attachNode->world, calculateTransform());
        // the composed scale carries both the element's own scale chain and the attach node's, so the
        // canvas grows and shrinks with its neighbours instead of drifting out of the layout
        out.worldWidth = _size.width * out.transform.scale;
        out.worldHeight = _size.height * out.transform.scale;
        return true;
    }
}
