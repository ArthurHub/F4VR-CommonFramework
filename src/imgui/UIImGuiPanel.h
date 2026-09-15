#pragma once

#include <memory>
#include <string>

#include "../vrui/UIElement.h"
#include "../vrui/UIPanel.h"
#include "../vrui/UIPanelStyle.h"
#include "ImGuiCanvas.h"

namespace f4cf::imgui
{
    /**
     * Canvas pixels per vrui unit - the same for every UIImGuiPanel, deliberately.
     *
     * A panel states its size once, in vrui units like every other element, and its canvas's pixel
     * resolution follows from this, so the two can never disagree and squash the content.
     *
     * It is not a per-panel knob because it is not a sharpness knob - that is setSupersample. The
     * font has one fixed pixel size, so this ratio decides how much of the panel a line of text
     * covers: raising it does not draw the same text more finely, it draws it SMALLER. Held fixed,
     * it gives every panel the same relation between its size and its text: 48px of font is one
     * vrui unit to the em, so at the default 24px a row of text takes about 0.6 of a unit.
     *
     * The knob for how big the text looks is the font size (setFontSizePixels), which moves the
     * text without touching the layout.
     */
    inline constexpr float CANVAS_PIXELS_PER_UNIT = 48.0f;

    /**
     * A vrui element whose content is drawn by Dear ImGui instead of scene-graph geometry.
     *
     * It takes part in vrui layout like any other element - put it in a UIContainer next to buttons
     * and it gets a slot in the row or column, at the same scale, in the same plane, facing the same
     * way. vrui supplies the placement; an imgui::Canvas fills the rectangle. Nothing else about the
     * element is special: hide it, hide a parent, or detach the tree and the canvas stops drawing
     * with it.
     *
     *     auto readout = std::make_shared<imgui::UIImGuiPanel>("BeamReadout", 8.0f);
     *     readout->setContent([this] { ImGui::Text("FOV: %.1f", beamFov()); });
     *     panel->addElement(readout);
     *
     * Size is in vrui units, and which constructor builds the panel picks how it is sized, as for a
     * vrui::UITextPanel (see vrui::UIPanelSizing): with no size it fits its content, with a width it
     * grows to the height its content needs at that width, and with both it stays as given.
     *
     * The pixel resolution follows from CANVAS_PIXELS_PER_UNIT and the element's own size, never its
     * containers' scale. The content is laid out and rendered at scale 1 and the quad stretches it with
     * the rest of the layout, so text, widgets and chrome scale together like any other vrui element.
     * The price is density: in a container scaled 1.6 the panel has 1.6x fewer atlas pixels per world
     * unit and reads correspondingly softer, which setSupersample buys back at a memory cost.
     * A panel too large to fit the shared atlas is scaled down to fit, keeping its aspect, and says
     * so in the log - it never silently distorts.
     *
     * Three things set it apart from its neighbours. It draws through the framework's overlay path
     * rather than the scene graph, so what hides it is the depth test described at setOccluded, not
     * the scene's own draw order. It is not interactive: vrui's finger-collision press handling
     * does not apply, since the content is pixels rather than widgets (see the interactivity work in
     * the design docs). And it is not a vrui::UIPanel, whatever the name suggests: it takes the same
     * vrui::UIPanelStyle and sizing modes, but ImGui draws its chrome and lays out its content.
     *
     * Lives under imgui/ rather than vrui/ because it is the adapter BETWEEN the two: building it
     * with vrui would make every vrui consumer depend on Dear ImGui, and it has to disappear along
     * with the rest of the layer when F4CF_WITH_IMGUI_UI is OFF. Neither subsystem depends on the
     * other; only this file depends on both, and a mod that never creates a panel never links it.
     */
    class UIImGuiPanel : public vrui::UIElement
    {
    public:
        /**
         * A panel as wide and as tall as its content, border and padding included.
         * @param name identifies the element in logs and is the canvas's ImGui window id - unique.
         */
        explicit UIImGuiPanel(const std::string& name);

        /**
         * A panel of fixed width whose height follows its content laid out at that width.
         * @param name identifies the element in logs and is the canvas's ImGui window id - unique.
         * @param width size in vrui units, border and padding included.
         */
        UIImGuiPanel(const std::string& name, float width);

        /**
         * A panel of fixed size; content that does not fit is clipped.
         * @param name identifies the element in logs and is the canvas's ImGui window id - unique.
         * @param width / height size in vrui units, i.e. the slot it asks the layout for.
         */
        UIImGuiPanel(const std::string& name, float width, float height);

        /**
         * The ImGui widgets to draw. Runs on the game thread inside the framework's frame - see
         * ContentCallback.
         */
        void setContent(ContentCallback content);

