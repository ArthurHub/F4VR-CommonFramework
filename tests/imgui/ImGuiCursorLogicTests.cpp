#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>

#include "imgui/ImGuiCursorLogic.h"

using Catch::Matchers::WithinAbs;
using f4cf::imgui::internal::canvasUnderCursor;
using f4cf::imgui::internal::ScreenRect;

TEST_CASE("the cursor on a canvas is measured from its top left corner", "[imgui][cursor]")
{
    const std::array canvases{ ScreenRect{ .left = 100.0f, .top = 50.0f, .width = 400.0f, .height = 300.0f } };
    const auto hit = canvasUnderCursor(canvases, 150.0f, 80.0f);
    REQUIRE(hit);
    CHECK(hit->index == 0);
    CHECK_THAT(hit->x, WithinAbs(50.0f, 1e-4));
    CHECK_THAT(hit->y, WithinAbs(30.0f, 1e-4));
}

TEST_CASE("the cursor off every canvas is on none", "[imgui][cursor]")
{
    const std::array canvases{ ScreenRect{ .left = 100.0f, .top = 50.0f, .width = 400.0f, .height = 300.0f } };
    CHECK_FALSE(canvasUnderCursor(canvases, 99.0f, 80.0f));
    CHECK_FALSE(canvasUnderCursor(canvases, 150.0f, 400.0f));
    CHECK_FALSE(canvasUnderCursor({}, 150.0f, 80.0f));
}

TEST_CASE("a canvas holds its left and top edges and not its right and bottom ones", "[imgui][cursor]")
{
    const std::array canvases{ ScreenRect{ .left = 0.0f, .top = 0.0f, .width = 100.0f, .height = 100.0f },
        ScreenRect{ .left = 100.0f, .top = 0.0f, .width = 100.0f, .height = 100.0f } };
    const auto onEdge = canvasUnderCursor(canvases, 100.0f, 0.0f);
    REQUIRE(onEdge);
    CHECK(onEdge->index == 1);
    CHECK_THAT(onEdge->x, WithinAbs(0.0f, 1e-4));
    CHECK_FALSE(canvasUnderCursor(canvases, 50.0f, 100.0f));
}

TEST_CASE("of canvases that overlap, the cursor is on the one drawn last", "[imgui][cursor]")
{
    const std::array canvases{ ScreenRect{ .left = 0.0f, .top = 0.0f, .width = 300.0f, .height = 300.0f },
        ScreenRect{ .left = 100.0f, .top = 100.0f, .width = 100.0f, .height = 100.0f } };
    const auto overlap = canvasUnderCursor(canvases, 150.0f, 150.0f);
    REQUIRE(overlap);
    CHECK(overlap->index == 1);
    CHECK_THAT(overlap->x, WithinAbs(50.0f, 1e-4));

    const auto under = canvasUnderCursor(canvases, 50.0f, 50.0f);
    REQUIRE(under);
    CHECK(under->index == 0);
}
