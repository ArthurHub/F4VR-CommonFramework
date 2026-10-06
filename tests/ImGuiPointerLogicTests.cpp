#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <optional>

#include "imgui/ImGuiPointerLogic.h"

using Catch::Matchers::WithinAbs;
using f4cf::imgui::internal::intersectPlaneFront;
using f4cf::imgui::internal::intersectQuadFront;
using f4cf::imgui::internal::PointerHandLatch;
using f4cf::imgui::internal::PointerOwner;
using f4cf::imgui::internal::PointerOwnership;
using f4cf::imgui::internal::QuadHit;
using f4cf::imgui::internal::scrollFromThumbstick;

namespace
{
    struct Vec
    {
        float x;
        float y;
        float z;
    };

    // A quad 4 wide and 2 high that stands 10 away along +Y and faces the origin: x is its right, z is up
    constexpr Vec TOP_LEFT{ -2.0f, 10.0f, 1.0f };
    constexpr Vec TOP_RIGHT{ 2.0f, 10.0f, 1.0f };
    constexpr Vec BOTTOM_LEFT{ -2.0f, 10.0f, -1.0f };
    constexpr Vec ORIGIN{ 0.0f, 0.0f, 0.0f };

    std::optional<QuadHit> hitFrom(const Vec& origin, const Vec& direction)
    {
        return intersectQuadFront(origin, direction, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT);
    }

    constexpr f4cf::imgui::internal::PointerHandInput AWAY{ .onCanvas = false, .down = false };
    constexpr f4cf::imgui::internal::PointerHandInput AWAY_DOWN{ .onCanvas = false, .down = true };
    constexpr f4cf::imgui::internal::PointerHandInput ON{ .onCanvas = true, .down = false };
    constexpr f4cf::imgui::internal::PointerHandInput ON_DOWN{ .onCanvas = true, .down = true };
}

