#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../render/PrimitiveDraw.h"
#include "UIElement.h"
#include "UIPanelStyle.h"

namespace f4cf::vrui
{
    /**
     * One row of the panel: its text, and optionally a color of its own.
     *
     * A row that names no color takes the panel's (setColor), so a panel that is mostly one color
     * says so once and only the rows that stand out name their own. Color is what splits a draw
     * call, not the row, so rows sharing a color cost nothing extra - but the runs are emitted in
     * order, so alternating colors down a long panel does add draws.
     */
    struct TextRow
    {
        std::string text;
        std::optional<render::Color> color;
    };

    /**
     * Fills in the rows the panel shows this frame. Runs on the GAME thread at frame end, after the
     * tree has been laid out, so it may read game state freely. Append to the vector; it arrives
     * empty, and an empty result simply draws nothing.
     */
    using TextRowsCallback = std::function<void(std::vector<TextRow>&)>;

    /**
     * Height of a capital letter, in vrui units: the text size, and nothing else. Lowercase
     * ascenders reach a little above it, and descenders about a third of it below the baseline.
     *
     * Deliberately separate from the spacing below, so opening the rows up does not shrink the text
     * and resizing the text does not change how tight the rows are. setTextHeight overrides it.
     */
    inline constexpr float TEXT_PANEL_TEXT_HEIGHT_UNITS = 0.25f;

    /**
     * Distance from one baseline to the next, as a multiple of the text height. At 1.0 a row's
     * capitals start right where the row above sits, so its descenders run into them; about 1.4
     * clears them. setLineSpacing overrides it.
     *
     * The extra space goes BETWEEN rows only. The first row's capitals start on the top edge of the
     * text area, so opening the rows up never widens the gap to the top border - that gap is the
     * padding, and matches the gap to the left border, which is measured to the first letter's ink.
     */
    inline constexpr float TEXT_PANEL_LINE_SPACING = 1.6f;

    /**
     * Default border thickness and corner radius, in vrui units, for setBorder's optional arguments.
     */
    inline constexpr float TEXT_PANEL_BORDER_THICKNESS_UNITS = 0.04f;
    inline constexpr float TEXT_PANEL_BORDER_CORNER_RADIUS_UNITS = 0.2f;

    /**
     * A vrui element that draws rows of text with the framework's own primitive renderer.
     *
     * It is the low-fidelity sibling of imgui::UICanvas: the same idea - a rectangle in a vrui
     * layout whose inside is drawn by the overlay rather than by scene-graph geometry - but with no
     * Dear ImGui behind it. No atlas texture, no render-to-texture pass, no ImGui dependency at all,
     * so it is available even when F4CF_WITH_IMGUI_UI is OFF.
     *
     *     auto readout = std::make_shared<vrui::UITextPanel>("BeamReadout", 12.0f, 4.0f);
     *     readout->setContent([this](std::vector<vrui::TextRow>& rows) {
     *         rows.emplace_back(std::format("FOV {:.1f}", beamFov()));
     *         rows.emplace_back("OVERHEAT", render::colors::Red);
     *     });
     *     panel->addElement(readout);
     *
     * The text is the framework's font, Roboto Medium, kerned and drawn from a distance field so it
     * stays sharp at any size and viewing distance. What you give up against UICanvas is layout: one
     * size and one color per row, no wrapping and no scrolling. Rows that do not fit are clipped
     * rather than reflowed - past the bottom edge they are dropped, past the right edge they are cut
     * after the last character that fits - because an overflowing row would otherwise cover the
     * sibling widgets. Text, in other words, and nothing else: no widgets, no tables, no input.
     *
     * What you gain is that it costs almost nothing. Every panel shares one overlay layer, and rows
     * of one color collapse into a single draw call.
     *
     * It starts bare - no background, no border, a little padding - and setStyle dresses it:
     * vrui::F4VR_PANEL_STYLE is the house look, and a mod can name its own. It is occluded by the
     * world by default, so it sits in the scene like the widgets around it rather than showing
     * through walls; see setOccluded for when to turn that off, and for what happens on a build
     * where the scene depth cannot be captured. It is not interactive.
     */
    class UITextPanel : public UIElement
    {
    public:
        /**
         * @param name identifies the element in logs.
         * @param width / height size in vrui units. What fits is measured against the area left
         *        after the border and padding are taken off each side: as many rows as that height
         *        holds at the row pitch (text height x line spacing), with room under the last for its
         *        descenders, and in each row as much text as that width holds.
         */
        UITextPanel(const std::string& name, float width, float height);
        ~UITextPanel() override;

