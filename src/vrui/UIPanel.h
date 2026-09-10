#pragma once

#include <string>
#include <string_view>

#include "../render/PrimitiveDraw.h"
#include "UIElement.h"
#include "UIPanelStyle.h"

namespace f4cf::vrui
{
    /**
     * Default border thickness and corner radius, in vrui units, for setBorder's optional arguments.
     */
    inline constexpr float PANEL_BORDER_THICKNESS_UNITS = 0.04f;
    inline constexpr float PANEL_BORDER_CORNER_RADIUS_UNITS = 0.2f;

    /**
     * Where a panel's content goes this frame, in world space: the rectangle its border and padding
     * leave, lying in the panel's own plane.
     */
    struct UIPanelContentArea
    {
        // the middle of the content rectangle, which is off the panel's middle when opposite sides are
        // padded differently
        RE::NiPoint3 center;

        // unit axes of the panel's plane, read with vrui's convention: +right along the width, +up
        // along the height
        RE::NiPoint3 right;
        RE::NiPoint3 up;

        // world size of the rectangle; always positive, since content is not asked to draw otherwise
        float width = 0.0f;
        float height = 0.0f;

        // world units per vrui unit, for content that states its own sizes in vrui units
        float scale = 1.0f;
    };

    /**
     * Base of the vrui elements drawn by the framework's own primitive renderer rather than by
     * scene-graph geometry: a rectangle in a vrui layout whose chrome - background, border, rounding
     * and padding - is drawn here, and whose content is drawn by the subclass into what the chrome
     * leaves. UITextPanel draws rows of text there, UIImagePanel an image.
     *
     * It starts bare - no background, no border, a little padding - and setStyle dresses it:
     * vrui::F4VR_PANEL_STYLE is the house look, and a mod can name its own.
     *
     * Every panel shares one overlay layer (two, when some are occluded and some are not), so a
     * panel costs almost nothing: no scene-graph node, no render-to-texture pass, and consecutive
     * content of one colour and texture goes out in a single draw call. Within a layer, all the
     * fills are painted first, then all the images, then all the text - so panels are not meant to
     * overlap each other.
     *
     * A panel is occluded by the world by default, so it sits in the scene like the widgets around
     * it rather than showing through walls; see setOccluded. It is not interactive.
     */
    class UIPanel : public UIElement
    {
    public:
        ~UIPanel() override;

        UIPanel(const UIPanel&) = delete;
        UIPanel& operator=(const UIPanel&) = delete;
        UIPanel(UIPanel&&) = delete;
        UIPanel& operator=(UIPanel&&) = delete;

        /**
         * The whole look in one go - content colour, background, border, rounding and padding; see
         * vrui::UIPanelStyle, and vrui::F4VR_PANEL_STYLE for the house look. Replaces all of it; the
         * setters below change one part.
         *
         * What the content colour means is the subclass's: a text panel's default row colour, while
         * an image panel ignores it, since a style's text colour would dye every image.
         */
        void setStyle(const UIPanelStyle& style);

        /**
         * The panel's background colour, alpha included. Transparent until asked for; a fully
         * transparent one (alpha 0) is not drawn at all, leaving the content floating in the world.
         *
         * It fills the panel's rectangle, rounded by the corner radius and painted under both the
         * border and the content.
         */
        void setBackgroundColor(const render::Color& color);

        /**
         * Draw a border around the panel's rectangle. Off until called; clearBorder turns it off.
         *
         * It is built from filled triangles rather than lines, because D3D11 draws every line one
         * pixel wide no matter what is asked of it - so thickness is only possible as geometry.
         *
         * The border grows INWARD from the panel's rectangle: the outer edge is exactly the slot the
         * layout gave the panel, so a bordered panel never spills onto its neighbours. The content is
         * inset by its thickness, on top of the padding, so it never runs over it - which does mean
         * a thicker border leaves less room for content.
         *
         * The corner radius shapes the BACKGROUND as well as the border, which is why it is one
         * value for both rather than a property of the border alone: the background's outline is the
         * border's outer edge, so no background can be left showing past the border at a corner.
         * setCornerRadius sets it on its own, for a rounded panel with no border.
         *
         * @param thickness in vrui units, clamped to half the shorter side.
         * @param cornerRadius in vrui units, 0 for square corners, clamped to half the shorter side.
         */
        void setBorder(const render::Color& color, float thickness = PANEL_BORDER_THICKNESS_UNITS, float cornerRadius = PANEL_BORDER_CORNER_RADIUS_UNITS);

        void clearBorder();

        /**
         * Round the panel's corners without adding a border, in vrui units. Kept separate from
         * setBorder because the radius rounds the background whether or not there is a border on top
         * of it.
         */
        void setCornerRadius(float units);

        /**
         * Space between the content and the panel's edge, in vrui units, one value per side - see
         * UIPadding for the named constructors. The border's thickness adds to it rather than eating
         * into it, so changing one never moves the other.
         */
        void setPadding(const UIPadding& padding);

        /**
         * The same padding on all four sides - UIPadding::all in one call, and the common case.
         */
        void setPadding(float units);

        /**
         * Whether the world hides the panel when something is in front of it. On by default, which is
         * what makes a panel read as part of the scene rather than pasted over it.
         *
         * Turn it off for a panel that must always be readable - a warning, or a menu you do not want
         * to lose when you turn and a wall comes between you and it. Note that occlusion also depends
         * on the framework capturing the engine's depth buffer; where it cannot, every panel draws on
         * top regardless, so this is a preference rather than a guarantee.
         */
        void setOccluded(bool occluded);

        bool isOccluded() const
        {
            return _occluded;
        }

        std::string toString() const override;

        // Internal: nothing to do during layout - the panel is drawn at frame end, once the whole
        // tree has been laid out and its transform is final.
        void onFrameUpdate(UIFrameUpdateContext*) override
        {}

        // Internal: append this panel's chrome and content to the frame being built for the render
        // thread.
        void appendTo(render::PrimitiveDraw& frame) const;

    protected:
        /**
         * @param name identifies the element in logs.
         * @param width / height size in vrui units - the panel's outer edge, border included.
         */
        UIPanel(const std::string& name, float width, float height);

        /**
         * Draw the content into `area`, on top of the chrome. Runs on the GAME thread at frame end,
         * and only while the panel is visible, attached, and has room left inside its border and
         * padding - so it may read game state freely.
         */
        virtual void appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const = 0;

        /**
         * The concrete class's name, for toString.
         */
        virtual std::string_view typeName() const = 0;

        // content colour, background, border, rounding and padding together, defaulting to a bare
        // panel
        UIPanelStyle _style;

    private:
        bool _occluded = true;
    };
}
