#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

// An example of a plain-std test. Replace it with tests of the mod's game-independent logic: a file of
// src/<area>/ with no PCH, F4SE or CommonLibF4, tested in tests/<area>/<File>Tests.cpp and listed with its
// source in tests/CMakeLists.txt.

TEST_CASE("Example: a plain test runs with no game")
{
    const std::vector values{ 3, 1, 2 };

    REQUIRE(std::ranges::max(values) == 3);
}
