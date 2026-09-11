#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "../render/Texture.h"
#include "UIPanel.h"

namespace f4cf::vrui
{
    /**
     * A vrui panel that draws an image with the framework's own primitive renderer.
     *
     *     auto icon = std::make_shared<vrui::UIImagePanel>("BeamIcon", 4.0f, 4.0f);
     *     icon->setImage("Data\\Textures\\MyMod\\beam-icon.dds");
     *     icon->setStyle(vrui::F4VR_PANEL_STYLE);
     *     panel->addElement(icon);
     *
     * The image is loaded by the engine's texture loader (see render::Texture), so its path resolves
     * the way a NIF's texture path does - loose files or BA2 archives - and it draws in its own
     * colours and alpha, multiplied by the tint. Until the texture has loaded, and for good if it
     * cannot, the panel shows its chrome alone.
     *
     * The chrome is UIPanel's and the image sits inside its border and padding. The style's content
     * colour does NOT tint the image - a house style's text colour would dye every image - so tinting
     * is setTint's alone.
     *
     * Images that use different textures cannot share a draw call, so every distinct texture on
     * screen costs one.
     *
     * Built with a width alone, the panel takes its height from the image's proportions, so the image
     * fills its content area with no empty bands and the fit makes no difference. The proportions are
     * only known once the texture has loaded, which the panel asks for during layout; until then, and
     * for good if the image cannot load, the panel is square. There is no fit-content sizing, since an
     * image has proportions but no natural size in vrui units.
     */
    class UIImagePanel : public UIPanel
    {
    public:
        /**
         * A panel of fixed width whose height follows the image's proportions.
         * @param name identifies the element in logs.
         * @param width size in vrui units, border and padding included.
         */
        UIImagePanel(const std::string& name, float width);

        /**
         * A panel of fixed size.
         * @param name identifies the element in logs.
         * @param width / height size in vrui units, border and padding included.
         */
        UIImagePanel(const std::string& name, float width, float height);

        /**
         * The image to show, by the path the engine's texture loader takes - "Data\\Textures\\..."
         * for a mod's own file. It is loaded the first time the panel draws, and panels showing the
         * same path share one texture (see render::Texture::load). A path the engine cannot find is
         * logged once and shows nothing.
         */
        void setImage(std::string path);

        /**
         * The image to show, as a texture shared with other panels or held by the mod.
         */
        void setImage(std::shared_ptr<render::Texture> texture);

        /**
         * Show no image, leaving the chrome as it is.
         */
        void clearImage();

        /**
         * Multiplies the image's colours and alpha: white, the default, leaves the image as it is,
         * and a lower alpha fades it.
         */
        void setTint(const render::Color& tint);

        /**
         * How the image fills the content area; Contain by default.
         */
        void setFit(UIImageFit fit);

    protected:
        std::optional<UISize> measureContent(float availableWidth) override;
        void appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const override;

        std::string_view typeName() const override
        {
            return "UIImagePanel";
        }

    private:
        std::shared_ptr<render::Texture> _texture;
        render::Color _tint = render::colors::White;
        UIImageFit _fit = UIImageFit::Contain;
    };
}
