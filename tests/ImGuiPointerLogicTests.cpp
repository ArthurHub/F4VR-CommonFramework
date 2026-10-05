#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <optional>

#include "imgui/ImGuiPointerLogic.h"

using Catch::Matchers::WithinAbs;
using f4cf::imgui::internal::intersectQuadFront;
using f4cf::imgui::internal::PointerOwner;
using f4cf::imgui::internal::PointerOwnership;
using f4cf::imgui::internal::QuadHit;

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
