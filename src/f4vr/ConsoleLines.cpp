#include "ConsoleLines.h"

#include <algorithm>

namespace f4cf::f4vr
{
    /**
     * Adds what one print gave: a line for each piece between newlines, without a trailing carriage return. A print
     * ends in a newline, which starts no line of its own; a blank line inside it is a line. An empty print adds none.
     */
    void ConsoleLines::add(std::string_view printed)
    {
        if (printed.empty()) {
            return;
        }
        if (printed.ends_with('\n')) {
            printed.remove_suffix(1);
        }
        std::vector<std::string> pieces;
        while (true) {
            const auto end = printed.find('\n');
            auto piece = printed.substr(0, end);
            if (piece.ends_with('\r')) {
                piece.remove_suffix(1);
            }
            pieces.emplace_back(piece);
            if (end == std::string_view::npos) {
                break;
            }
            printed.remove_prefix(end + 1);
        }

        std::scoped_lock lock(_mutex);
        for (auto& piece : pieces) {
            _lines.push_back({ .number = ++_lastNumber, .text = std::move(piece) });
            if (_lines.size() > _capacity) {
                _lines.pop_front();
            }
        }
    }

    /**
     * The lines numbered after the number, of those still kept, oldest first. 0 gives every line kept.
     */
    std::vector<ConsoleLine> ConsoleLines::since(const std::uint64_t number) const
    {
        std::scoped_lock lock(_mutex);
        const auto first = std::ranges::find_if(_lines, [number](const ConsoleLine& line) {
            return line.number > number;
        });
        return { first, _lines.end() };
    }

    /**
     * The number of the latest line, 0 before the first.
     */
    std::uint64_t ConsoleLines::lastNumber() const
    {
        std::scoped_lock lock(_mutex);
        return _lastNumber;
    }

    std::size_t ConsoleLines::size() const
    {
        std::scoped_lock lock(_mutex);
        return _lines.size();
    }
}
