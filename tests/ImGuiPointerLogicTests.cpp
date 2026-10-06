#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

#include "imgui/ImGuiPointerLogic.h"

using Catch::Matchers::WithinAbs;
using f4cf::imgui::internal::CurvedQuad;
using f4cf::imgui::internal::intersectPlaneFront;
using f4cf::imgui::internal::intersectQuadFront;
using f4cf::imgui::internal::intersectSurfaceFront;
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

    // curved at its distance from the origin, so the axis it is bent around is upright through the origin
    constexpr float CURVE_RADIUS = 10.0f;

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

TEST_CASE("a curved quad keeps its middle where it is, and its sides come toward its front", "[imgui][pointer]")
{
    const CurvedQuad curve(TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);

    const Vec topMiddle = curve.pointAt(0.5f, 0.0f);
    CHECK_THAT(topMiddle.x, WithinAbs(0.0, 1e-5));
    CHECK_THAT(topMiddle.y, WithinAbs(10.0, 1e-5));
    CHECK_THAT(topMiddle.z, WithinAbs(1.0, 1e-5));

    // the width of 4 is kept along the curve: 0.2 of a radian to each side at a radius of 10
    CHECK_THAT(curve.arc(), WithinAbs(0.4, 1e-6));
    const Vec left = curve.pointAt(0.0f, 0.5f);
    CHECK_THAT(left.x, WithinAbs(-10.0 * std::sin(0.2), 1e-5));
    CHECK_THAT(left.y, WithinAbs(10.0 * std::cos(0.2), 1e-5));
    CHECK_THAT(left.z, WithinAbs(0.0, 1e-5));
    const Vec bottomRight = curve.pointAt(1.0f, 1.0f);
    CHECK_THAT(bottomRight.x, WithinAbs(10.0 * std::sin(0.2), 1e-5));
    CHECK_THAT(bottomRight.y, WithinAbs(10.0 * std::cos(0.2), 1e-5));
    CHECK_THAT(bottomRight.z, WithinAbs(-1.0, 1e-5));
}

TEST_CASE("every point of a curved quad is at the radius from the axis", "[imgui][pointer]")
{
    // the axis is upright through the origin: the quad's front faces it from 10 away, which is the radius
    const CurvedQuad curve(TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);
    for (const float u : { 0.0f, 0.2f, 0.5f, 0.9f, 1.0f }) {
        const Vec point = curve.pointAt(u, 0.3f);
        CHECK_THAT(std::hypot(point.x, point.y), WithinAbs(10.0, 1e-4));
    }
}

TEST_CASE("the width of a curved quad runs along the curve", "[imgui][pointer]")
{
    const CurvedQuad curve(TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);

    const Vec middle = curve.rightAt(0.5f);
    CHECK_THAT(middle.x, WithinAbs(1.0, 1e-5));
    CHECK_THAT(middle.y, WithinAbs(0.0, 1e-5));
    CHECK_THAT(middle.z, WithinAbs(0.0, 1e-5));

    // at the right side it turns toward the front, and stays square to the line from the axis
    const Vec side = curve.rightAt(1.0f);
    CHECK_THAT(side.x, WithinAbs(std::cos(0.2), 1e-5));
    CHECK_THAT(side.y, WithinAbs(-std::sin(0.2), 1e-5));
    CHECK_THAT(side.z, WithinAbs(0.0, 1e-5));
    const Vec point = curve.pointAt(1.0f, 0.5f);
    CHECK_THAT(side.x * point.x + side.y * point.y, WithinAbs(0.0, 1e-4));
}

TEST_CASE("a quad that faces another way is bent toward its own front", "[imgui][pointer]")
{
    // stands 5 away along +X and faces the origin, so its right is -Y
    const CurvedQuad curve(Vec{ 5.0f, 1.0f, 1.0f }, Vec{ 5.0f, -1.0f, 1.0f }, Vec{ 5.0f, 1.0f, -1.0f }, 5.0f);

    const Vec left = curve.pointAt(0.0f, 0.5f);
    CHECK_THAT(left.x, WithinAbs(5.0 * std::cos(0.2), 1e-5));
    CHECK_THAT(left.y, WithinAbs(5.0 * std::sin(0.2), 1e-5));
    CHECK_THAT(left.z, WithinAbs(0.0, 1e-5));
}

TEST_CASE("a ray meets a curved quad where the quad is drawn", "[imgui][pointer]")
{
    const CurvedQuad curve(TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);

    // from the axis, from beside it inside the cylinder, and from outside the cylinder on the side of the front
    for (const Vec& origin : { ORIGIN, Vec{ 3.0f, 4.0f, -0.5f }, Vec{ -2.0f, -15.0f, 0.8f } }) {
        for (const float u : { 0.0f, 0.25f, 0.5f, 0.8f, 1.0f }) {
            for (const float v : { 0.0f, 0.4f, 1.0f }) {
                // the surface and not the quad: a point of the quad's edge can be met a rounding beside it
                const Vec point = curve.pointAt(u, v);
                const auto hit = intersectSurfaceFront(origin, Vec{ point.x - origin.x, point.y - origin.y, point.z - origin.z }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);
                REQUIRE(hit);
                CHECK_THAT(hit->u, WithinAbs(u, 1e-4));
                CHECK_THAT(hit->v, WithinAbs(v, 1e-4));
                // in lengths of the direction, which runs from the origin to the point
                CHECK_THAT(hit->distance, WithinAbs(1.0, 1e-4));
            }
        }
    }
}

