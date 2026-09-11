#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "../render/Texture.h"
#include "UIPanel.h"
#include "UIPressable.h"

namespace f4cf::vrui
{
    /**
     * Largest height a line of a button's text is drawn at beside an image, in vrui units - the height
     * of a capital letter. A line too wide for the button at this size is shrunk until it fits;
     * setTextHeight overrides it.
     */
    inline constexpr float BUTTON_PANEL_TEXT_HEIGHT_UNITS = 0.24f;

    /**
     * The same, for a button with no image: its text has the whole height to itself, so it can be
     * drawn larger. setTextOnlyHeight overrides it.
     */
    inline constexpr float BUTTON_PANEL_TEXT_ONLY_HEIGHT_UNITS = 0.36f;

    /**
     * Space between the image and a line of text, in vrui units.
     */
    inline constexpr float BUTTON_PANEL_CONTENT_GAP_UNITS = 0.08f;

    /**
     * How much of a disabled button's border, text and image still shows, as a multiplier on their
     * alpha. The background is left alone, so a disabled button still reads as a button.
     */
    inline constexpr float BUTTON_PANEL_DISABLED_ALPHA = 0.35f;

    /**
     * A pressable vrui button drawn by the framework's own primitive renderer - text, an image, or both,
     * composed at runtime - rather than baked into a NIF mesh and its texture.
     *
     *     auto gobo = std::make_shared<vrui::UIButtonPanel>("SwitchGobo");
     *     gobo->setTopText("SWITCH");
     *     gobo->setImage("Data\\Textures\\MyMod\\gobo-icon.dds");
     *     gobo->setBottomText("GOBO");
     *     gobo->setOnPressHandler([this](vrui::UIButtonPanel*) { switchGobo(); });
     *     row->addElement(gobo);
     *
     * The content is a column: an optional top line, then an optional image or an optional middle line,
     * then an optional bottom line. Any of them can be left out - up to three lines of text, an image
     * only, or a line above and/or below an image. The middle line and the image share the one slot, so
     * the image wins: the middle line is not drawn while the button has an image.
     *
     * With an image, the top line's capitals start at the top edge and the bottom line's end at the
     * bottom edge, so both sit the same distance from the border and from the image. Without one, the
     * lines are spread with equal space between the border and the first and last lines, padding
     * included, and half that space between each pair, so a single line is centred. When the button is
     * too short for that, the gaps between lines give way first. Everything is measured on the capitals, since labels
     * mostly are; descenders hang below their line.
     *
     * Each line is horizontally centred and sized on its own: as large as the text height allows while
     * it still fits the button's width, so a long line shrinks without taking the other one with it.
     * Text is shrunk to fit rather than clipped, so a long label comes out smaller, never cut. Only if
     * the lines would overfill the height do both shrink, by the same factor.
     *
     * It presses the way UIButton does: pushed in by the interaction bone past a short travel it fires
     * once, and fires again only after the bone has backed away in front of it. While disabled it is
     * dimmed and cannot be pressed. It starts in vrui::F4VR_BUTTON_STYLE; the style's content colour is
     * the text colour, and the image is tinted by setImageTint alone.
     *
     * Its size is fixed unless setFitWidth is on: then its height stays as given and its width follows
     * the content, so a long label widens the button instead of shrinking.
     */
    class UIButtonPanel : public UIPanel, public UIPressable
    {
    public:
        /**
         * @param name identifies the element in logs.
         * @param width / height size in vrui units, border and padding included - 2x2 matches the NIF
         *        buttons.
         */
        explicit UIButtonPanel(const std::string& name, float width = 2.0f, float height = 2.0f);

        /**
         * The line above the image, or the first line when there is no image. Empty leaves it out.
         */
        void setTopText(std::string text);

        /**
         * The line between the top and bottom lines, drawn only when the button has no image - the image
         * takes its place. Empty leaves it out.
         */
        void setMiddleText(std::string text);

        /**
         * The line below the image, or the last line when there is no image. Empty leaves it out.
         */
        void setBottomText(std::string text);

        /**
         * The image between the lines, by the path the engine's texture loader takes - see
         * render::Texture::load. An empty path leaves it out.
         */
        void setImage(std::string path);

        /**
         * The image between the lines, as a texture shared with other elements or held by the mod.
         */
        void setImage(std::shared_ptr<render::Texture> texture);

        /**
         * Leave the image out.
         */
        void clearImage();

        /**
         * Multiplies the image's colours and alpha; white, the default, leaves it as it is.
         */
        void setImageTint(const render::Color& tint);

        /**
         * The largest height a line is drawn at beside an image, in vrui units; it is still shrunk to
         * fit.
         */
        void setTextHeight(float units);

        /**
         * The largest height a line is drawn at when the button has no image, in vrui units; it is still
         * shrunk to fit.
         */
        void setTextOnlyHeight(float units);

        /**
         * Size the button's width to its content at its height, instead of shrinking the text to the width
         * it was given. Each line then keeps its full text height, and the image its proportions in the
         * height the lines leave. The button never gets narrower than it is tall, so a short label still
         * makes a square button; setMaxWidth caps it, past which the lines shrink to fit as before.
         */
        void setFitWidth(bool fitWidth);

        using UIPanel::setMaxWidth;

        /**
         * Called on the game thread each time the button is pressed. A button with no handler is not
         * pressable.
         */
        void setOnPressHandler(std::function<void(UIButtonPanel*)> handler);

        bool isDisabled() const override
        {
            return _disabled;
        }

        /**
         * A disabled button cannot be pressed and is drawn dimmed.
         */
        void setDisabled(bool disabled) override;

        // Internal: press detection, run by the UI manager each frame.
        void onFrameUpdate(UIFrameUpdateContext* context) override;

    protected:
        /**
         * Whether a push fires the button this frame. A subclass that fires through a handler of its own
         * decides for itself.
         */
        virtual bool isPressable() const
        {
            return !_disabled && _onPressHandler != nullptr;
        }

        std::optional<UISize> measureContent(float availableWidth, float availableHeight) override;
        std::string stateFlags() const override;
        void writeDevLayoutFields(std::string& line) const override;
        void readDevLayoutFields(const DevLayoutFields& fields) override;
        RE::NiTransform calculateTransform() const override;
        void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;
        UIPanelStyle resolveStyle() const override;
        void appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const override;

        std::string_view typeName() const override
        {
            return "UIButtonPanel";
        }

    private:
        void handlePress(UIFrameUpdateContext* context);

        std::string _topText;
        std::string _middleText;
        std::string _bottomText;
        std::shared_ptr<render::Texture> _texture;
        render::Color _imageTint = render::colors::White;
        float _textHeightUnits = BUTTON_PANEL_TEXT_HEIGHT_UNITS;
        float _textOnlyHeightUnits = BUTTON_PANEL_TEXT_ONLY_HEIGHT_UNITS;
        std::function<void(UIButtonPanel*)> _onPressHandler;
        bool _disabled = false;

        // press state, kept the way UIWidget keeps it: whether this push already fired, how far the
        // button is pushed in, and whether the bone is near enough for the hand to point
        bool _pressEventFired = false;
        float _pressYOffset = 0.0f;
        bool _wasPressableCloseToInteraction = false;
    };
}
