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
     *     rows.emplace_back("PRESETS", std::nullopt, render::TextDecoration::Underline);
     */
    struct TextRow
    {
        std::string text;
        std::optional<render::Color> color;
        render::TextDecoration decoration = render::TextDecoration::None;
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
     * A vrui panel that draws rows of text with the framework's own primitive renderer.
     *
     * It is the low-fidelity sibling of imgui::UICanvas: the same idea - a rectangle in a vrui
     * layout whose inside is drawn by the overlay rather than by scene-graph geometry - but with no
     * Dear ImGui behind it. No atlas texture, no render-to-texture pass, no ImGui dependency at all,
     * so it is available even when F4CF_WITH_IMGUI_UI is OFF.
     *
     *     auto readout = std::make_shared<vrui::UITextPanel>("BeamReadout", 12.0f, 4.0f);
     *     readout->setStyle(vrui::F4VR_PANEL_STYLE);
     *     readout->setContent([this](std::vector<vrui::TextRow>& rows) {
     *         rows.emplace_back(std::format("FOV {:.1f}", beamFov()));
     *         rows.emplace_back("OVERHEAT", render::colors::Red);
     *     });
     *     panel->addElement(readout);
     *
     * The text is the framework's font - Roboto Medium, unless the mod ships its own at
     * render::CUSTOM_TEXT_FONT_PATH - kerned and drawn from a distance field so it stays sharp at any
     * size and viewing distance. What you give up against UICanvas is layout: one size and one color
     * per row, no wrapping and no scrolling. Rows that do not fit are clipped rather than reflowed -
     * past the bottom edge they are dropped, past the right edge they are cut after the last
     * character that fits - because an overflowing row would otherwise cover the sibling widgets.
     *
     * The chrome - background, border, rounding, padding, occlusion - is UIPanel's; the rows are laid
     * out inside what it leaves, and the style's content colour is the rows' default colour.
     */
    class UITextPanel : public UIPanel
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
        void appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const override;

        std::string_view typeName() const override
        {
            return "UITextPanel";
        }

    private:
        TextRowsCallback _content;
        render::TextAlign _align = render::TextAlign::Left;
        float _textHeightUnits = TEXT_PANEL_TEXT_HEIGHT_UNITS;
        float _lineSpacing = TEXT_PANEL_LINE_SPACING;
    };
}
