#include "UIImagePanel.h"

#include <utility>

namespace f4cf::vrui
{
    UIImagePanel::UIImagePanel(const std::string& name, const float width)
        : UIPanel(name, width, width)
    {
        setSizing(UIPanelSizing::FixedWidth);
    }

    UIImagePanel::UIImagePanel(const std::string& name, const float width, const float height)
        : UIPanel(name, width, height)
    {}

    /**
     * The content width it is given, at the image's proportions - loading the texture if this is the
     * first time anything asked, since its size is not known before. Layout runs on the game thread,
     * which loading requires. A fixed-size panel has no use for the answer, so it does not ask.
     */
    std::optional<UISize> UIImagePanel::measureContent(const float availableWidth)
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
        return UISize(availableWidth, availableWidth * static_cast<float>(_texture->height()) / static_cast<float>(_texture->width()));
    }

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

    void UIImagePanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (_texture) {
            appendImage(frame, *_texture, area.center, area.right, area.up, area.width, area.height, _fit, _tint);
        }
    }
}
