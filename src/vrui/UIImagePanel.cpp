#include "UIImagePanel.h"

#include <utility>

namespace f4cf::vrui
{
    UIImagePanel::UIImagePanel(const std::string& name)
        : UIPanel(name, 0.0f, 0.0f)
    {
        setSizing(UIPanelSizing::FitContent);
    }

    UIImagePanel::UIImagePanel(const std::string& name, const float width)
        : UIPanel(name, width, width)
    {
        setSizing(UIPanelSizing::FixedWidth);
    }

    UIImagePanel::UIImagePanel(const std::string& name, const float width, const float height)
        : UIPanel(name, width, height)
    {}

    void UIImagePanel::setImage(std::string path)
    {
        _texture = path.empty() ? nullptr : render::Texture::load(path);
    }

    void UIImagePanel::setImage(std::shared_ptr<render::Texture> texture)
    {
        _texture = std::move(texture);
    }

    void UIImagePanel::clearImage()
    {
        _texture.reset();
    }

    void UIImagePanel::setTint(const render::Color& tint)
    {
        _tint = tint;
    }

    void UIImagePanel::setFit(const UIImageFit fit)
    {
        _fit = fit;
    }

    /**
     * A fixed-width panel's image at that width, in its proportions; a fit-content panel's at its own
     * size in pixels over IMAGE_PANEL_PIXELS_PER_UNIT, scaled down to the width available when that is
     * less. Loads the texture if this is the first time anything asked, since its size is not known
     * before - layout runs on the game thread, which loading requires. A fixed-size panel has no use for
     * the answer, so it does not ask.
     */
    std::optional<UISize> UIImagePanel::measureContent(const float availableWidth, float)
    {
        if (getSizing() == UIPanelSizing::Fixed || !_texture) {
            return std::nullopt;
        }
        if (_texture->width() == 0 && !_texture->view()) {
            return std::nullopt;
        }
        if (_texture->width() == 0 || _texture->height() == 0) {
            return std::nullopt;
        }

        const auto imageWidth = static_cast<float>(_texture->width());
        const auto imageHeight = static_cast<float>(_texture->height());
        if (getSizing() == UIPanelSizing::FixedWidth) {
            return UISize(availableWidth, availableWidth * imageHeight / imageWidth);
        }

        const float naturalWidth = imageWidth / IMAGE_PANEL_PIXELS_PER_UNIT;
        const float scale = naturalWidth > availableWidth ? availableWidth / naturalWidth : 1.0f;
        return UISize(naturalWidth * scale, imageHeight / IMAGE_PANEL_PIXELS_PER_UNIT * scale);
    }

    void UIImagePanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (_texture) {
            appendImage(frame, *_texture, area.center, area.right, area.up, area.width, area.height, _fit, _tint);
        }
    }
}
