#include "UITextPanel.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "../render/TextFont.h"

namespace f4cf::vrui
{
    UITextPanel::UITextPanel(const std::string& name, const float width, const float height)
        : UIPanel(name, width, height)
    {}

    void UITextPanel::setContent(TextRowsCallback content)
    {
        _content = std::move(content);
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
     * Turn this frame's rows into plane-welded text runs filling the content area.
     *
     * Rows are laid out downward from the top edge and clipped in both directions, since the panel
     * neither wraps nor scrolls: a row past the bottom is dropped, a row wider than the area is
     * truncated. Silently spilling over the edges would cover the neighbouring widgets.
     */
    void UITextPanel::appendContent(render::PrimitiveDraw& frame, const UIPanelContentArea& area) const
    {
        if (!_content) {
            return;
        }

        std::vector<TextRow> rows;
        _content(rows);
        if (rows.empty()) {
            return;
        }

        const float textHeight = _textHeightUnits * area.scale;
        const float rowPitch = textHeight * _lineSpacing;
        if (rowPitch <= 0.0f) {
            return;
        }
        // a row takes its capitals and the descenders under them; the line spacing only adds room
        // between rows
        const float rowHeight = textHeight + render::textDescent(textHeight);
        if (area.height < rowHeight) {
            logger::sample(5000,
                "Text panel '{}' has no room for a row of text inside its border and padding ({:.2f} units tall, a row needs {:.2f}); no rows drawn",
                _name,
                area.height / area.scale,
                rowHeight / area.scale);
            return;
        }

        // the scale cancels out of this division, and out of fitting each row to the width below, so
        // what fits follows the layout rather than the size the container happens to have been given
        const auto maxRows = 1 + static_cast<std::size_t>((area.height - rowHeight) / rowPitch);

        // the first row's capitals start ON the top edge of the content area, so the space above them
        // is the padding and nothing else - matching the space left of the first letter's ink
        const float topOffset = area.height * 0.5f;
        const float alignOffset = _align == render::TextAlign::Left ? area.width * -0.5f : _align == render::TextAlign::Right ? area.width * 0.5f : 0.0f;

        for (std::size_t row = 0; row < rows.size() && row < maxRows; ++row) {
            const std::string_view text = std::string_view(rows[row].text).substr(0, render::fitText(rows[row].text, textHeight, area.width));
            const RE::NiPoint3 anchor = area.center + area.right * alignOffset + area.up * (topOffset - static_cast<float>(row) * rowPitch);
            frame.addOrientedText(text, anchor, area.right, area.up, textHeight, rows[row].color.value_or(_style.color), _align);
        }
    }
}
