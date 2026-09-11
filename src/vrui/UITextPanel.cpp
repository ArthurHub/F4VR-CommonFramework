#include "UITextPanel.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "../render/TextFont.h"

namespace f4cf::vrui
{
    namespace
    {
        /**
         * Room added to the content area before deciding what fits in it, in vrui units - far too little
         * to see. A panel sized to its content is exactly as wide and tall as that content, and the draw
         * re-derives the area through the world scale, so float rounding alone would otherwise drop the
         * last letter of the widest line or the last line.
         */
        constexpr float FIT_SLACK_UNITS = 0.001f;

        /**
         * Length in bytes of the UTF-8 character that starts at text[0], so a forced break never splits
         * one.
         */
        std::size_t firstCharLength(const std::string_view text)
        {
            const auto lead = static_cast<unsigned char>(text[0]);
            const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            return (std::min)(length, text.size());
        }
    }

    UITextPanel::UITextPanel(const std::string& name)
        : UIPanel(name, 0.0f, 0.0f)
    {
        setSizing(UIPanelSizing::FitContent);
    }

    UITextPanel::UITextPanel(const std::string& name, const float width)
        : UIPanel(name, width, 0.0f)
    {
        setSizing(UIPanelSizing::FixedWidth);
    }

    UITextPanel::UITextPanel(const std::string& name, const float width, const float height)
        : UIPanel(name, width, height)
    {}

    void UITextPanel::setContent(TextRowsCallback content)
    {
        _content = std::move(content);
        _rows.clear();
        _lines.clear();
        _linesValid = false;
    }

    void UITextPanel::setColor(const render::Color& color)
    {
        _style.color = color;
    }

    void UITextPanel::setAlign(const render::TextAlign align)
    {
        _align = align;
    }

    void UITextPanel::setTextHeight(const float units)
    {
        // a non-positive height would divide by zero in the row-count maths and draw nothing at all
        _textHeightUnits = (std::max)(0.01f, units);
    }

    void UITextPanel::setLineSpacing(const float multiplier)
    {
        // below 1.0 consecutive rows would overlap, which is never what a caller means by spacing
        _lineSpacing = (std::max)(1.0f, multiplier);
    }

    /**
     * Fetch this frame's rows, re-wrap them if anything they are wrapped against changed, and report
     * the size of the wrapped block.
     *
     * The callback runs every frame, but it usually produces the same text as last frame, so only the
     * texts are compared and the wrap is redone only when one differs, or the width or text height
     * does. The rows are taken either way, so a colour or decoration change still reaches the draw.
     */
    std::optional<UISize> UITextPanel::measureContent(const float availableWidth)
    {
        if (!_content) {
            return UISize(0.0f, 0.0f);
        }

        _scratchRows.clear();
        _content(_scratchRows);
        const bool textChanged = !std::ranges::equal(_rows, _scratchRows, {}, &TextRow::text, &TextRow::text);
        std::swap(_rows, _scratchRows);

        if (textChanged || !_linesValid || availableWidth != _linesWrapWidth || _textHeightUnits != _linesTextHeight) {
            wrapRows(availableWidth);
        }

        if (_lines.empty()) {
            return UISize(0.0f, 0.0f);
        }
        // the first line's capitals start on the top edge, each further line is a pitch below, and
        // the last needs room for its descenders
        const float height = _textHeightUnits + static_cast<float>(_lines.size() - 1) * _textHeightUnits * _lineSpacing + render::textDescent(_textHeightUnits);
        return UISize(_linesWidestUnits, height);
    }

