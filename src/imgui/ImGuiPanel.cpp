#include "ImGuiPanel.h"

#include <algorithm>

#include "../ModBase.h"
#include "ImGuiLayer.h"

namespace f4cf::imgui
{
    namespace
    {
        // Below ~16 glyphs lose their shape; above ~128 the atlas grows for nothing.
        constexpr float MIN_FONT_SIZE = 16.0f;
        constexpr float MAX_FONT_SIZE = 128.0f;

        float s_fontSizePixels = 48.0f;
    }

    void setFontSizePixels(const float sizePixels)
    {
        s_fontSizePixels = std::clamp(sizePixels, MIN_FONT_SIZE, MAX_FONT_SIZE);
    }

    namespace internal
    {
        float fontSizePixels()
        {
            return s_fontSizePixels;
        }
    }

    Panel::Panel(std::string name, const int pixelWidth, const int pixelHeight)
        : _name(std::move(name)),
          _pixelWidth(std::clamp(pixelWidth, 1, MAX_PANEL_PIXEL_SIZE)),
          _pixelHeight(std::clamp(pixelHeight, 1, MAX_PANEL_PIXEL_SIZE))
    {
        // Registering the pump here, on the first panel ever built, is what keeps ImGui out of mods
        // that never draw one: ModBase names no symbol in this subsystem, so with static-library
        // semantics none of its object files - and therefore none of ImGui - is pulled into such a
        // mod at all. Once a panel exists the framework pumps it, with nothing for the mod to
        // remember to call.
        static const bool pumpRegistered = [] {
            registerFrameEndCallback(&internal::onFrameEnd);
            return true;
        }();
        static_cast<void>(pumpRegistered);

        internal::registerPanel(this);
    }

    Panel::~Panel()
    {
        internal::unregisterPanel(this);
    }

    void Panel::setContent(ContentCallback content)
    {
        _content = std::move(content);
    }

    void Panel::setPlacement(PlacementProvider placement)
    {
        _placement = std::move(placement);
    }

    void Panel::setPixelSize(const int pixelWidth, const int pixelHeight)
    {
        _pixelWidth = std::clamp(pixelWidth, 1, MAX_PANEL_PIXEL_SIZE);
        _pixelHeight = std::clamp(pixelHeight, 1, MAX_PANEL_PIXEL_SIZE);
    }

    void Panel::setOccluded(const bool occluded)
    {
        _occluded = occluded;
    }

    void Panel::setBackgroundColor(const render::Color& color)
    {
        _background = color;
    }

    void Panel::setBorder(const render::Color& color, const float thicknessPixels, const float cornerRadiusPixels)
    {
        _borderColor = color;
        _borderThickness = (std::max)(0.0f, thicknessPixels);
        _cornerRadius = (std::max)(0.0f, cornerRadiusPixels);
    }

    void Panel::clearBorder()
    {
        // the rounding is deliberately kept: it shapes the background, which is still there
        _borderThickness = 0.0f;
    }

    void Panel::setCornerRadius(const float pixels)
    {
        _cornerRadius = (std::max)(0.0f, pixels);
    }

    void Panel::setPadding(const float pixels)
    {
        _padding = (std::max)(0.0f, pixels);
    }

    void Panel::setVisible(const bool visible)
    {
        _visible = visible;
    }

    bool Panel::isVisible() const
    {
        return _visible;
    }

    const std::string& Panel::name() const
    {
        return _name;
    }

    int Panel::pixelWidth() const
    {
        return _pixelWidth;
    }

    int Panel::pixelHeight() const
    {
        return _pixelHeight;
    }

    const ContentCallback& Panel::content() const
    {
        return _content;
    }

    const PlacementProvider& Panel::placement() const
    {
        return _placement;
    }
}
