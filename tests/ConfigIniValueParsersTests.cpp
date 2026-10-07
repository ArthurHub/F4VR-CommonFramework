#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "config/IniValueParsers.h"

using f4cf::config::parseColor255;
using f4cf::config::parseHandPose;
using f4cf::config::parseTransform;
using f4cf::config::takeName;

namespace
{
    // a color as a preset has it before its INI value is read: white, at an opacity of its own
    constexpr std::array<float, 4> PRESET{ 1.0f, 1.0f, 1.0f, 0.4f };

    /**
     * A channel of a color as the parser gives it for a number of 0 to 255.
     */
    float channel(const int value)
    {
        return static_cast<float>(value) / 255.0f;
    }
}

TEST_CASE("ConfigIniValueParsers: a transform is its position, its rotation in degrees and its scale")
{
    const auto transform = parseTransform("1.5,-2,3;90,0,-45.5;0.8");
    REQUIRE(transform);
    REQUIRE(transform->x == 1.5f);
    REQUIRE(transform->y == -2.0f);
    REQUIRE(transform->z == 3.0f);
    REQUIRE(transform->heading == 90.0f);
    REQUIRE(transform->roll == 0.0f);
    REQUIRE(transform->attitude == -45.5f);
    REQUIRE(transform->scale == 0.8f);
}

TEST_CASE("ConfigIniValueParsers: a transform is read with spaces around its numbers")
{
    const auto transform = parseTransform("  1 , 2 , 3 ; 4 , 5 , 6 ; 7  ");
    REQUIRE(transform);
    REQUIRE(transform->x == 1.0f);
    REQUIRE(transform->z == 3.0f);
    REQUIRE(transform->heading == 4.0f);
    REQUIRE(transform->attitude == 6.0f);
    REQUIRE(transform->scale == 7.0f);
}

TEST_CASE("ConfigIniValueParsers: a transform that lacks a number or a separator is malformed")
{
    REQUIRE_FALSE(parseTransform(""));
    REQUIRE_FALSE(parseTransform("none"));
    // no scale
    REQUIRE_FALSE(parseTransform("1,2,3;4,5,6"));
    REQUIRE_FALSE(parseTransform("1,2,3;4,5,6;"));
    // a comma where the rotation starts
    REQUIRE_FALSE(parseTransform("1,2,3,4,5,6;7"));
    // a word in place of a number
    REQUIRE_FALSE(parseTransform("1,2,3;4,five,6;7"));
}

TEST_CASE("ConfigIniValueParsers: a hand pose is four numbers for each finger and two for the palm, in that order")
{
    const auto pose = parseHandPose("0.1,0.2,0.3,0.4; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,-16; 0.5,-0.25");
    REQUIRE(pose);

    // the thumb first
    REQUIRE((*pose)[0] == 0.1f);
    REQUIRE((*pose)[3] == 0.4f);
    // the index, and the pinky last of the fingers
    REQUIRE((*pose)[4] == 1.0f);
    REQUIRE((*pose)[16] == 13.0f);
    REQUIRE((*pose)[19] == -16.0f);
    // the palm's pitch and yaw
    REQUIRE((*pose)[20] == 0.5f);
    REQUIRE((*pose)[21] == -0.25f);
}

TEST_CASE("ConfigIniValueParsers: a hand pose with a finger short of a number, or with no palm, is malformed")
{
    REQUIRE_FALSE(parseHandPose(""));
    // the thumb has three numbers
    REQUIRE_FALSE(parseHandPose("1,2,3; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 0.5,0.25"));
    // no palm
    REQUIRE_FALSE(parseHandPose("0,0,0,0; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16"));
    // the palm has its pitch only
    REQUIRE_FALSE(parseHandPose("0,0,0,0; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 0.5"));
    // the fingers are not apart
    REQUIRE_FALSE(parseHandPose("0,0,0,0, 1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16, 0.5,0.25"));
}

TEST_CASE("ConfigIniValueParsers: a color of three channels keeps the opacity it had")
{
    auto color = PRESET;
    REQUIRE(parseColor255("255,128,0", color));
    REQUIRE(color == std::array{ 1.0f, channel(128), 0.0f, 0.4f });
}

TEST_CASE("ConfigIniValueParsers: the fourth channel of a color is its opacity")
{
    auto color = PRESET;
    REQUIRE(parseColor255("0,64,255,51", color));
    REQUIRE(color == std::array{ 0.0f, channel(64), 1.0f, channel(51) });

    REQUIRE(parseColor255(" 10 , 20 , 30 , 0 ", color));
    REQUIRE(color == std::array{ channel(10), channel(20), channel(30), 0.0f });
}

TEST_CASE("ConfigIniValueParsers: a malformed color leaves the color as it was")
{
    for (const std::string text : {
             "",
             "red",
             // two channels, and five
             "255,128",
             "1,2,3,4,5",
             // over 255 and under 0
             "256,0,0",
             "0,-1,0",
             // not a whole number
             "0.5,0,0",
             "255,128,0x",
             "255,12 8,0",
         }) {
        CAPTURE(text);
        auto color = PRESET;
        REQUIRE_FALSE(parseColor255(text, color));
        REQUIRE(color == PRESET);
    }
}

TEST_CASE("ConfigIniValueParsers: a name is taken out of a list of names, once")
{
    std::string names = "skelly, geometry";

    REQUIRE(takeName(names, "geometry"));
    REQUIRE(names == "skelly, ");
    REQUIRE_FALSE(takeName(names, "geometry"));

    REQUIRE(takeName(names, "skelly"));
    REQUIRE(names == ", ");
}

TEST_CASE("ConfigIniValueParsers: a name is taken only as a whole, in whatever order the names are asked for")
{
    std::string names = "fp_skelly";
    REQUIRE_FALSE(takeName(names, "skelly"));
    REQUIRE_FALSE(takeName(names, "fp"));
    REQUIRE(names == "fp_skelly");

    names = "perf_reset, fp_skelly, skelly";
    REQUIRE_FALSE(takeName(names, "perf"));
    REQUIRE(takeName(names, "skelly"));
    REQUIRE(names == "perf_reset, fp_skelly, ");
    REQUIRE(takeName(names, "fp_skelly"));
    REQUIRE(takeName(names, "perf_reset"));
}

TEST_CASE("ConfigIniValueParsers: the names of a list can have any separator")
{
    for (const std::string separator : { ",", ", ", " ", ";", "|", " + " }) {
        CAPTURE(separator);
        std::string names = "skelly" + separator + "world" + separator + "perf";

        REQUIRE(takeName(names, "world"));
        REQUIRE(names == "skelly" + separator + separator + "perf");
        REQUIRE(takeName(names, "perf"));
        REQUIRE(takeName(names, "skelly"));
    }
}

TEST_CASE("ConfigIniValueParsers: a name that the list does not have leaves the list as it was")
{
    std::string names = "skelly, world";
    REQUIRE_FALSE(takeName(names, "geometry"));
    // the letter case is part of a name
    REQUIRE_FALSE(takeName(names, "Skelly"));
    REQUIRE_FALSE(takeName(names, ""));
    REQUIRE(names == "skelly, world");

    std::string none;
    REQUIRE_FALSE(takeName(none, "skelly"));
}
