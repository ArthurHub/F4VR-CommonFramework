#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "UIPanel.h"

namespace f4cf::vrui
{
    /**
     * One row of the panel: its text, and optionally a color and a decoration of its own.
     *
     * A row that names no color takes the panel's (setColor), so a panel that is mostly one color
     * says so once and only the rows that stand out name their own. Color is what splits a draw
     * call, not the row, so rows sharing a color cost nothing extra - but the runs are emitted in
     * order, so alternating colors down a long panel does add draws. A decoration adds no draw: it
     * goes out with its row's text.
     *
     * A row is a paragraph rather than a line: a '\n' in its text starts a new line, and text too
     * wide for the panel wraps onto the next. Every line a row makes keeps its color and decoration.
     *
     *     rows.emplace_back("PRESETS", std::nullopt, render::TextDecoration::Underline);
     */
    struct TextRow
    {
        std::string text;
        std::optional<render::Color> color;
        render::TextDecoration decoration = render::TextDecoration::None;
    };

    /**
     * Fills in the rows the panel shows this frame. Runs on the GAME thread once per frame during
     * layout, before the tree is laid out, so the panel can size itself to what it is given - which
     * means game state that changes later in the frame shows up on the next one. Append to the
     * vector; it arrives empty, and an empty result simply draws nothing.
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
     * A vrui panel that draws rows of text with the framework's own primitive renderer.
     *
     * It is the low-fidelity sibling of imgui::UICanvas: the same idea - a rectangle in a vrui
     * layout whose inside is drawn by the overlay rather than by scene-graph geometry - but with no
     * Dear ImGui behind it. No atlas texture, no render-to-texture pass, no ImGui dependency at all,
     * so it is available even when F4CF_WITH_IMGUI_UI is OFF.
     *
     *     auto readout = std::make_shared<vrui::UITextPanel>("BeamReadout", 12.0f);
     *     readout->setStyle(vrui::F4VR_PANEL_STYLE);
     *     readout->setContent([this](std::vector<vrui::TextRow>& rows) {
     *         rows.emplace_back(std::format("FOV {:.1f}", beamFov()));
     *         rows.emplace_back("OVERHEAT", render::colors::Red);
     *     });
     *     panel->addElement(readout);
     *
     * Which constructor builds it picks how it is sized (see UIPanelSizing): with no size it fits its
     * content, with a width it grows to the height its content needs at that width, and with both it
     * stays as given. Text wraps at spaces to the width it has, breaking a word only when the word
     * alone is too wide; a fit-content panel has no width to wrap to unless setMaxWidth gives it one.
     * A fixed-size panel drops the lines past its bottom edge rather than spill over the widgets below.
     *
     * The text is the framework's font - Roboto Medium, unless the mod ships its own at
     * render::CUSTOM_TEXT_FONT_PATH - kerned and drawn from a distance field so it stays sharp at any
     * size and viewing distance. What you give up against UICanvas is layout: one size per panel and
     * one color per row, and no scrolling.
     *
     * The content callback runs every frame, but the text is only re-wrapped and re-measured when it
     * changes, or the width, text height or chrome it is laid out against does.
     *
     * The chrome - background, border, rounding, padding, occlusion - is UIPanel's; the rows are laid
     * out inside what it leaves, and the style's content colour is the rows' default colour.
     */
    class UITextPanel : public UIPanel
    {
    public:
        /**
         * A panel as wide and as tall as its content, border and padding included.
         * @param name identifies the element in logs.
         */
        explicit UITextPanel(const std::string& name);

        /**
         * A panel of fixed width whose height follows its content wrapped to that width.
         * @param name identifies the element in logs.
         * @param width size in vrui units, border and padding included.
         */
        UITextPanel(const std::string& name, float width);

        /**
         * A panel of fixed size.
         * @param name identifies the element in logs.
         * @param width / height size in vrui units. What fits is measured against the area left
         *        after the border and padding are taken off each side: as many lines as that height
         *        holds at the row pitch (text height x line spacing), with room under the last for its
         *        descenders, each wrapped to that width.
         */
        UITextPanel(const std::string& name, float width, float height);

        using UIPanel::setMaxWidth;
        using UIPanel::setSizing;

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

    protected:
        std::optional<UISize> measureContent(float availableWidth) override;
        void appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const override;

        std::string_view typeName() const override
        {
            return "UITextPanel";
        }

    private:
        /**
         * One drawn line: a byte range of one row's text. Offsets rather than a view, so the rows can
         * be swapped for an equal set without leaving the lines pointing into the old strings.
         */
        struct TextLine
        {
            std::size_t row;
            std::size_t begin;
            std::size_t length;
        };

        void wrapRows(float wrapWidth);

        TextRowsCallback _content;
        render::TextAlign _align = render::TextAlign::Left;
        float _textHeightUnits = TEXT_PANEL_TEXT_HEIGHT_UNITS;
        float _lineSpacing = TEXT_PANEL_LINE_SPACING;

        // this frame's rows, fetched during layout and drawn at frame end; the callback fills the
        // scratch vector, which is kept only to compare against and to reuse its capacity
        std::vector<TextRow> _rows;
        std::vector<TextRow> _scratchRows;

        // the rows wrapped into lines, valid while the text and what it was wrapped against still match
        std::vector<TextLine> _lines;
        bool _linesValid = false;
        float _linesWrapWidth = 0.0f;
        float _linesTextHeight = 0.0f;
        float _linesWidestUnits = 0.0f;
    };
}
