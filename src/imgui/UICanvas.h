#pragma once

#include <memory>
#include <string>

#include "../vrui/UIElement.h"
#include "../vrui/UIPanelStyle.h"
#include "ImGuiPanel.h"

namespace f4cf::imgui
{
    /**
     * Layout pixels per vrui unit - the same for every canvas, deliberately.
     *
     * A canvas states its size once, in vrui units like every other element, and its pixel
     * resolution follows from this, so the two can never disagree and squash the content.
     *
     * It is not a per-canvas knob because it is not a sharpness knob - that is setSupersample. The
     * font has one fixed pixel size, so this ratio decides how much of the panel a line of text
     * covers: raising it does not draw the same text more finely, it draws it SMALLER. Held fixed,
     * it gives every canvas the same relation between its size and its text: 48px of font is one
     * vrui unit to the em, so at the default 24px a row of text takes about 0.6 of a unit.
     *
     * The knob for how big the text looks is the font size (setFontSizePixels), which moves the
     * text without touching the layout.
     */
    inline constexpr float CANVAS_PIXELS_PER_UNIT = 48.0f;

    /**
     * What setBorder's optional arguments default to, in vrui units rather than pixels, so a canvas
     * is styled in the same terms it is laid out in. The whole chrome at once is vrui::UIPanelStyle.
     */
    inline constexpr float CANVAS_BORDER_THICKNESS_UNITS = 0.04f;
    inline constexpr float CANVAS_BORDER_CORNER_RADIUS_UNITS = 0.2f;

    /**
     * A vrui element whose content is drawn by Dear ImGui instead of scene-graph geometry.
     *
     * It takes part in vrui layout like any other element - put it in a UIContainer next to buttons
     * and it gets a slot in the row or column, at the same scale, in the same plane, facing the same
     * way. vrui supplies the placement; ImGui fills the rectangle. Nothing else about the element is
     * special: hide it, hide a parent, or detach the tree and the panel stops drawing with it.
     *
     *     auto canvas = std::make_shared<imgui::UICanvas>("BeamReadout", 8.0f, 5.0f);
     *     canvas->setContent([this] { ImGui::Text("FOV: %.1f", beamFov()); });
     *     panel->addElement(canvas);
     *
     * Size is given once, in vrui units; the pixel resolution follows from CANVAS_PIXELS_PER_UNIT
     * and the element's own size, never its containers' scale. The content is laid out and rendered
     * at scale 1 and the quad stretches it with the rest of the layout, so text, widgets and chrome
     * scale together like any other vrui element. The price is density: in a container scaled 1.6
     * the canvas has 1.6x fewer atlas pixels per world unit and reads correspondingly softer, which
     * setSupersample buys back at a memory cost.
     * A canvas too large to fit the shared atlas is scaled down to fit, keeping its aspect, and says
     * so in the log - it never silently distorts.
     *
     * Two things set it apart from its neighbours. It draws through the framework's overlay path
     * rather than the scene graph, so what hides it is the depth test described at setOccluded, not
     * the scene's own draw order. And it is not interactive: vrui's finger-collision press handling
     * does not apply, since the content is pixels rather than widgets (see the interactivity work in
     * the design docs).
     *
     * Lives under imgui/ rather than vrui/ because it is the adapter BETWEEN the two: building it
     * with vrui would make every vrui consumer depend on Dear ImGui, and it has to disappear along
     * with the rest of the layer when F4CF_WITH_IMGUI_UI is OFF. Neither subsystem depends on the
     * other; only this file depends on both, and a mod that never creates a canvas never links it.
     */
    class UICanvas : public vrui::UIElement
    {
    public:
        /**
         * @param name identifies the element in logs and is the panel's ImGui window id - unique.
         * @param width / height size in vrui units, i.e. the slot it asks the layout for.
         */
        UICanvas(const std::string& name, float width, float height);

        /**
         * The ImGui widgets to draw. Runs on the game thread inside the framework's frame - see
         * ContentCallback.
         */
        void setContent(ContentCallback content);

        /**
         * Whether the world hides the canvas when something is in front of it. On by default, so it
         * sits in the scene like the widgets around it; turn it off for a canvas that has to stay
         * readable whatever is in front of it. See imgui::Panel::setOccluded.
         */
        void setOccluded(bool occluded);

        /**
         * The whole look in one go - content colour, background, border, rounding and padding; see
         * vrui::UIPanelStyle, and vrui::F4VR_PANEL_STYLE for the house look. Replaces all of it; the
         * setters below change one part.
         *
         * Setters only record what was asked, in vrui units. The panel picks it up at the next frame
         * update, converted into the canvas's layout pixels.
         */
        void setStyle(const vrui::UIPanelStyle& style);

        /**
         * Colour for the canvas's text. Content that colours itself still wins - see
         * Panel::setTextColor.
         */
        void setTextColor(const render::Color& color);

        /**
         * The canvas background, alpha included. Transparent until asked for, so a canvas starts as
         * its content and nothing else.
         */
        void setBackgroundColor(const render::Color& color);

        /**
         * Draw a border around the canvas, in vrui units. Off until called. The corner radius rounds
         * the background too, so nothing shows past the stroke at the corners - see Panel::setBorder.
         */
        void setBorder(const render::Color& color, float thicknessUnits = CANVAS_BORDER_THICKNESS_UNITS, float cornerRadiusUnits = CANVAS_BORDER_CORNER_RADIUS_UNITS);

        void clearBorder();

        /**
         * Round the canvas corners without adding a border, in vrui units.
         */
        void setCornerRadius(float units);

        /**
         * Space between the content and the canvas edge, in vrui units, one value per side - see
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

        // Internal: keep the panel's resolution and chrome in step with what vrui laid out.
        virtual void onFrameUpdate(vrui::UIFrameUpdateContext* context) override;

    private:
        bool resolvePlacement(PanelPlacement& out) const;
        void refreshPanel();

        std::unique_ptr<Panel> _panel;

        // the look as asked for, in vrui units; converted into the panel's pixels each frame
        vrui::UIPanelStyle _style;
    };
}
