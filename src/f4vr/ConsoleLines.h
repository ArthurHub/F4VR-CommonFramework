#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// Plain std: no PCH, F4SE or CommonLibF4, so the unit tests compile it by itself.

namespace f4cf::f4vr
{
    /**
     * A line the console printed, numbered in the order printed from 1.
     */
    struct ConsoleLine
    {
        std::uint64_t number = 0;
        std::string text;
    };

    /**
     * The last lines the console printed, numbered in order, the oldest dropped past the capacity. A reader keeps the
     * number of the last line it read and asks for the lines after it, so it sees each line once and can tell from the
     * first number it gets that lines were dropped in between. Any thread: a print can come from any thread.
     */
    class ConsoleLines
    {
    public:
        static constexpr std::size_t DEFAULT_CAPACITY = 4000;

        explicit ConsoleLines(const std::size_t capacity = DEFAULT_CAPACITY)
            : _capacity(capacity == 0 ? 1 : capacity)
        {}

        void add(std::string_view printed);
        [[nodiscard]] std::vector<ConsoleLine> since(std::uint64_t number) const;
        [[nodiscard]] std::uint64_t lastNumber() const;
        [[nodiscard]] std::size_t size() const;

    private:
        mutable std::mutex _mutex;
        std::deque<ConsoleLine> _lines;
        std::size_t _capacity;
        std::uint64_t _lastNumber = 0;
    };
}
