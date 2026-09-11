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
     * A piece of a row in a colour of its own. Spans follow one another with nothing added between them,
     * so the spaces between words belong to one span or the other.
     */
    struct TextSpan
    {
        std::string text;

        // the row's colour when unset
        std::optional<render::Color> color;
    };

    /**
     * One row of the panel: its text, and optionally a color, a decoration and a text height of its own.
     *
     * A row that names no color takes the panel's (setColor), so a panel that is mostly one color
     * says so once and only the rows that stand out name their own. Color is what splits a draw
     * call, not the row, so rows sharing a color cost nothing extra - but the runs are emitted in
     * order, so alternating colors down a long panel does add draws. A decoration adds no draw: it
     * goes out with its row's text.
     *
     * A row is a paragraph rather than a line: a '\n' in its text starts a new line, and text too
     * wide for the panel wraps onto the next. Every line a row makes keeps its color, decoration and
     * height. A '\t' moves on to the next tab stop (see UITextPanel::setTabWidth).
     *
     * For more than one color in a row, give it spans instead of text: they are drawn one after another
     * as a single paragraph, each in its own color or the row's, and wrap as one - a color change inside
     * a word does not let it break there. An underline runs under each span's text, so it has a gap
     * where a color change falls on a space.
     *
     *     rows.emplace_back("PRESETS", std::nullopt, render::TextDecoration::Underline);
     *     rows.push_back({ .textHeight = 0.4f, .spans = { { "HP " }, { "100", render::colors::Green } } });
     */
    struct TextRow
    {
        std::string text;
        std::optional<render::Color> color;
        render::TextDecoration decoration = render::TextDecoration::None;

        // height of this row's capitals in vrui units; the panel's text height when unset
        std::optional<float> textHeight;

        // when not empty, drawn in place of `text`: the row as pieces in colors of their own
        std::vector<TextSpan> spans;
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
     * Distance from one line's capitals to the next's, as a multiple of the text height. At 1.0 a line's
     * capitals start right where the line above sits, so its descenders run into them; about 1.4 clears
     * them. setLineSpacing overrides it.
     *
     * The extra space goes BETWEEN lines only. The first line's capitals start on the top edge of the
     * text area, so opening the lines up never widens the gap to the top border - that gap is the
     * padding, and matches the gap to the left border, which is measured to the first letter's ink.
     * Between lines of different heights, the extra space follows the taller of the two, so a small line
     * never tucks into a large one's descenders.
     */
    inline constexpr float TEXT_PANEL_LINE_SPACING = 1.6f;

    /**
     * Tab stops are this many space widths apart, at the panel's text height, until setTabWidth says
     * otherwise.
     */
    inline constexpr float TEXT_PANEL_TAB_SPACES = 4.0f;

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
     *         rows.emplace_back(std::format("FOV\t{:.1f}", beamFov()));
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
     * Spaces at the start of a row indent its first line, and a tab moves on to the next stop, measured
     * from the start of the line in vrui units - so rows line up in columns even at different text
     * heights. Spaces at the start of a line the wrap made are dropped, as is a tab whose stop is past
     * the width, which wraps instead.
     *
     * The text is the framework's font - Roboto Medium, unless the mod ships its own at
     * render::CUSTOM_TEXT_FONT_PATH - kerned and drawn from a distance field so it stays sharp at any
     * size and viewing distance. What you give up against UICanvas is layout: no scrolling, and no mixing
     * sizes within a row.
     *
     * The content callback runs every frame, but the text is only re-wrapped and re-measured when a row's
     * text, spans or height changes, or the width, text height or tab width it is laid out against does.
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
         *        holds at their spacing, with room under the last for its descenders, each wrapped to
         *        that width.
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
         * Which edge the lines are aligned to. Left (the default) starts them at the left edge,
         * Right ends them at the right edge, Center straddles the middle.
         */
        void setAlign(render::TextAlign align);

        /**
         * Height of a capital letter in vrui units - the text size of every row that does not name its
         * own. Lines stay as tightly or loosely spaced as they were, because the spacing is a multiple of
         * it.
         */
        void setTextHeight(float units);

        /**
         * Distance from one line's capitals to the next's, as a multiple of the text height. Below 1.0 a
         * line's capitals would overlap the line above's, so it is clamped there.
         */
        void setLineSpacing(float multiplier);

        /**
         * Distance between tab stops, in vrui units, measured from the start of the line. 0, the default,
         * puts them TEXT_PANEL_TAB_SPACES space widths apart at the panel's text height.
         */
        void setTabWidth(float units);

    protected:
        std::optional<UISize> measureContent(float availableWidth, float availableHeight) override;
        void appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const override;
        void writeDevLayoutFields(std::string& line) const override;
        void readDevLayoutFields(const DevLayoutFields& fields) override;

        std::string_view typeName() const override
        {
            return "UITextPanel";
        }

    private:
        /**
         * A run of one span's text placed on a line. Byte offsets rather than a view, so the rows can be
         * swapped for an equal set without leaving the pieces pointing into the old strings.
         */
        struct TextPiece
        {
            std::size_t span;
            std::size_t begin;
            std::size_t length;

            // pen position the run starts at, from the line's start, and the offset of its first ink from
            // there, in vrui units
            float penX;
            float inkLeft;
        };

        /**
         * One drawn line: its row, its pieces, and the extent of its ink, in vrui units.
         */
        struct TextLine
        {
            std::size_t row;
            std::size_t firstPiece;
            std::size_t pieceCount;
            float textHeight;

            // the ink of a line starting at pen 0 begins a side bearing in; that bearing is taken off so
            // the first letter's ink sits on the edge, as it does for a single run
            float bearing;
            float inkRight;

            float width() const
            {
                return pieceCount > 0 ? inkRight - bearing : 0.0f;
            }
        };

        class LineWrapper;

        static std::size_t spanCount(const TextRow& row);
        static std::string_view spanText(const TextRow& row, std::size_t span);
        float rowTextHeight(const TextRow& row) const;
        float resolveTabWidth() const;
        float lineStep(std::size_t line) const;
        void wrapRows(float wrapWidth);

        TextRowsCallback _content;
        render::TextAlign _align = render::TextAlign::Left;
        float _textHeightUnits = TEXT_PANEL_TEXT_HEIGHT_UNITS;
        float _lineSpacing = TEXT_PANEL_LINE_SPACING;
        float _tabWidthUnits = 0.0f;

        // this frame's rows, fetched during layout and drawn at frame end; the callback fills the
        // scratch vector, which is kept only to compare against and to reuse its capacity
        std::vector<TextRow> _rows;
        std::vector<TextRow> _scratchRows;

        // the rows laid out into lines of pieces, valid while the rows and what they were laid out
        // against still match
        std::vector<TextLine> _lines;
        std::vector<TextPiece> _pieces;
        bool _linesValid = false;
        float _linesWrapWidth = 0.0f;
        float _linesTextHeight = 0.0f;
        float _linesTabWidth = 0.0f;
        float _linesWidestUnits = 0.0f;
    };
}
