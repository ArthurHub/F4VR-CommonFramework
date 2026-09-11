#pragma once

#include <cstdint>
#include <optional>
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
     * How an image fills the rectangle it is given.
     */
    enum class UIImageFit : std::uint8_t
    {
        // as large as fits in the image's own proportions, centred, leaving the rest of the area empty
        Contain,
        // stretched to fill the area exactly, whatever that does to its proportions
        Stretch,
    };

    /**
     * Which of a panel's dimensions are the caller's and which follow its content. The content is
     * measured during layout, so the size a container lays the panel out with is this frame's.
     */
    enum class UIPanelSizing : std::uint8_t
    {
        // width and height as given; content that does not fit is clipped
        Fixed,
        // width as given, height grown or shrunk to hold the content at that width
        FixedWidth,
        // both follow the content, which wraps only past the max width when one is set
        FitContent,
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
     *
     * Its size is fixed unless the subclass measures its content (see UIPanelSizing): then, during
     * layout, the dimensions that follow the content are set to it plus the border and padding, so
     * the container around the panel lays it out at the size it is about to be drawn at.
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

        UIPanelSizing getSizing() const
        {
            return _sizing;
        }

        std::string toString() const override;

        // Internal: size the panel to its content before its container lays it out.
        void onLayoutUpdate(UIFrameUpdateContext* context) override;

        // Internal: nothing to do per frame - the panel is drawn at frame end, once the whole tree has
        // been laid out and its transform is final.
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
         * Size of the content in vrui units, border and padding excluded, when laid out no wider than
         * `availableWidth` - infinite for a FitContent panel with no max width. Runs on the GAME thread
         * during layout, every frame the panel is visible and whatever its sizing, so it is also where a
         * subclass prepares what appendContent draws.
         *
         * The default, and nullopt from an override, leaves the size as it is: a panel whose content
         * has no size to report yet, like an image that has not loaded.
         */
        virtual std::optional<UISize> measureContent(float availableWidth);

        /**
         * Which dimensions follow the content. Protected, so a subclass exposes only the modes its
         * content can answer - an image has proportions but no natural size.
         */
        void setSizing(UIPanelSizing sizing);

        /**
         * The widest a FitContent panel grows before its content has to wrap, in vrui units, border
         * and padding included. 0, the default, lets it grow without limit.
         */
        void setMaxWidth(float units);

        /**
         * The concrete class's name, for toString.
         */
        virtual std::string_view typeName() const = 0;

        /**
         * The look to draw with this frame: the panel's style as set, unless a subclass whose look
         * follows its state - a disabled button - adjusts a copy of it here, so the chrome drawn by
         * the base and the content drawn by the subclass agree.
         */
        virtual UIPanelStyle resolveStyle() const
        {
            return _style;
        }

        /**
         * Draw a texture into a rectangle of the panel's plane, centred on `center` and fitted by
         * `fit`. Draws nothing while the texture has not loaded. GAME thread only, since the first
         * call loads it.
         */
        static void appendImage(render::PrimitiveDraw& frame, render::Texture& texture, const RE::NiPoint3& center, const RE::NiPoint3& right, const RE::NiPoint3& up, float width,
            float height, UIImageFit fit, const render::Color& tint);

        // content colour, background, border, rounding and padding together, defaulting to a bare
        // panel
        UIPanelStyle _style;

    private:
        bool _occluded = true;
        UIPanelSizing _sizing = UIPanelSizing::Fixed;
        float _maxWidthUnits = 0.0f;
    };
}