        /**
         * Which dimensions follow the content - see vrui::UIPanelSizing, and the constructors, which
         * pick the common ones.
         *
         * ImGui content can only be measured by drawing it, and it is drawn at frame end, after vrui has
         * laid the frame out - so a panel that follows its content is laid out at the size the content
         * took up the frame before. A change in the content reaches the layout one frame late, clipped
         * or with room to spare for that frame; the first frame, with nothing measured yet, lays the
         * content out without showing it.
         *
         * A dimension that follows the content lays it out in as much room as it may grow to - the max
         * width, or the whole atlas (MAX_CANVAS_PIXEL_SIZE, about 21 units) - and never grows past it:
         * content past that is clipped. Wrapped text (ImGui::TextWrapped) wraps at that room, so with a
         * max width it wraps only past it. Content that fills whatever room it is given - a full-width
         * progress bar, right-aligned text - sizes the panel out to that room; give it a width of its own.
         */
        void setSizing(vrui::UIPanelSizing sizing);

        vrui::UIPanelSizing getSizing() const
        {
            return _sizing;
        }

        /**
         * The widest a panel whose width follows its content (FitContent, FixedHeight) grows, in vrui
         * units, border and padding included - the width wrapped text wraps at. 0, the default, lets it
         * grow to the atlas.
         */
        void setMaxWidth(float units);

        /**
         * Whether the world hides the panel when something is in front of it. On by default, so it
         * sits in the scene like the widgets around it; turn it off for a panel that has to stay
         * readable whatever is in front of it. See imgui::Canvas::setOccluded.
         */
        void setOccluded(bool occluded);

        /**
         * The whole look in one go - content colour, background, border, rounding and padding; see
         * vrui::UIPanelStyle, and vrui::F4VR_PANEL_STYLE for the house look. Replaces all of it; the
         * setters below change one part.
         *
         * Setters only record what was asked, in vrui units. The canvas picks it up at the next frame
         * update, converted into its layout pixels.
         */
        void setStyle(const vrui::UIPanelStyle& style);

        /**
         * Colour for the panel's text. Content that colours itself still wins - see
         * Canvas::setTextColor.
         */
        void setTextColor(const render::Color& color);

        /**
         * The panel background, alpha included. Transparent until asked for, so a panel starts as
         * its content and nothing else.
         */
        void setBackgroundColor(const render::Color& color);

        /**
         * Draw a border around the panel, in vrui units, with the same defaults as a vrui::UIPanel's.
         * Off until called. The corner radius rounds the background too, so nothing shows past the
         * stroke at the corners - see Canvas::setBorder.
         */
        void setBorder(const render::Color& color, float thicknessUnits = vrui::PANEL_BORDER_THICKNESS_UNITS, float cornerRadiusUnits = vrui::PANEL_BORDER_CORNER_RADIUS_UNITS);

        void clearBorder();

        /**
         * Round the panel corners without adding a border, in vrui units.
         */
        void setCornerRadius(float units);

        /**
         * Space between the content and the panel edge, in vrui units, one value per side - see
         * vrui::UIPadding for the named constructors. The border adds to it rather than eating in.
         *
         * This is padding to ImGui's own layout box, so a first row of TEXT sits a little lower than
         * the top padding suggests: a font reserves ascender space above its capitals, and ImGui
         * lays out the line box, not the ink. Trimming the top side is how to even that up by eye.
         */
        void setPadding(const vrui::UIPadding& padding);

        /**
         * The same padding on all four sides - vrui::UIPadding::all in one call.
         */
        void setPadding(float units);

        virtual std::string toString() const override;

        // Internal: size the panel to what its content took up before its container lays it out.
        virtual void onLayoutUpdate(vrui::UIFrameUpdateContext* context) override;

        // Internal: keep the canvas's resolution and chrome in step with what vrui laid out.
        virtual void onFrameUpdate(vrui::UIFrameUpdateContext* context) override;

    protected:
        void writeDevLayoutFields(std::string& line) const override;
        void readDevLayoutFields(const DevLayoutFields& fields) override;

    private:
        bool resolvePlacement(CanvasPlacement& out) const;
        void refreshCanvas();
        CanvasSize contentRoomPixels(float pixelsPerUnit) const;

        std::unique_ptr<Canvas> _canvas;

        // the look as asked for, in vrui units; converted into the canvas's pixels each frame
        vrui::UIPanelStyle _style;

        vrui::UIPanelSizing _sizing = vrui::UIPanelSizing::Fixed;
        float _maxWidthUnits = 0.0f;
    };
}
