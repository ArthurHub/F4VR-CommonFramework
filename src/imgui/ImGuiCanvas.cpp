#include "ImGuiCanvas.h"

#include <algorithm>

#include "../ModBase.h"
#include "ImGuiLayer.h"

namespace f4cf::imgui
{
    Canvas::Canvas(std::string name, const int pixelWidth, const int pixelHeight)
        : _name(std::move(name)),
          _pixelWidth(std::clamp(pixelWidth, 1, MAX_CANVAS_PIXEL_SIZE)),
          _pixelHeight(std::clamp(pixelHeight, 1, MAX_CANVAS_PIXEL_SIZE))
    {
        // Registering the pump here, on the first canvas ever built, is what keeps ImGui out of mods
        // that never draw one: ModBase names no symbol in this subsystem, so with static-library
        // semantics none of its object files - and therefore none of ImGui - is pulled into such a
        // mod at all. Once a canvas exists the framework pumps it, with nothing for the mod to
        // remember to call.
        static const bool pumpRegistered = [] {
            registerFrameEndCallback(&internal::onFrameEnd);
            return true;
        }();
        static_cast<void>(pumpRegistered);

        internal::registerCanvas(this);
    }

    Canvas::~Canvas()
    {
        internal::unregisterCanvas(this);
    }

    void Canvas::setContent(ContentCallback content)
    {
        _content = std::move(content);
    }

    void Canvas::setPlacement(PlacementProvider placement)
    {
        _placement = std::move(placement);
    }

    void Canvas::setPixelSize(const int pixelWidth, const int pixelHeight)
    {
        _pixelWidth = std::clamp(pixelWidth, 1, MAX_CANVAS_PIXEL_SIZE);
        _pixelHeight = std::clamp(pixelHeight, 1, MAX_CANVAS_PIXEL_SIZE);
    }

    void Canvas::setOccluded(const bool occluded)
    {
        _occluded = occluded;
    }

    void Canvas::setTextColor(const render::Color& color)
    {
        _textColor = color;
    }

    void Canvas::setBackgroundColor(const render::Color& color)
    {
        _background = color;
    }

    void Canvas::setBorder(const render::Color& color, const float thicknessPixels, const float cornerRadiusPixels)
    {
        _borderColor = color;
        _borderThickness = (std::max)(0.0f, thicknessPixels);
        _cornerRadius = (std::max)(0.0f, cornerRadiusPixels);
    }

    void Canvas::clearBorder()
    {
        // the rounding is deliberately kept: it shapes the background, which is still there
        _borderThickness = 0.0f;
    }

    void Canvas::setCornerRadius(const float pixels)
    {
        _cornerRadius = (std::max)(0.0f, pixels);
    }

    void Canvas::setPadding(const CanvasPadding& padding)
    {
        // a negative side would push the content out over the border instead of away from it
        _padding = { (std::max)(0.0f, padding.top), (std::max)(0.0f, padding.right), (std::max)(0.0f, padding.bottom), (std::max)(0.0f, padding.left) };
    }

    void Canvas::setPadding(const float pixels)
    {
        const float side = (std::max)(0.0f, pixels);
        setPadding(CanvasPadding{ side, side, side, side });
    }

    void Canvas::setAvailableContentSize(const CanvasSize& size)
    {
        constexpr float limit = static_cast<float>(MAX_CANVAS_PIXEL_SIZE);
        _availableContentSize = { .width = std::clamp(size.width, 0.0f, limit), .height = std::clamp(size.height, 0.0f, limit) };
    }

    void Canvas::setMeasuredContentSize(const CanvasSize& size)
    {
        _measuredContentSize = size;
    }

    void Canvas::setVisible(const bool visible)
    {
        _visible = visible;
    }

    bool Canvas::isVisible() const
    {
        return _visible;
    }

    const std::string& Canvas::name() const
    {
        return _name;
    }

    int Canvas::pixelWidth() const
    {
        return _pixelWidth;
    }

    int Canvas::pixelHeight() const
    {
        return _pixelHeight;
    }

    const ContentCallback& Canvas::content() const
    {
        return _content;
    }

    const PlacementProvider& Canvas::placement() const
    {
        return _placement;
    }
}
