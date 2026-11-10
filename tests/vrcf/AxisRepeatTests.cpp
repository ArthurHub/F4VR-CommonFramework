#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include "vrcf/AxisRepeat.h"
#include "vrcf/VRControllersManager.h"

using f4cf::vrcf::AxisRepeat;
using f4cf::vrcf::Direction;

namespace
{
    // times and the cooldown are binary fractions, so the sums of a held push are exact
    constexpr float FRAME = 1.0f / 64.0f;
    constexpr float COOLDOWN = 0.125f;
    constexpr float THRESHOLD = 0.85f;

    std::optional<Direction> pushUp(AxisRepeat& repeat, const float now, const float y = 1.0f, const bool accelerate = true)
    {
        return repeat.step(0.0f, y, now, THRESHOLD, COOLDOWN, accelerate, Direction::Any);
    }

    /**
     * The times the stick steps when held fully up from `from` to `to`, checked every frame.
     */
    std::vector<float> holdUp(AxisRepeat& repeat, const float from, const float to, const bool accelerate = true)
    {
        std::vector<float> steps;
        for (float now = from; now <= to; now += FRAME) {
            if (pushUp(repeat, now, 1.0f, accelerate)) {
                steps.push_back(now);
            }
        }
        return steps;
    }

    /**
     * Every wait between two steps that both fall within [from, to].
     */
    std::vector<float> intervalsWithin(const std::vector<float>& steps, const float from, const float to)
    {
        std::vector<float> intervals;
        for (std::size_t i = 1; i < steps.size(); ++i) {
            if (steps[i - 1] >= from && steps[i] <= to) {
                intervals.push_back(steps[i] - steps[i - 1]);
            }
        }
        return intervals;
    }
}

TEST_CASE("AxisRepeat: a fresh push steps at once, and a stick within the threshold never does")
{
    AxisRepeat repeat;
    REQUIRE_FALSE(pushUp(repeat, 0.0f, 0.5f));
    REQUIRE(pushUp(repeat, FRAME) == Direction::Up);
}

TEST_CASE("AxisRepeat: a held push repeats first after the initial delay, then every cooldown")
{
    AxisRepeat repeat;
    const auto steps = holdUp(repeat, 0.0f, 0.9f);

    REQUIRE(steps.size() >= 3);
    REQUIRE(steps[0] == 0.0f);
    // the first multiple of a frame past INITIAL_DELAY
    REQUIRE(steps[1] == 23 * FRAME);
    for (const float interval : intervalsWithin(steps, steps[1], 0.9f)) {
        REQUIRE(interval == COOLDOWN);
    }
}

TEST_CASE("AxisRepeat: the longer a push is held, the faster it repeats")
{
    AxisRepeat repeat;
    const auto steps = holdUp(repeat, 0.0f, 4.0f);

    const auto first = intervalsWithin(steps, 0.36f, 0.99f);
    const auto faster = intervalsWithin(steps, 1.1f, 2.49f);
    const auto fastest = intervalsWithin(steps, 2.6f, 4.0f);
    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(faster.empty());
    REQUIRE_FALSE(fastest.empty());
    for (const float interval : first) {
        REQUIRE(interval == COOLDOWN);
    }
    for (const float interval : faster) {
        REQUIRE(interval == COOLDOWN / 2);
    }
    for (const float interval : fastest) {
        REQUIRE(interval == COOLDOWN / 4);
    }
}

TEST_CASE("AxisRepeat: a push that does not accelerate repeats every cooldown however long it is held")
{
    AxisRepeat repeat;
    const auto steps = holdUp(repeat, 0.0f, 4.0f, false);

    REQUIRE(steps[1] == 23 * FRAME);
    for (const float interval : intervalsWithin(steps, steps[1], 4.0f)) {
        REQUIRE(interval == COOLDOWN);
    }
}