        UITextPanel(const UITextPanel&) = delete;
        UITextPanel& operator=(const UITextPanel&) = delete;
        UITextPanel(UITextPanel&&) = delete;
        UITextPanel& operator=(UITextPanel&&) = delete;

        /**
         * The rows to draw. See TextRowsCallback - it runs on the game thread each frame.
         */
        void setContent(TextRowsCallback content);

        /**
         * Color for rows that do not name one of their own. White by default.
         */
        void setColor(const render::Color& color);

        /**
         * Which edge the rows are aligned to. Left (the default) starts them at the left edge,
         * Right ends them at the right edge, Center straddles the middle.
         */
        void setAlign(render::TextAlign align);

        /**
         * Height of a capital letter in vrui units - the text size. Rows stay as tightly or loosely
         * spaced as they were, because the spacing is a multiple of this.
         */
        void setTextHeight(float units);

        /**
         * Distance from one baseline to the next, as a multiple of the text height. Below 1.0 a row's
         * capitals would overlap the row above's, so it is clamped there.
         */
        void setLineSpacing(float multiplier);

        /**
         * The whole look in one go - row colour, background, border, rounding and padding; see
         * vrui::UIPanelStyle, and vrui::F4VR_PANEL_STYLE for the house look. Replaces all of it; the
         * setters below change one part.
         */
        void setStyle(const UIPanelStyle& style);

        /**
         * The panel's background colour, alpha included. Transparent until asked for; a fully
         * transparent one (alpha 0) is not drawn at all, leaving the rows floating in the world.
         *
         * It fills the panel's rectangle, rounded by the corner radius and painted under both the
         * border and the rows.
         */
        void setBackgroundColor(const render::Color& color);

        /**
         * Draw a border around the panel's rectangle. Off until called; clearBorder turns it off.
         *
         * It is built from filled triangles rather than lines, because D3D11 draws every line one
         * pixel wide no matter what is asked of it - so thickness is only possible as geometry.
         *
         * The border grows INWARD from the panel's rectangle: the outer edge is exactly the slot the
         * layout gave the panel, so a bordered panel never spills onto its neighbours. The rows are
         * inset by its thickness, on top of the padding, so they never run over it - which does mean
         * a thicker border leaves less room for text.
         *
         * The corner radius shapes the BACKGROUND as well as the border, which is why it is one
         * value for both rather than a property of the border alone: the background's outline is the
         * border's outer edge, so no background can be left showing past the border at a corner.
         * setCornerRadius sets it on its own, for a rounded panel with no border.
         *
         * @param thickness in vrui units, clamped to half the shorter side.
         * @param cornerRadius in vrui units, 0 for square corners, clamped to half the shorter side.
         */
        void setBorder(const render::Color& color, float thickness = TEXT_PANEL_BORDER_THICKNESS_UNITS, float cornerRadius = TEXT_PANEL_BORDER_CORNER_RADIUS_UNITS);

        void clearBorder();

        /**
         * Round the panel's corners without adding a border, in vrui units. Kept separate from
         * setBorder because the radius rounds the background whether or not there is a border on top
         * of it.
         */
        void setCornerRadius(float units);

        /**
         * Space between the rows and the panel's edge, in vrui units, one value per side - see
         * UIPadding for the named constructors. The border's thickness adds to it rather than eating
         * into it, so changing one never moves the other.
         *
         * Per side because a row of text is wider than it is tall, so the same number rarely reads
         * the same way above it as beside it.
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

        virtual std::string toString() const override;

        // Internal: nothing to do during layout - the rows are pulled at frame end, once the whole
        // tree has been laid out and this panel's transform is final.
        virtual void onFrameUpdate(UIFrameUpdateContext*) override
        {}

        // Internal: append this panel's rows to the frame being built for the render thread.
        void appendTo(render::PrimitiveDraw& frame) const;

    private:
        TextRowsCallback _content;
        render::TextAlign _align = render::TextAlign::Left;
        bool _occluded = true;
        float _textHeightUnits = TEXT_PANEL_TEXT_HEIGHT_UNITS;
        float _lineSpacing = TEXT_PANEL_LINE_SPACING;

        // row colour, background, border, rounding and padding together, defaulting to a bare panel
        UIPanelStyle _style;
    };
}
