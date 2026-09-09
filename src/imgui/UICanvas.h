#pragma once

#include <memory>
#include <string>

#include "../vrui/UIElement.h"
#include "ImGuiPanel.h"

namespace f4cf::imgui
{
    /**
     * Atlas pixels per vrui unit - the same for every canvas, deliberately.
     *
     * A canvas states its size once, in vrui units like every other element, and its pixel
     * resolution follows from this, so the two can never disagree and squash the content.
     *
     * It is not a per-canvas knob because it is not really a sharpness knob. The font is rasterized
     * at one fixed pixel size, so this ratio also decides how much of the panel a line of text
     * covers: raising it does not draw the same text more finely, it draws it SMALLER. Held fixed,
     * it gives every canvas the same relation between world size and text size - one vrui unit is
     * about one line of text, so a canvas 6 units tall fits roughly 6 rows, in any container.
     *
     * The knob for how big the text looks is the font size (setFontSizePixels), which moves the
     * text without touching the layout.
     */
    inline constexpr float CANVAS_PIXELS_PER_UNIT = 48.0f;

    /**
     * What a canvas's border and padding default to, in vrui units rather than pixels, so a canvas is
     * styled in the same terms it is laid out in.
     *
     * The border values match vrui::UITextPanel's, so the two kinds of panel sit beside each other
     * without one looking heavier than the other.
     */
    inline constexpr float CANVAS_BORDER_THICKNESS_UNITS = 0.08f;
    inline constexpr float CANVAS_BORDER_CORNER_RADIUS_UNITS = 0.4f;
    inline constexpr float CANVAS_PADDING_UNITS = 0.2f;

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
     * and keeps following it - resize the element or rescale a parent container and the resolution
     * tracks it, so the content stays the same physical size and sharpness instead of stretching.
     * A canvas too large to fit the shared atlas is scaled down to fit, keeping its aspect, and says
     * so in the log - it never silently distorts.
     *
     * Two things it does not inherit from its neighbours. It draws through the framework's overlay
     * path, so it is always ON TOP - a sibling widget physically in front of it will not occlude it,
     * which does not show in a row or column layout but will in nested or overlapping containers.
     * And it is not interactive: vrui's finger-collision press handling does not apply, since the
     * content is pixels rather than widgets (see the interactivity work in the design docs).
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
         * The canvas background, alpha included. Half-transparent by default so it reads like the
         * vrui widgets beside it; see imgui::PANEL_BACKGROUND.
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
         * Space between the content and the canvas edge, in vrui units. The border adds to it.
         */
        void setPadding(float units);

        virtual std::string toString() const override;

        // Internal: keep the panel's resolution in step with the size vrui laid out for it.
        virtual void onFrameUpdate(vrui::UIFrameUpdateContext* context) override;

    private:
        bool resolvePlacement(PanelPlacement& out) const;
        void refreshPixelSize();

        std::unique_ptr<Panel> _panel;
    };
}