TEST_CASE("AxisRepeat: a hold ends below its share of the threshold, and a stick dipping above it holds on")
{
    AxisRepeat repeat;
    REQUIRE(pushUp(repeat, 0.0f) == Direction::Up);

    SECTION("a dip above the release point is the same hold, which waits out its initial delay")
    {
        REQUIRE_FALSE(pushUp(repeat, FRAME, 0.7f));
        REQUIRE_FALSE(pushUp(repeat, 2 * FRAME));
    }
    SECTION("below the release point the hold ends, and the next push is a fresh one")
    {
        REQUIRE_FALSE(pushUp(repeat, FRAME, 0.5f));
        REQUIRE(pushUp(repeat, 2 * FRAME) == Direction::Up);
    }
}

TEST_CASE("AxisRepeat: a push another way is a fresh push")
{
    AxisRepeat repeat;
    REQUIRE(repeat.step(0.0f, 1.0f, 0.0f, THRESHOLD, COOLDOWN, true, Direction::UpDown) == Direction::Up);
    REQUIRE(repeat.step(0.0f, -1.0f, FRAME, THRESHOLD, COOLDOWN, true, Direction::UpDown) == Direction::Down);
}

TEST_CASE("AxisRepeat: an axis that is not checked for a while is a fresh push when it is")
{
    AxisRepeat repeat;
    REQUIRE(pushUp(repeat, 0.0f) == Direction::Up);

    SECTION("checked again within UNCHECKED_RELEASE, the hold goes on")
    {
        REQUIRE_FALSE(pushUp(repeat, 12 * FRAME));
    }
    SECTION("checked again after it, the push steps at once")
    {
        REQUIRE(pushUp(repeat, 20 * FRAME) == Direction::Up);
    }
}

TEST_CASE("AxisRepeat: a step a caller does not accept is left for one that does")
{
    AxisRepeat repeat;
    REQUIRE_FALSE(repeat.step(1.0f, 0.0f, 0.0f, THRESHOLD, COOLDOWN, true, Direction::Up));
    REQUIRE_FALSE(repeat.step(1.0f, 0.0f, 0.0f, THRESHOLD, COOLDOWN, true, Direction::UpDown));
    REQUIRE(repeat.step(1.0f, 0.0f, 0.0f, THRESHOLD, COOLDOWN, true, Direction::RightLeft) == Direction::Right);
    // taken, the next step of the hold waits its interval for every caller
    REQUIRE_FALSE(repeat.step(1.0f, 0.0f, FRAME, THRESHOLD, COOLDOWN, true, Direction::Right));
}

TEST_CASE("AxisRepeat: a push is read along its dominant component")
{
    REQUIRE(AxisRepeat::pushedDirection(0.5f, 0.6f, 0.4f) == Direction::Up);
    REQUIRE(AxisRepeat::pushedDirection(-0.6f, 0.5f, 0.4f) == Direction::Left);
    REQUIRE(AxisRepeat::pushedDirection(0.3f, -0.9f, 0.85f) == Direction::Down);
    REQUIRE_FALSE(AxisRepeat::pushedDirection(0.6f, 0.6f, 0.85f));
}

TEST_CASE("AxisRepeat: Any, UpDown and RightLeft accept the directions they stand for")
{
    REQUIRE(AxisRepeat::accepts(Direction::Any, Direction::Left));
    REQUIRE(AxisRepeat::accepts(Direction::UpDown, Direction::Down));
    REQUIRE_FALSE(AxisRepeat::accepts(Direction::UpDown, Direction::Right));
    REQUIRE(AxisRepeat::accepts(Direction::RightLeft, Direction::Left));
    REQUIRE_FALSE(AxisRepeat::accepts(Direction::RightLeft, Direction::Up));
    REQUIRE(AxisRepeat::accepts(Direction::Up, Direction::Up));
    REQUIRE_FALSE(AxisRepeat::accepts(Direction::Up, Direction::Down));
}
