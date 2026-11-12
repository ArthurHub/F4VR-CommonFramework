#include <catch2/catch_test_macros.hpp>

#include "vrcf/VRControllersHaptic.h"

using namespace f4cf::vrcf;

namespace
{
    constexpr HapticPattern PATTERNS[] = {
        HapticPattern::Tick,
        HapticPattern::Click,
        HapticPattern::DoubleClick,
        HapticPattern::TripleClick,
        HapticPattern::Success,
        HapticPattern::Warning,
        HapticPattern::Error,
        HapticPattern::Notification,
        HapticPattern::Start,
        HapticPattern::Stop,
        HapticPattern::RampUp,
        HapticPattern::RampDown,
        HapticPattern::Heartbeat1,
        HapticPattern::Heartbeat2,
        HapticPattern::Heartbeat3,
        HapticPattern::Buzz,
        HapticPattern::MidBuzz,
        HapticPattern::LongBuzz,
    };
}

TEST_CASE("VRControllersHaptic: a pattern is read by its name, in any letter case and with any separators")
{
    REQUIRE(parseHapticPattern("tick") == HapticPattern::Tick);
    REQUIRE(parseHapticPattern("Double Click") == HapticPattern::DoubleClick);
    REQUIRE(parseHapticPattern("triple_click") == HapticPattern::TripleClick);
    REQUIRE(parseHapticPattern("RAMP-UP") == HapticPattern::RampUp);
    REQUIRE(parseHapticPattern("heartbeat2") == HapticPattern::Heartbeat2);
    REQUIRE(parseHapticPattern(" LongBuzz ") == HapticPattern::LongBuzz);
}

TEST_CASE("VRControllersHaptic: no pattern for the words for off, and for a name that is not one")
{
    for (const auto text : { "", "  ", "none", "OFF", "rumble", "heartbeat4" }) {
        CAPTURE(text);
        REQUIRE_FALSE(parseHapticPattern(text));
    }
}

TEST_CASE("VRControllersHaptic: every pattern has segments that take time, with intensities of 0 to 1")
{
    for (const auto pattern : PATTERNS) {
        CAPTURE(static_cast<int>(pattern));
        const auto segments = VRControllersHaptic::getPattern(pattern);

        REQUIRE_FALSE(segments.empty());
        bool felt = false;
        for (const auto& segment : segments) {
            REQUIRE(segment.duration > 0.0f);
            REQUIRE(segment.startIntensity >= 0.0f);
            REQUIRE(segment.startIntensity <= 1.0f);
            REQUIRE(segment.endIntensity >= 0.0f);
            REQUIRE(segment.endIntensity <= 1.0f);
            felt = felt || segment.startIntensity > 0.0f || segment.endIntensity > 0.0f;
        }
        REQUIRE(felt);

        // it starts and ends with something to feel, a pause only between
        REQUIRE(segments.front().startIntensity + segments.front().endIntensity > 0.0f);
        REQUIRE(segments.back().startIntensity + segments.back().endIntensity > 0.0f);
    }
}

TEST_CASE("VRControllersHaptic: the click patterns are that many taps with pauses between")
{
    const auto taps = [](const HapticPattern pattern) {
        int count = 0;
        for (const auto& segment : VRControllersHaptic::getPattern(pattern)) {
            count += segment.startIntensity > 0.0f ? 1 : 0;
        }
        return count;
    };

    REQUIRE(taps(HapticPattern::Click) == 1);
    REQUIRE(taps(HapticPattern::DoubleClick) == 2);
    REQUIRE(taps(HapticPattern::TripleClick) == 3);
    REQUIRE(VRControllersHaptic::getPattern(HapticPattern::DoubleClick).size() == 3);
    REQUIRE(VRControllersHaptic::getPattern(HapticPattern::TripleClick).size() == 5);
}

TEST_CASE("VRControllersHaptic: nothing plays on a hand until a pattern is triggered on it")
{
    const VRControllersHaptic haptics;

    REQUIRE_FALSE(haptics.isPlaying(vr::TrackedControllerRole_LeftHand));
    REQUIRE_FALSE(haptics.isPlaying(vr::TrackedControllerRole_RightHand));
}