    /**
     * Break the rows into lines no wider than wrapWidth, in vrui units (infinite for no wrapping), and
     * note the widest.
     *
     * Each '\n' ends a line, and an empty line is kept, so a blank row still takes its space. Within a
     * line, the break goes after the last whole word that fits, dropping the spaces at it; a word that
     * does not fit on a line of its own is broken after the last character that does - and after at
     * least one, however narrow the panel, so the wrap always moves forward.
     *
     * Measured at the text height in vrui units: widths scale linearly with the height, so what fits
     * here fits the world-scaled area at draw time.
     */
    void UITextPanel::wrapRows(const float wrapWidth)
    {
        _lines.clear();
        _linesWidestUnits = 0.0f;
        _linesWrapWidth = wrapWidth;
        _linesTextHeight = _textHeightUnits;
        _linesValid = true;

        const auto addLine = [&](const std::size_t row, const std::string_view text, const std::size_t begin, const std::size_t length) {
            _lines.push_back({ row, begin, length });
            _linesWidestUnits = (std::max)(_linesWidestUnits, render::measureText(text.substr(begin, length), _textHeightUnits));
        };

        for (std::size_t row = 0; row < _rows.size(); ++row) {
            const std::string_view text = _rows[row].text;
            std::size_t segmentBegin = 0;
            while (true) {
                std::size_t segmentEnd = text.find('\n', segmentBegin);
                const bool lastSegment = segmentEnd == std::string_view::npos;
                if (lastSegment) {
                    segmentEnd = text.size();
                }
                // a Windows line ending leaves its '\r' behind, which would draw as a missing glyph
                const std::size_t contentEnd = segmentEnd > segmentBegin && text[segmentEnd - 1] == '\r' ? segmentEnd - 1 : segmentEnd;

                std::size_t pos = segmentBegin;
                if (pos == contentEnd) {
                    addLine(row, text, pos, 0);
                }
                while (pos < contentEnd) {
                    const std::string_view rest = text.substr(pos, contentEnd - pos);
                    const std::size_t fits = render::fitText(rest, _textHeightUnits, wrapWidth);
                    if (fits >= rest.size()) {
                        addLine(row, text, pos, rest.size());
                        break;
                    }

                    // fitText counts spaces as always fitting, so a space at or before `fits` ends a
                    // word that fit whole
                    std::size_t lineEnd = 0;
                    std::size_t next = 0;
                    const std::size_t space = rest.find_last_of(' ', fits);
                    if (space != std::string_view::npos) {
                        lineEnd = space;
                        while (lineEnd > 0 && rest[lineEnd - 1] == ' ') {
                            --lineEnd;
                        }
                        next = space + 1;
                    }
                    if (lineEnd == 0) {
                        // no whole word fits: break inside it
                        lineEnd = fits > 0 ? fits : firstCharLength(rest);
                        next = lineEnd;
                    }
                    addLine(row, text, pos, lineEnd);

                    pos += next;
                    while (pos < contentEnd && text[pos] == ' ') {
                        ++pos;
                    }
                }

                if (lastSegment) {
                    break;
                }
                segmentBegin = segmentEnd + 1;
            }
        }
    }

    /**
     * Draw the lines wrapped during layout, top down from the top edge of the content area.
     *
     * The lines already fit the width they were wrapped to; each is still cut to the area in case the
     * panel's size was changed after layout. Lines past the bottom are dropped, since spilling over the
     * edge would cover the neighbouring widgets.
     */
    void UITextPanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (_lines.empty()) {
            return;
        }

        const float textHeight = _textHeightUnits * area.scale;
        const float rowPitch = textHeight * _lineSpacing;
        if (rowPitch <= 0.0f) {
            return;
        }
        const float slack = FIT_SLACK_UNITS * area.scale;
        // a line takes its capitals and the descenders under them; the line spacing only adds room
        // between lines
        const float rowHeight = textHeight + render::textDescent(textHeight);
        if (area.height + slack < rowHeight) {
            logger::sample(5000,
                "Text panel '{}' has no room for a row of text inside its border and padding ({:.2f} units tall, a row needs {:.2f}); no rows drawn",
                _name,
                area.height / area.scale,
                rowHeight / area.scale);
            return;
        }

        // the scale cancels out of this division, and out of fitting each line to the width below, so
        // what fits follows the layout rather than the size the container happens to have been given
        const auto maxLines = 1 + static_cast<std::size_t>((area.height + slack - rowHeight) / rowPitch);

        // the first line's capitals start ON the top edge of the content area, so the space above them
        // is the padding and nothing else - matching the space left of the first letter's ink
        const float topOffset = area.height * 0.5f;
        const float alignOffset = _align == render::TextAlign::Left ? area.width * -0.5f : _align == render::TextAlign::Right ? area.width * 0.5f : 0.0f;

        for (std::size_t index = 0; index < _lines.size() && index < maxLines; ++index) {
            const TextLine& line = _lines[index];
            const TextRow& row = _rows[line.row];
            std::string_view text = std::string_view(row.text).substr(line.begin, line.length);
            text = text.substr(0, render::fitText(text, textHeight, area.width + slack));
            const RE::NiPoint3 anchor = area.center + area.right * alignOffset + area.up * (topOffset - static_cast<float>(index) * rowPitch);
            frame.addOrientedText(text, anchor, area.right, area.up, textHeight, row.color.value_or(_style.color), _align, 0.0f, 0.0f, row.decoration);
        }
    }
}
