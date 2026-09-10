#include "UIImagePanel.h"

#include <utility>

namespace f4cf::vrui
{
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

    void UIImagePanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (_texture) {
            appendImage(frame, *_texture, area.center, area.right, area.up, area.width, area.height, _fit, _tint);
        }
    }
}
