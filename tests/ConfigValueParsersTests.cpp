#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "config/ValueParsers.h"

using f4cf::config::parseColor255;
using f4cf::config::parseHandPose;
using f4cf::config::parseTransform;
using f4cf::config::transformTableFromText;

namespace
{
    /**
     * A transform read from the TOML text of its value, as a config has it.
     */
    auto transformOf(const std::string& value)
    {
        const auto table = toml::parse("v = " + value);
        return parseTransform(*table.get("v"));
    }

    /**
     * Why the TOML text of a value is not a transform, or "(read)" when it is one.
     */
    std::string transformErrorOf(const std::string& value)
    {
        const auto transform = transformOf(value);
        return transform ? std::string("(read)") : transform.error();
    }

    // a color as a preset has it before its config value is read: white, at an opacity of its own
    constexpr std::array<float, 4> PRESET{ 1.0f, 1.0f, 1.0f, 0.4f };

    /**
     * A channel of a color as the parser gives it for a number of 0 to 255.
     */
    float channel(const int value)
    {
        return static_cast<float>(value) / 255.0f;
    }
}

TEST_CASE("ConfigValueParsers: a transform is its position, its rotation in degrees and its scale, integers or floats")
{
    const auto transform = transformOf("{ pos = [1.5, -2, 3], rot = [90, 0, -45.5], scale = 0.8 }");
    REQUIRE(transform);
    REQUIRE(transform->x == 1.5f);
    REQUIRE(transform->y == -2.0f);
    REQUIRE(transform->z == 3.0f);
    REQUIRE(transform->heading == 90.0f);
    REQUIRE(transform->roll == 0.0f);
    REQUIRE(transform->attitude == -45.5f);
    REQUIRE(transform->scale == 0.8f);
}

TEST_CASE("ConfigValueParsers: a transform without a key has no move, no rotation or a scale of 1 for it")
{
    const auto transform = transformOf("{ pos = [1, 2, 3] }");
    REQUIRE(transform);
    REQUIRE(transform->x == 1.0f);
    REQUIRE(transform->z == 3.0f);
    REQUIRE(transform->heading == 0.0f);
    REQUIRE(transform->attitude == 0.0f);
    REQUIRE(transform->scale == 1.0f);

    const auto none = transformOf("{}");
    REQUIRE(none);
    REQUIRE(none->x == 0.0f);
    REQUIRE(none->scale == 1.0f);
}

TEST_CASE("ConfigValueParsers: a transform that is not a table, or has a key of another form or name, is malformed")
{
    // the text form of the INI config
    REQUIRE(transformErrorOf("\"1,2,3;4,5,6;7\"") == "it is not a table");
    // an array of two numbers, and of a word
    REQUIRE(transformErrorOf("{ pos = [1, 2] }") == "pos is not an array of three numbers");
    REQUIRE(transformErrorOf("{ rot = [1, \"five\", 3] }") == "rot is not an array of three numbers");
    REQUIRE(transformErrorOf("{ scale = [1, 1, 1] }") == "scale is not a number");
    // a misspelled key, which would read as its default
    REQUIRE(transformErrorOf("{ position = [1, 2, 3] }") == "it has the key 'position', which is not pos, rot or scale");
}

TEST_CASE("ConfigValueParsers: a transform in the INI's text form is the table of its numbers, each as the text has it")
{
    const auto table = transformTableFromText(" 5 , -2 , 1 ; 0 , 0.3 , 0 ; 24.00 ");
    REQUIRE(table);
    REQUIRE(table->is_inline());
    REQUIRE((*table)["pos"][1].value<double>() == -2.0);
    // a double, not the float nearest to 0.3
    REQUIRE((*table)["rot"][1].value<double>() == 0.3);
    REQUIRE((*table)["scale"].value<double>() == 24.0);

    // read back as the transform
    const auto transform = parseTransform(*table);
    REQUIRE(transform);
    REQUIRE(transform->x == 5.0f);
    REQUIRE(transform->roll == 0.3f);
    REQUIRE(transform->scale == 24.0f);
}

TEST_CASE("ConfigValueParsers: a text that lacks a number or a separator is not a transform")
{
    REQUIRE_FALSE(transformTableFromText(""));
    REQUIRE_FALSE(transformTableFromText("none"));
    // no scale
    REQUIRE_FALSE(transformTableFromText("1,2,3;4,5,6"));
    REQUIRE_FALSE(transformTableFromText("1,2,3;4,5,6;"));
    // a comma where the rotation starts
    REQUIRE_FALSE(transformTableFromText("1,2,3,4,5,6;7"));
    // a word in place of a number
    REQUIRE_FALSE(transformTableFromText("1,2,3;4,five,6;7"));
}

TEST_CASE("ConfigValueParsers: a hand pose is four numbers for each finger and two for the palm, in that order")
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

TEST_CASE("ConfigValueParsers: a hand pose with a finger short of a number, or with no palm, is malformed")
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

TEST_CASE("ConfigValueParsers: a color of three channels keeps the opacity it had")
{
    auto color = PRESET;
    REQUIRE(parseColor255("255,128,0", color));
    REQUIRE(color == std::array{ 1.0f, channel(128), 0.0f, 0.4f });
}

TEST_CASE("ConfigValueParsers: the fourth channel of a color is its opacity")
{
    auto color = PRESET;
    REQUIRE(parseColor255("0,64,255,51", color));
    REQUIRE(color == std::array{ 0.0f, channel(64), 1.0f, channel(51) });

    REQUIRE(parseColor255(" 10 , 20 , 30 , 0 ", color));
    REQUIRE(color == std::array{ channel(10), channel(20), channel(30), 0.0f });
}

TEST_CASE("ConfigValueParsers: a malformed color leaves the color as it was")
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
