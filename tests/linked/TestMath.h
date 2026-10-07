#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Comparisons of the game's math types for the tests of this executable: each number within a margin.
namespace test_math
{
    constexpr float MARGIN = 1e-4f;

    inline void requireNear(const float actual, const float expected, const float margin = MARGIN)
    {
        REQUIRE_THAT(actual, Catch::Matchers::WithinAbs(expected, margin));
    }

    inline void requireNear(const RE::NiPoint3& actual, const RE::NiPoint3& expected, const float margin = MARGIN)
    {
        CAPTURE(actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
        REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, margin));
        REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, margin));
        REQUIRE_THAT(actual.z, Catch::Matchers::WithinAbs(expected.z, margin));
    }

    inline void requireNear(const RE::NiMatrix3& actual, const RE::NiMatrix3& expected, const float margin = MARGIN)
    {
        for (std::size_t row = 0; row < 3; row++) {
            for (std::size_t column = 0; column < 3; column++) {
                CAPTURE(row, column);
                REQUIRE_THAT(actual.entry[row][column], Catch::Matchers::WithinAbs(expected.entry[row][column], margin));
            }
        }
    }

    inline void requireNear(const RE::NiTransform& actual, const RE::NiTransform& expected, const float margin = MARGIN)
    {
        requireNear(actual.translate, expected.translate, margin);
        requireNear(actual.rotate, expected.rotate, margin);
        requireNear(actual.scale, expected.scale, margin);
    }
}