TEST_CASE("a ray from the axis meets a curved quad at the radius, by the angle it is turned", "[imgui][pointer]")
{
    // turned 0.1 of a radian to the right, of the 0.2 the quad takes up to each side
    const auto hit = intersectQuadFront(ORIGIN, Vec{ std::sin(0.1f), std::cos(0.1f), 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(0.75, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(0.5, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(10.0, 1e-4));
}

TEST_CASE("a ray that passes beside a curved quad does not meet it, and meets its cylinder there", "[imgui][pointer]")
{
    // 0.3 of a radian to the right, past the quad's 0.2, and half a unit over its top edge
    const Vec beside{ 10.0f * std::sin(0.3f), 10.0f * std::cos(0.3f), 1.5f };
    CHECK_FALSE(intersectQuadFront(ORIGIN, beside, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS));

    const auto hit = intersectSurfaceFront(ORIGIN, beside, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS);
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(1.25, 1e-5));
    CHECK_THAT(hit->v, WithinAbs(-0.25, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(1.0, 1e-5));
}

TEST_CASE("a ray does not meet a curved quad from behind, along its axis or past it", "[imgui][pointer]")
{
    // from behind: it passes through the quad and meets the cylinder across from it, far beside the quad
    CHECK_FALSE(intersectQuadFront(Vec{ 0.0f, 20.0f, 0.0f }, Vec{ 0.0f, -1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS));
    // along the axis
    CHECK_FALSE(intersectSurfaceFront(ORIGIN, Vec{ 0.0f, 0.0f, 1.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS));
    // starting past it
    CHECK_FALSE(intersectSurfaceFront(Vec{ 0.0f, 11.0f, 0.0f }, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS));
    // from outside the cylinder, passing beside it
    CHECK_FALSE(intersectSurfaceFront(Vec{ 11.0f, -20.0f, 0.0f }, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, CURVE_RADIUS));
}

TEST_CASE("a ray from outside passes through the near part of a curved quad and meets the inside of the far part", "[imgui][pointer]")
{
    // half of a cylinder of radius 10 around the upright axis through the origin, open toward -Y
    const float halfWidth = 5.0f * std::numbers::pi_v<float>;
    const Vec topLeft{ -halfWidth, 10.0f, 1.0f };
    const Vec topRight{ halfWidth, 10.0f, 1.0f };
    const Vec bottomLeft{ -halfWidth, 10.0f, -1.0f };

    // comes to the left part from behind at 60 degrees left of the middle, and leaves at 60 degrees right of it
    const auto hit = intersectQuadFront(Vec{ -20.0f, 5.0f, 0.0f }, Vec{ 1.0f, 0.0f, 0.0f }, topLeft, topRight, bottomLeft, CURVE_RADIUS);
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(0.5 + 1.0 / 3.0, 1e-4));
    CHECK_THAT(hit->v, WithinAbs(0.5, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(20.0 + std::sqrt(75.0), 1e-3));
}

TEST_CASE("a curved quad is at most half of its cylinder", "[imgui][pointer]")
{
    // a radius too small for the width of 4 is taken as 4 over pi
    const CurvedQuad curve(TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, 0.1f);
    const float radius = 4.0f / std::numbers::pi_v<float>;
    CHECK_THAT(curve.arc(), WithinAbs(std::numbers::pi, 1e-5));

    // its sides are level with the axis, which is the radius in front of the middle
    const Vec left = curve.pointAt(0.0f, 0.5f);
    CHECK_THAT(left.x, WithinAbs(-radius, 1e-4));
    CHECK_THAT(left.y, WithinAbs(10.0 - radius, 1e-4));
    const Vec right = curve.pointAt(1.0f, 0.5f);
    CHECK_THAT(right.x, WithinAbs(radius, 1e-4));
    CHECK_THAT(right.y, WithinAbs(10.0 - radius, 1e-4));
}

TEST_CASE("a curved quad with no width or height is not met", "[imgui][pointer]")
{
    CHECK_FALSE(intersectSurfaceFront(ORIGIN, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_LEFT, BOTTOM_LEFT, CURVE_RADIUS));
    CHECK_FALSE(intersectSurfaceFront(ORIGIN, Vec{ 0.0f, 1.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, TOP_LEFT, CURVE_RADIUS));
}

TEST_CASE("a quad with no curve radius is met on its plane", "[imgui][pointer]")
{
    // one unit past the right edge: on the plane that is 10 away there, where the cylinder is nearer
    const auto hit = intersectSurfaceFront(ORIGIN, Vec{ 3.0f, 10.0f, 0.0f }, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, 0.0f);
    REQUIRE(hit);
    CHECK_THAT(hit->u, WithinAbs(1.25, 1e-5));
    CHECK_THAT(hit->distance, WithinAbs(1.0, 1e-5));
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
