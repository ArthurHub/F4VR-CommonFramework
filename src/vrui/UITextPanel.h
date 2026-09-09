#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../render/PrimitiveDraw.h"
#include "UIElement.h"

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
     * Height of the glyphs themselves, in vrui units: the text size, and nothing else.
     *
     * Deliberately separate from the spacing below, so opening the rows up does not shrink the text
     * and resizing the text does not change how tight the rows are. setTextHeight overrides it.
     */
    inline constexpr float TEXT_PANEL_TEXT_HEIGHT_UNITS = 0.28f;

    /**
     * Distance from one row to the next, as a multiple of the text height. 1.0 stacks rows so their
     * glyphs touch; higher gives them air. setLineSpacing overrides it.
     */
    inline constexpr float TEXT_PANEL_LINE_SPACING = 1.5f;

    /**
     * Default border thickness and corner radius, in vrui units, for setBorder's optional arguments.
     */
    inline constexpr float TEXT_PANEL_BORDER_THICKNESS_UNITS = 0.08f;
    inline constexpr float TEXT_PANEL_BORDER_CORNER_RADIUS_UNITS = 0.4f;

    /**
     * Space between the rows and the panel's edge, in vrui units, on top of any border. Small by
     * default so text does not sit hard against the edge; setPadding overrides it, 0 included.
     */
    inline constexpr float TEXT_PANEL_PADDING_UNITS = 0.15f;

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
     * What you give up against UICanvas is everything typographic. The font is the built-in 5x7
     * bitmap: blocky, fixed-width, UPPERCASE ONLY (lowercase is upcased on the way in), with no
     * kerning, wrapping or scrolling. Rows that do not fit are clipped rather than reflowed - past
     * the bottom edge they are dropped, past the right edge they are truncated - because an
     * overflowing row would otherwise cover the sibling widgets. Text, in other words, and nothing
     * else: no widgets, no tables, no input.
     *
     * What you gain is that it costs almost nothing and reads like an instrument panel. Every panel
     * shares one overlay layer, and rows of one color collapse into a single draw call.
     *
     * Like UICanvas it draws through the overlay path, so it is always ON TOP - a sibling widget in
     * front of it will not occlude it - and it is not interactive.
     */
    class UITextPanel : public UIElement
    {
    public:
        /**
         * @param name identifies the element in logs.
         * @param width / height size in vrui units. What fits is measured against the area left
         *        after the border and padding are taken off both sides: rows are that height over the
         *        row pitch (text height x line spacing), characters that width over the character
         *        width (text height x render::GLYPH_ASPECT).
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
         * Height of the glyphs in vrui units - the text size. Rows stay as tightly or loosely spaced
         * as they were, because the spacing is a multiple of this.
         */
        void setTextHeight(float units);

        /**
         * Distance from one row to the next, as a multiple of the text height. Below 1.0 the glyphs
         * would collide, so it is clamped there.
         */
        void setLineSpacing(float multiplier);

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
         * @param thickness in vrui units, clamped to half the shorter side.
         * @param cornerRadius in vrui units, 0 for square corners, clamped to half the shorter side.
         */
        void setBorder(const render::Color& color, float thickness = TEXT_PANEL_BORDER_THICKNESS_UNITS, float cornerRadius = TEXT_PANEL_BORDER_CORNER_RADIUS_UNITS);

        void clearBorder();

        /**
         * Space between the rows and the panel's edge, in vrui units. The border's thickness adds to
         * this rather than eating into it, so changing one does not move the other.
         */
        void setPadding(float units);

        virtual std::string toString() const override;

        // Internal: nothing to do during layout - the rows are pulled at frame end, once the whole
        // tree has been laid out and this panel's transform is final.
        virtual void onFrameUpdate(UIFrameUpdateContext*) override
        {}

        // Internal: append this panel's rows to the frame being built for the render thread.
        void appendTo(render::PrimitiveDraw& frame) const;

    private:
        TextRowsCallback _content;
        render::Color _color = render::colors::White;
        render::TextAlign _align = render::TextAlign::Left;
        float _textHeightUnits = TEXT_PANEL_TEXT_HEIGHT_UNITS;
        float _lineSpacing = TEXT_PANEL_LINE_SPACING;

        float _paddingUnits = TEXT_PANEL_PADDING_UNITS;

        // zero thickness is what "no border" means, so no separate flag is needed
        render::Color _borderColor = render::colors::White;
        float _borderThicknessUnits = 0.0f;
        float _borderCornerRadiusUnits = 0.0f;
    };
}
