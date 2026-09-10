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

    /**
     * Fit the image into the content area and add it as one textured quad.
     *
     * Contain needs the image's proportions, which are only known once the texture has loaded - but
     * nothing is drawn before then anyway, so the fit never has to guess. A texture that reports no
     * size falls back to filling the area.
     */
    void UIImagePanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (!_texture) {
            return;
        }
        render::TextureView view = _texture->view(); // loads on first use, so it must run game-side
        if (!view) {
            return;
        }

        float halfW = area.width * 0.5f;
        float halfH = area.height * 0.5f;
        if (_fit == UIImageFit::Contain && _texture->width() > 0 && _texture->height() > 0) {
            const float imageAspect = static_cast<float>(_texture->width()) / static_cast<float>(_texture->height());
            if (area.width > area.height * imageAspect) {
                halfW = halfH * imageAspect; // the area is wider than the image: empty bands left and right
            } else {
                halfH = halfW / imageAspect; // taller: empty bands above and below
            }
        }

        const auto at = [&](const float u, const float v) {
            return area.center + area.right * u + area.up * v;
        };
        frame.addImage(std::move(view), _texture->isSRGB(), at(-halfW, halfH), at(halfW, halfH), at(halfW, -halfH), at(-halfW, -halfH), _tint);
    }
}
