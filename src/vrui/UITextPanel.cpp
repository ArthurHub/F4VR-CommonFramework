#include "UITextPanel.h"

#include <algorithm>
#include <cmath>
#include <optional>
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

        /**
         * Whether two rows lay out the same: equal text, spans and height. Colours and decoration change
         * only how the lines are drawn, not where they break.
         */
        bool sameLayout(const TextRow& a, const TextRow& b)
        {
            return a.text == b.text && a.textHeight == b.textHeight && std::ranges::equal(a.spans, b.spans, {}, &TextSpan::text, &TextSpan::text);
        }
    }

    /**
     * Breaks rows into lines of placed pieces, one row at a time.
     *
     * A row is read as words, runs of spaces, tabs and newlines. A word may run across spans - a colour
     * change inside a word is not a place to break it - so each word is measured whole, as the sum of its
     * parts' advances, and goes on the current line if its ink still ends inside the width, or else starts
     * the next one. Only a word too wide for a line of its own is broken inside, after the last character
     * that fits, and after at least one however narrow the panel, so the wrap always moves forward.
     *
     * Positions are pen positions from the line's start, in vrui units. Spaces are held back until a word
     * follows them, so the spaces a line ends on take no room; they are dropped at the start of a line the
     * wrap made, and indent a row's first line. A tab moves the pen to the next stop past it, or wraps when
     * that stop is past the width.
     *
     * Consecutive text of one span is merged into one piece, the spaces between included, so a run of one
     * colour goes out as a single run of text: kerned throughout, and underlined across its spaces.
     */
    class UITextPanel::LineWrapper
    {
    public:
        LineWrapper(std::vector<TextLine>& lines, std::vector<TextPiece>& pieces, const float wrapWidth, const float tabWidth)
            : _lines(lines),
              _pieces(pieces),
              _wrapWidth(wrapWidth),
              _tabWidth(tabWidth)
        {}

        /**
         * Lay out one row at its text height, appending its lines and their pieces.
         */
        void wrapRow(const std::size_t rowIndex, const TextRow& row, const float textHeight)
        {
            _row = &row;
            _rowIndex = rowIndex;
            _textHeight = textHeight;
            startLine(true);

            for (std::size_t span = 0; span < spanCount(row); ++span) {
                const std::string_view text = spanText(row, span);
                std::size_t pos = 0;
                while (pos < text.size()) {
                    const char c = text[pos];
                    if (c == '\n') {
                        flushWord();
                        finishLine();
                        startLine(true);
                        ++pos;
                    } else if (c == '\r') {
                        // what a Windows line ending leaves before its '\n'
                        flushWord();
                        ++pos;
                    } else if (c == '\t') {
                        flushWord();
                        addTab();
                        ++pos;
                    } else if (c == ' ') {
                        flushWord();
                        const std::size_t end = (std::min)(text.find_first_not_of(' ', pos), text.size());
                        addSpaces(span, pos, end - pos);
                        pos = end;
                    } else {
                        const std::size_t end = (std::min)(text.find_first_of(" \t\r\n", pos), text.size());
                        _word.push_back({ span, pos, end - pos, render::measureTextRun(text.substr(pos, end - pos), _textHeight) });
                        pos = end;
                    }
                }
            }

            flushWord();
            finishLine();
        }

    private:
        /**
         * The part of a word that lies in one span.
         */
        struct WordPart
        {
            std::size_t span;
            std::size_t begin;
            std::size_t length;
            render::TextRunMetrics metrics;
        };

        void startLine(const bool paragraphStart)
        {
            _line = TextLine{ .row = _rowIndex, .firstPiece = _pieces.size(), .pieceCount = 0, .textHeight = _textHeight, .bearing = 0.0f, .inkRight = 0.0f };
            _paragraphStart = paragraphStart;
            _penX = 0.0f;
            clearPending();
            _pieceBreak = false;
        }

        void finishLine()
        {
            // a line the wrap started but nothing landed on - a tab pushed past the edge at the end of a
            // row - takes no space; an empty paragraph line does, so a blank row still shows as one
            if (!_paragraphStart && _line.pieceCount == 0) {
                return;
            }
            _lines.push_back(_line);
        }

        bool lineAtStart() const
        {
            return _line.pieceCount == 0 && _penX == 0.0f;
        }

        void clearPending()
        {
            _pendingAdvance = 0.0f;
            _pendingSpan.reset();
            _pendingMixed = false;
        }

        void addSpaces(const std::size_t span, const std::size_t begin, const std::size_t length)
        {
            if (!_paragraphStart && lineAtStart()) {
                return;
            }
            _pendingAdvance += render::measureTextRun(spanText(*_row, span).substr(begin, length), _textHeight).advance;
            if (!_pendingSpan) {
                _pendingSpan = span;
            } else if (*_pendingSpan != span) {
                _pendingMixed = true;
            }
        }

        void addTab()
        {
            if (!_paragraphStart && lineAtStart()) {
                return;
            }
            const float from = _penX + _pendingAdvance;
            const float stop = (std::floor(from / _tabWidth) + 1.0f) * _tabWidth;
            clearPending();
            if (stop - _line.bearing > _wrapWidth && !lineAtStart()) {
                finishLine();
                startLine(false);
                return;
            }
            _penX = stop;
            _pieceBreak = true;
        }

        /**
         * Place the word gathered so far: here if it fits, on a new line if it fits there, broken inside
         * if it does not fit a line of its own.
         */
        void flushWord()
        {
            if (_word.empty()) {
                return;
            }

            float advance = 0.0f;
            float inkLeft = 0.0f;
            float inkRight = 0.0f;
            bool hasInk = false;
            for (const WordPart& part : _word) {
                if (part.metrics.hasInk) {
                    if (!hasInk) {
                        inkLeft = advance + part.metrics.inkLeft;
                        hasInk = true;
                    }
                    inkRight = advance + part.metrics.inkRight;
                }
                advance += part.metrics.advance;
            }

            const auto fitsFrom = [&](const float startX) {
                const float bearing = _line.pieceCount > 0 ? _line.bearing : startX == 0.0f ? inkLeft : 0.0f;
                return !hasInk || startX + inkRight - bearing <= _wrapWidth;
            };

            float startX = _penX + _pendingAdvance;
            bool fits = fitsFrom(startX);
            if (!fits && startX > 0.0f) {
                finishLine();
                startLine(false);
                startX = 0.0f;
                fits = fitsFrom(startX);
            }

            if (fits) {
                _penX = startX;
                for (const WordPart& part : _word) {
                    placeRun(part.span, part.begin, part.length, part.metrics);
                }
            } else {
                breakWord();
            }
            _word.clear();
        }

        /**
         * Place a word too wide for a line of its own, part by part, cutting a part after the last
         * character that fits and carrying the rest onto the next line.
         */
        void breakWord()
        {
            for (const WordPart& part : _word) {
                const std::string_view text = spanText(*_row, part.span);
                std::size_t begin = part.begin;
                std::size_t length = part.length;
                render::TextRunMetrics metrics = part.metrics;
                while (length > 0) {
                    const float startX = _penX + _pendingAdvance;
                    const float bearing = _line.pieceCount > 0 ? _line.bearing : startX == 0.0f ? metrics.inkLeft : 0.0f;
                    if (!metrics.hasInk || startX + metrics.inkRight - bearing <= _wrapWidth) {
                        _penX = startX;
                        placeRun(part.span, begin, length, metrics);
                        break;
                    }

                    // the room from this part's first ink to the right edge
                    std::size_t fits = render::fitText(text.substr(begin, length), _textHeight, _wrapWidth - (startX + metrics.inkLeft - bearing));
                    if (fits >= length) {
                        _penX = startX;
                        placeRun(part.span, begin, length, metrics);
                        break;
                    }
                    if (fits == 0) {
                        if (!lineAtStart()) {
                            finishLine();
                            startLine(false);
                            continue;
                        }
                        fits = firstCharLength(text.substr(begin, length));
                    }

                    _penX = startX;
                    placeRun(part.span, begin, fits, render::measureTextRun(text.substr(begin, fits), _textHeight));
                    begin += fits;
                    length -= fits;
                    metrics = render::measureTextRun(text.substr(begin, length), _textHeight);
                    finishLine();
                    startLine(false);
                }
            }
        }

        /**
         * Put a run at the pen, joining the line's last piece when it continues the same span with nothing
         * but that span's spaces between them, and move the pen past it.
         */
        void placeRun(const std::size_t span, const std::size_t begin, const std::size_t length, const render::TextRunMetrics& metrics)
        {
            const bool joinsPiece = _line.pieceCount > 0 && !_pieceBreak && _pieces.back().span == span && (!_pendingSpan || (*_pendingSpan == span && !_pendingMixed));
            clearPending();
            _pieceBreak = false;
            if (!metrics.hasInk) {
                _penX += metrics.advance;
                return;
            }

            if (lineAtStart()) {
                _line.bearing = metrics.inkLeft;
            }
            if (joinsPiece) {
                TextPiece& piece = _pieces.back();
                piece.length = begin + length - piece.begin;
            } else {
                _pieces.push_back({ span, begin, length, _penX, metrics.inkLeft });
                ++_line.pieceCount;
            }
            _line.inkRight = (std::max)(_line.inkRight, _penX + metrics.inkRight);
            _penX += metrics.advance;
        }

        std::vector<TextLine>& _lines;
        std::vector<TextPiece>& _pieces;
        float _wrapWidth;
        float _tabWidth;

        const TextRow* _row = nullptr;
        std::size_t _rowIndex = 0;
        float _textHeight = 0.0f;

        TextLine _line{};
        bool _paragraphStart = true;
        float _penX = 0.0f;

        // spaces waiting for a word to follow them, and whether they all came from one span - only then
        // can the word join that span's piece
        float _pendingAdvance = 0.0f;
        std::optional<std::size_t> _pendingSpan;
        bool _pendingMixed = false;

        // set by a tab: the next text starts a piece of its own, even in the same span
        bool _pieceBreak = false;

        std::vector<WordPart> _word;
    };

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
        _pieces.clear();
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
        // a non-positive height would divide by zero in the line maths and draw nothing at all
        _textHeightUnits = (std::max)(0.01f, units);
    }

    void UITextPanel::setLineSpacing(const float multiplier)
    {
        // below 1.0 consecutive lines would overlap, which is never what a caller means by spacing
        _lineSpacing = (std::max)(1.0f, multiplier);
    }

    void UITextPanel::setTabWidth(const float units)
    {
        _tabWidthUnits = (std::max)(0.0f, units);
    }

    std::size_t UITextPanel::spanCount(const TextRow& row)
    {
        return row.spans.empty() ? 1 : row.spans.size();
    }

    std::string_view UITextPanel::spanText(const TextRow& row, const std::size_t span)
    {
        return row.spans.empty() ? std::string_view(row.text) : std::string_view(row.spans[span].text);
    }

    float UITextPanel::rowTextHeight(const TextRow& row) const
    {
        return row.textHeight ? (std::max)(0.01f, *row.textHeight) : _textHeightUnits;
    }

    float UITextPanel::resolveTabWidth() const
    {
        // never zero, which would put every stop on the pen and divide by zero finding the next one
        const float width = _tabWidthUnits > 0.0f ? _tabWidthUnits : render::measureTextRun(" ", _textHeightUnits).advance * TEXT_PANEL_TAB_SPACES;
        return (std::max)(0.01f, width);
    }

    /**
     * From one line's capitals to the next's, in vrui units: the line's own height, plus the spacing's
     * extra room scaled by the taller of the two, so a small line under a large one still clears its
     * descenders. Equal heights give the plain text height x line spacing.
     */
    float UITextPanel::lineStep(const std::size_t line) const
    {
        const float height = _lines[line].textHeight;
        return height + (_lineSpacing - 1.0f) * (std::max)(height, _lines[line + 1].textHeight);
    }

    /**
     * Fetch this frame's rows, lay them out again if anything they are laid out against changed, and
     * report the size of the block.
     *
     * The callback runs every frame, but it usually produces the same rows as last frame, so only what
     * affects the layout is compared - texts, spans and heights - and the layout is redone only when one
     * differs, or the width, text height or tab width does. The rows are taken either way, so a colour
     * or decoration change still reaches the draw.
     */
    std::optional<UISize> UITextPanel::measureContent(const float availableWidth, float)
    {
        if (!_content) {
            return UISize(0.0f, 0.0f);
        }

        _scratchRows.clear();
        _content(_scratchRows);
        const bool layoutChanged = !std::ranges::equal(_rows, _scratchRows, sameLayout);
        std::swap(_rows, _scratchRows);

        if (layoutChanged || !_linesValid || availableWidth != _linesWrapWidth || _textHeightUnits != _linesTextHeight || _tabWidthUnits != _linesTabWidth) {
            wrapRows(availableWidth);
        }

        if (_lines.empty()) {
            return UISize(0.0f, 0.0f);
        }
        // every line but the last steps down to the next; the last needs its capitals and its descenders
        float height = 0.0f;
        for (std::size_t line = 0; line + 1 < _lines.size(); ++line) {
            height += lineStep(line);
        }
        const float lastHeight = _lines.back().textHeight;
        height += lastHeight + render::textDescent(lastHeight);
        return UISize(_linesWidestUnits, height);
    }

    /**
     * Lay the rows out into lines no wider than wrapWidth, in vrui units (infinite for no wrapping), and
     * note the widest. See LineWrapper for how a row breaks.
     *
     * Measured in vrui units at each row's text height: widths scale linearly with the height, so what
     * fits here fits the world-scaled area at draw time.
     */
    void UITextPanel::wrapRows(const float wrapWidth)
    {
        _lines.clear();
        _pieces.clear();
        _linesWidestUnits = 0.0f;
        _linesWrapWidth = wrapWidth;
        _linesTextHeight = _textHeightUnits;
        _linesTabWidth = _tabWidthUnits;
        _linesValid = true;

        LineWrapper wrapper(_lines, _pieces, wrapWidth, resolveTabWidth());
        for (std::size_t row = 0; row < _rows.size(); ++row) {
            wrapper.wrapRow(row, _rows[row], rowTextHeight(_rows[row]));
        }
        for (const TextLine& line : _lines) {
            _linesWidestUnits = (std::max)(_linesWidestUnits, line.width());
        }
    }

    /**
     * Draw the lines laid out during layout, top down from the top edge of the content area, each piece as
     * its own run of text at its place on the line.
     *
     * The lines already fit the width they were wrapped to; each piece is still cut to the area in case the
     * panel's size was changed after layout. Lines past the bottom are dropped, since spilling over the edge
     * would cover the neighbouring widgets.
     */
    void UITextPanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (_lines.empty()) {
            return;
        }

        const float slack = FIT_SLACK_UNITS * area.scale;
        const float leftEdge = area.width * -0.5f;
        const float bottomEdge = area.height * -0.5f - slack;

        // the first line's capitals start ON the top edge of the content area, so the space above them
        // is the padding and nothing else - matching the space left of the first letter's ink
        float capitalsTop = area.height * 0.5f;
        for (std::size_t index = 0; index < _lines.size(); ++index) {
            const TextLine& line = _lines[index];
            const float textHeight = line.textHeight * area.scale;
            if (capitalsTop - textHeight - render::textDescent(textHeight) < bottomEdge) {
                if (index == 0) {
                    logger::sample(5000,
                        "Text panel '{}' has no room for a row of text inside its border and padding ({:.2f} units tall, a row needs {:.2f}); no rows drawn",
                        _name,
                        area.height / area.scale,
                        line.textHeight + render::textDescent(line.textHeight));
                }
                break;
            }

            const TextRow& row = _rows[line.row];
            const float spare = (std::max)(0.0f, area.width - line.width() * area.scale);
            const float alignOffset = _align == render::TextAlign::Left ? 0.0f : _align == render::TextAlign::Right ? spare : spare * 0.5f;

            for (std::size_t p = line.firstPiece; p < line.firstPiece + line.pieceCount; ++p) {
                const TextPiece& piece = _pieces[p];
                const float inkX = (piece.penX + piece.inkLeft - line.bearing) * area.scale + alignOffset;
                const float room = area.width + slack - inkX;
                if (room <= 0.0f) {
                    break;
                }
                std::string_view text = spanText(row, piece.span).substr(piece.begin, piece.length);
                text = text.substr(0, render::fitText(text, textHeight, room));
                if (text.empty()) {
                    break;
                }

                const render::Color rowColor = row.color.value_or(_style.color);
                const render::Color color = row.spans.empty() ? rowColor : row.spans[piece.span].color.value_or(rowColor);
                const RE::NiPoint3 anchor = area.center + area.right * (leftEdge + inkX) + area.up * capitalsTop;
                frame.addOrientedText(text, anchor, area.right, area.up, textHeight, color, render::TextAlign::Left, 0.0f, 0.0f, row.decoration);
            }

            if (index + 1 < _lines.size()) {
                capitalsTop -= lineStep(index) * area.scale;
            }
        }
    }
}