TEST_CASE("a ray at the middle of a quad meets it at half its width and height", "[imgui][pointer]")
{
    const auto hit = hitFrom(ORIGIN, Vec{ 0.0f, 1.0f, 0.0f });
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(0.5, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(0.5, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(10.0, 1e-4));
}

TEST_CASE("a hit is measured from the top left corner of the quad", "[imgui][pointer]")
{
    // meets the quad half a unit right of its left edge and half a unit under its top edge
    const auto hit = hitFrom(ORIGIN, Vec{ -1.5f, 10.0f, 0.5f });
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(0.125, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(0.25, 1e-5));
    // in lengths of the direction, which is not of length 1 here
    CHECK_THAT(hit->distance, WithinAbs(1.0, 1e-5));
}

TEST_CASE("a ray that does not start at the origin meets the quad where it points", "[imgui][pointer]")
{
    const auto hit = hitFrom(Vec{ 1.0f, 4.0f, -0.5f }, Vec{ 0.0f, 1.0f, 0.0f });
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(0.75, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(0.75, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(6.0, 1e-4));
}

TEST_CASE("a quad that faces another way is met from its own front", "[imgui][pointer]")
{
    // stands 5 away along +X and faces the origin, so its right is -Y
    const Vec topLeft{ 5.0f, 1.0f, 1.0f };
    const Vec topRight{ 5.0f, -1.0f, 1.0f };
    const Vec bottomLeft{ 5.0f, 1.0f, -1.0f };

    const auto hit = intersectQuadFront(ORIGIN, Vec{ 5.0f, 0.5f, -0.5f }, topLeft, topRight, bottomLeft);
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(0.25, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(0.75, 1e-5));

    CHECK_FALSE(intersectQuadFront(Vec{ 10.0f, 0.0f, 0.0f }, Vec{ -1.0f, 0.0f, 0.0f }, topLeft, topRight, bottomLeft));
}

TEST_CASE("a ray that passes beside a quad does not meet it", "[imgui][pointer]")
{
    CHECK_FALSE(hitFrom(ORIGIN, Vec{ 2.1f, 10.0f, 0.0f }));
    CHECK_FALSE(hitFrom(ORIGIN, Vec{ -2.1f, 10.0f, 0.0f }));
    CHECK_FALSE(hitFrom(ORIGIN, Vec{ 0.0f, 10.0f, 1.1f }));
    CHECK_FALSE(hitFrom(ORIGIN, Vec{ 0.0f, 10.0f, -1.1f }));
}

TEST_CASE("a ray does not meet a quad from behind, along it or past it", "[imgui][pointer]")
{
    // from behind
    CHECK_FALSE(hitFrom(Vec{ 0.0f, 20.0f, 0.0f }, Vec{ 0.0f, -1.0f, 0.0f }));
    // along the quad
    CHECK_FALSE(hitFrom(ORIGIN, Vec{ 1.0f, 0.0f, 0.0f }));
    // pointing away from it
    CHECK_FALSE(hitFrom(ORIGIN, Vec{ 0.0f, -1.0f, 0.0f }));
    // starting past it
    CHECK_FALSE(hitFrom(Vec{ 0.0f, 11.0f, 0.0f }, Vec{ 0.0f, 1.0f, 0.0f }));
}

TEST_CASE("a quad with no width or height is not met", "[imgui][pointer]")
{
    CHECK_FALSE(intersectQuadFront(ORIGIN, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_LEFT, BOTTOM_LEFT));
    CHECK_FALSE(intersectQuadFront(ORIGIN, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, TOP_LEFT));
}

TEST_CASE("a ray that passes beside a quad meets its plane there", "[imgui][pointer]")
{
    // one unit past the right edge and half a unit over the top edge
    const auto hit = intersectPlaneFront(ORIGIN, Vec{ 3.0f, 10.0f, 1.5f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT);
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(1.25, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(-0.25, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(1.0, 1e-5));
}

TEST_CASE("a ray does not meet the plane of a quad from behind, along it or past it", "[imgui][pointer]")
{
    CHECK_FALSE(intersectPlaneFront(Vec{ 5.0f, 20.0f, 0.0f }, Vec{ 0.0f, -1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT));
    CHECK_FALSE(intersectPlaneFront(ORIGIN, Vec{ 1.0f, 0.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT));
    CHECK_FALSE(intersectPlaneFront(Vec{ 5.0f, 11.0f, 0.0f }, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT));
}

TEST_CASE("a hand operates the UI while its ray is on a canvas", "[imgui][pointer]")
{
    PointerHandLatch latch;
    CHECK_FALSE(latch.operates());

    latch.update(ON);
    CHECK(latch.operates());
    CHECK_FALSE(latch.pressing());

    latch.update(AWAY);
    CHECK_FALSE(latch.operates());
}

TEST_CASE("a trigger pulled on a canvas presses the UI until it is released", "[imgui][pointer]")
{
    PointerHandLatch latch;
    latch.update(ON);
    latch.update(ON_DOWN);
    CHECK(latch.operates());
    CHECK(latch.pressing());

    latch.update(ON);
    CHECK(latch.operates());
    CHECK_FALSE(latch.pressing());
}

TEST_CASE("a press is the UI's until it is released, also with the ray off the canvas", "[imgui][pointer]")
{
    PointerHandLatch latch;
    latch.update(ON);
    latch.update(ON_DOWN);

    // the ray slides off the canvas in the middle of the press
    latch.update(AWAY_DOWN);
    CHECK(latch.operates());
    CHECK(latch.pressing());

    // and comes back
    latch.update(ON_DOWN);
    CHECK(latch.pressing());

    // released off the canvas: the hand is the game's again, with its trigger up
    latch.update(AWAY_DOWN);
    latch.update(AWAY);
    CHECK_FALSE(latch.operates());
    CHECK_FALSE(latch.pressing());
}

TEST_CASE("a trigger that is down when the ray comes to a canvas is the game's until it is released", "[imgui][pointer]")
{
    PointerHandLatch latch;
    latch.update(AWAY_DOWN);
    CHECK_FALSE(latch.operates());

    // on the canvas with the pull of before: no press, and the hand is not taken from the game
    latch.update(ON_DOWN);
    CHECK_FALSE(latch.operates());
    CHECK_FALSE(latch.pressing());
    latch.update(ON_DOWN);
    CHECK_FALSE(latch.operates());

    // released on the canvas: from here the hand operates the UI
    latch.update(ON);
    CHECK(latch.operates());
    CHECK_FALSE(latch.pressing());
    latch.update(ON_DOWN);
    CHECK(latch.pressing());
}

TEST_CASE("a pull that passes over a canvas stays the game's", "[imgui][pointer]")
{
    PointerHandLatch latch;
    latch.update(AWAY_DOWN);
    latch.update(ON_DOWN);
    latch.update(AWAY_DOWN);
    CHECK_FALSE(latch.operates());

    // back on the canvas, still the same pull
    latch.update(ON_DOWN);
    CHECK_FALSE(latch.operates());
    CHECK_FALSE(latch.pressing());
}

TEST_CASE("a trigger pulled in the frame its ray comes to a canvas is the game's", "[imgui][pointer]")
{
    PointerHandLatch latch;
    latch.update(AWAY);
    latch.update(ON_DOWN);
    CHECK_FALSE(latch.operates());
    CHECK_FALSE(latch.pressing());
}

TEST_CASE("a thumbstick inside its dead zone scrolls nothing", "[imgui][pointer]")
{
    CHECK(scrollFromThumbstick(0.0f, 0.2f) == 0.0f);
    CHECK(scrollFromThumbstick(0.1f, 0.2f) == 0.0f);
    CHECK(scrollFromThumbstick(-0.19f, 0.2f) == 0.0f);
    CHECK(scrollFromThumbstick(0.2f, 0.2f) == 0.0f);
}

TEST_CASE("a thumbstick scrolls evenly from the edge of its dead zone to a full push", "[imgui][pointer]")
{
    CHECK_THAT(scrollFromThumbstick(0.21f, 0.2f), WithinAbs(0.0125, 1e-5));
    CHECK_THAT(scrollFromThumbstick(0.6f, 0.2f), WithinAbs(0.5, 1e-5));
    CHECK_THAT(scrollFromThumbstick(1.0f, 0.2f), WithinAbs(1.0, 1e-5));
    // a push that reads past its end scrolls no faster
    CHECK_THAT(scrollFromThumbstick(1.2f, 0.2f), WithinAbs(1.0, 1e-5));
}

TEST_CASE("a thumbstick pushed the other way scrolls the other way", "[imgui][pointer]")
{
    CHECK_THAT(scrollFromThumbstick(-0.6f, 0.2f), WithinAbs(-0.5, 1e-5));
    CHECK_THAT(scrollFromThumbstick(-1.0f, 0.2f), WithinAbs(-1.0, 1e-5));
}

TEST_CASE("a push that is not a number, or a dead zone that leaves no room, scrolls nothing", "[imgui][pointer]")
{
    CHECK(scrollFromThumbstick(std::numeric_limits<float>::quiet_NaN(), 0.2f) == 0.0f);
    CHECK(scrollFromThumbstick(1.0f, 1.0f) == 0.0f);
}

TEST_CASE("no hand owns the pointer while no ray is on a canvas", "[imgui][pointer]")
{
    PointerOwnership ownership;
    CHECK(ownership.update(AWAY, AWAY) == PointerOwner::None);
    CHECK(ownership.update(AWAY_DOWN, AWAY_DOWN) == PointerOwner::None);
}

TEST_CASE("a hand whose ray is alone on a canvas owns the pointer", "[imgui][pointer]")
{
    PointerOwnership ownership;
    CHECK(ownership.update(ON, AWAY) == PointerOwner::Primary);
    CHECK(ownership.update(AWAY, ON) == PointerOwner::Offhand);
    CHECK(ownership.update(AWAY, AWAY) == PointerOwner::None);
}

TEST_CASE("with both rays on a canvas the primary hand owns the pointer until the offhand presses", "[imgui][pointer]")
{
    PointerOwnership ownership;
    CHECK(ownership.update(ON, ON) == PointerOwner::Primary);
    CHECK(ownership.update(ON, ON_DOWN) == PointerOwner::Offhand);
    // it stays with the hand that pressed last
    CHECK(ownership.update(ON, ON) == PointerOwner::Offhand);
    CHECK(ownership.update(ON_DOWN, ON) == PointerOwner::Primary);
    CHECK(ownership.update(ON, ON) == PointerOwner::Primary);
}

TEST_CASE("the hand that pressed last gets the pointer back when its ray returns", "[imgui][pointer]")
{
    PointerOwnership ownership;
    ownership.update(ON, ON_DOWN);
    ownership.update(ON, ON);
    CHECK(ownership.update(ON, AWAY) == PointerOwner::Primary);
    CHECK(ownership.update(ON, ON) == PointerOwner::Offhand);
}

TEST_CASE("a hand that presses alone on a canvas is the one that pressed last", "[imgui][pointer]")
{
    PointerOwnership ownership;
    CHECK(ownership.update(AWAY, ON_DOWN) == PointerOwner::Offhand);
    CHECK(ownership.update(AWAY, ON) == PointerOwner::Offhand);
    CHECK(ownership.update(ON, ON) == PointerOwner::Offhand);
}

TEST_CASE("the owner keeps the pointer while it holds its trigger down", "[imgui][pointer]")
{
    PointerOwnership ownership;
    CHECK(ownership.update(ON_DOWN, ON) == PointerOwner::Primary);
    // the offhand presses in the middle of the primary hand's drag
    CHECK(ownership.update(ON_DOWN, ON_DOWN) == PointerOwner::Primary);
    // and does not get the pointer when the drag ends: its press was not on a pointer of its own
    CHECK(ownership.update(ON, ON_DOWN) == PointerOwner::Primary);
    CHECK(ownership.update(ON, ON) == PointerOwner::Primary);
}

TEST_CASE("a press away from the canvases does not take the pointer", "[imgui][pointer]")
{
    PointerOwnership ownership;
    CHECK(ownership.update(ON, AWAY_DOWN) == PointerOwner::Primary);
    // the offhand's ray reaches a canvas with its trigger already down
    CHECK(ownership.update(ON, ON_DOWN) == PointerOwner::Primary);
    CHECK(ownership.update(ON, ON) == PointerOwner::Primary);
}

TEST_CASE("when both hands press in one frame and neither owns the pointer, the primary hand gets it", "[imgui][pointer]")
{
    PointerOwnership ownership;
    // the offhand pressed last
    ownership.update(AWAY, ON_DOWN);
    ownership.update(AWAY, AWAY);
    CHECK(ownership.update(ON_DOWN, ON_DOWN) == PointerOwner::Primary);
}
