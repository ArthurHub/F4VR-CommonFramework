#include <catch2/catch_test_macros.hpp>

#include "f4vr/ConsoleLines.h"

using f4cf::f4vr::ConsoleLines;

namespace
{
    std::vector<std::string> textsSince(const ConsoleLines& lines, const std::uint64_t number)
    {
        std::vector<std::string> texts;
        for (const auto& line : lines.since(number)) {
            texts.push_back(line.text);
        }
        return texts;
    }
}

TEST_CASE("ConsoleLines: a print is a line for each piece between newlines, the last newline starting none")
{
    ConsoleLines lines;
    lines.add("GetPos: X >> 2048.00\n");
    lines.add("first\r\n\nthird\n");
    lines.add("no newline");
    lines.add("");

    CHECK(textsSince(lines, 0) == std::vector<std::string>{ "GetPos: X >> 2048.00", "first", "", "third", "no newline" });
    CHECK(lines.lastNumber() == 5);
}

TEST_CASE("ConsoleLines: since gives the lines after a number, each numbered in order")
{
    ConsoleLines lines;
    lines.add("a\nb\n");
    const auto read = lines.lastNumber();
    lines.add("c\n");

    const auto after = lines.since(read);
    REQUIRE(after.size() == 1);
    CHECK(after[0].number == 3);
    CHECK(after[0].text == "c");
    CHECK(lines.since(lines.lastNumber()).empty());
}

TEST_CASE("ConsoleLines: past the capacity the oldest lines are dropped, and the numbers go on")
{
    ConsoleLines lines(3);
    lines.add("1\n2\n3\n4\n5\n");

    CHECK(lines.size() == 3);
    const auto kept = lines.since(0);
    REQUIRE(kept.size() == 3);
    CHECK(kept.front().number == 3);
    CHECK(kept.back().number == 5);
    CHECK(textsSince(lines, 1) == std::vector<std::string>{ "3", "4", "5" });
}
