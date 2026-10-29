#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <string>
#include <utility>

#include "config/ValueParsers.h"

using f4cf::config::handPoseTomlText;
using f4cf::config::parseColor;
using f4cf::config::parseHandPose;
using f4cf::config::parseHandPoseText;
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
     * A hand pose read from the TOML text of its value, as a config has it.
     */
    auto poseOf(const std::string& value)
    {
        const auto table = toml::parse("v = " + value);
        return parseHandPose(*table.get("v"));
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
    constexpr f4cf::render::Color PRESET{ 1.0f, 1.0f, 1.0f, 0.4f };

    /**
     * A channel of a color as the parser gives it for a number of 0 to 255.
     */
    float channel(const int value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    /**
     * Read the TOML text of a color's value over `color`. Returns why it is not a color, or "(read)" when it is one.
     */
    std::string readColor(const std::string& value, f4cf::render::Color& color)
    {
        const auto table = toml::parse("v = " + value);
        const auto parsed = parseColor(*table.get("v"), color);
        return parsed ? std::string("(read)") : parsed.error();
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
    const auto pose = poseOf(
        "{ thumb = [0.1, 0.2, 0.3, 0.4], index = [1, 2, 3, 4], middle = [5, 6, 7, 8], ring = [9, 10, 11, 12], "
        "pinky = [13, 14, 15, -16], palm = [0.5, -0.25] }");
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

TEST_CASE("ConfigValueParsers: a hand pose without a finger or the palm has zeros for it")
{
    const auto pose = poseOf("{ index = [1, 2, 3, 4] }");
    REQUIRE(pose);

    REQUIRE((*pose)[0] == 0.0f);
    REQUIRE((*pose)[4] == 1.0f);
    REQUIRE((*pose)[7] == 4.0f);
    REQUIRE((*pose)[21] == 0.0f);
}

TEST_CASE("ConfigValueParsers: a hand pose that is not a table, or has a key of another form or name, is malformed")
{
    for (const auto& [value, error] : std::initializer_list<std::pair<std::string, std::string>>{
             // the text form of the INI config
             { "\"0,0,0,0; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 0.5,0.25\"", "it is not a table" },
             // a finger short of a number, and one with a word
             { "{ thumb = [1, 2, 3] }", "thumb is not an array of four numbers, [prox, mid, dist, splay]" },
             { "{ ring = [1, 2, \"three\", 4] }", "ring is not an array of four numbers, [prox, mid, dist, splay]" },
             // the palm with its pitch only
             { "{ palm = [0.5] }", "palm is not an array of two numbers, [pitch, yaw]" },
             // a misspelled finger, which would read as zeros
             { "{ pinkie = [1, 2, 3, 4] }", "it has the key 'pinkie', which is not thumb, index, middle, ring, pinky or palm" },
         }) {
        CAPTURE(value);
        const auto pose = poseOf(value);
        REQUIRE_FALSE(pose);
        REQUIRE(pose.error() == error);
    }
}

TEST_CASE("ConfigValueParsers: a hand pose's table text reads back as the pose, every number as the shortest text of its float")
{
    const auto pose = parseHandPoseText("0.48,1.14,0.97,0.06;0.75,0.56,0.29,0.23;0.17,0.55,0.62,0.03;-0.38,0.87,0.53,0.03;-0.80,0.96,1.14,0.16;15.00,0.00");
    REQUIRE(pose);

    const auto text = handPoseTomlText(*pose);
    REQUIRE(text ==
            "{ thumb = [0.48, 1.14, 0.97, 0.06], index = [0.75, 0.56, 0.29, 0.23], middle = [0.17, 0.55, 0.62, 0.03], ring = [-0.38, 0.87, 0.53, 0.03], "
            "pinky = [-0.8, 0.96, 1.14, 0.16], palm = [15.0, 0.0] }");
    REQUIRE(poseOf(text) == *pose);
}

TEST_CASE("ConfigValueParsers: a hand pose's INI text is four numbers for each finger and two for the palm")
{
    const auto pose = parseHandPoseText("0.1,0.2,0.3,0.4; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,-16; 0.5,-0.25");
    REQUIRE(pose);
    REQUIRE((*pose)[0] == 0.1f);
    REQUIRE((*pose)[19] == -16.0f);
    REQUIRE((*pose)[21] == -0.25f);

    REQUIRE_FALSE(parseHandPoseText(""));
    // the thumb has three numbers
    REQUIRE_FALSE(parseHandPoseText("1,2,3; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 0.5,0.25"));
    // no palm
    REQUIRE_FALSE(parseHandPoseText("0,0,0,0; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16"));
    // the palm has its pitch only
    REQUIRE_FALSE(parseHandPoseText("0,0,0,0; 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 0.5"));
    // the fingers are not apart
    REQUIRE_FALSE(parseHandPoseText("0,0,0,0, 1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16, 0.5,0.25"));
}

TEST_CASE("ConfigValueParsers: a color is its rgb and its alpha, the opacity, each a whole number in 0..255")
{
    auto color = PRESET;
    REQUIRE(readColor("{ rgb = [0, 64, 255], alpha = 51 }", color) == "(read)");
    REQUIRE(color == f4cf::render::Color{ 0.0f, channel(64), 1.0f, channel(51) });
}

TEST_CASE("ConfigValueParsers: a color without a key keeps the channels it had for it")
{
    auto color = PRESET;
    REQUIRE(readColor("{ rgb = [255, 128, 0] }", color) == "(read)");
    REQUIRE(color == f4cf::render::Color{ 1.0f, channel(128), 0.0f, 0.4f });

    // the opacity of a preset alone
    color = PRESET;
    REQUIRE(readColor("{ alpha = 0 }", color) == "(read)");
    REQUIRE(color == f4cf::render::Color{ 1.0f, 1.0f, 1.0f, 0.0f });
}

TEST_CASE("ConfigValueParsers: a color's bytes are the whole numbers it was read from")
{
    auto color = PRESET;
    REQUIRE(readColor("{ rgb = [0, 64, 255], alpha = 51 }", color) == "(read)");
    REQUIRE(color.bytes() == std::array{ 0, 64, 255, 51 });

    // every byte comes back as it was
    for (int value = 0; value <= 255; ++value) {
        CAPTURE(value);
        REQUIRE(f4cf::render::Color::rgba(value, value, value, value).bytes() == std::array{ value, value, value, value });
    }

    // a channel outside 0..1 is the nearest end
    REQUIRE(f4cf::render::Color{ -0.5f, 1.5f, 0.5f, 1.0f }.bytes() == std::array{ 0, 255, 128, 255 });
}

TEST_CASE("ConfigValueParsers: a malformed color leaves the color as it was")
{
    for (const auto& [value, error] : std::initializer_list<std::pair<std::string, std::string>>{
             // the text form of the INI config, and an array with no names
             { "\"255,128,0\"", "it is not a table" },
             { "[255, 128, 0]", "it is not a table" },
             // two channels, and four
             { "{ rgb = [255, 128] }", "rgb is not an array of three numbers" },
             { "{ rgb = [255, 128, 0, 40] }", "rgb is not an array of three numbers" },
             // over 255, under 0, and not a whole number
             { "{ rgb = [256, 0, 0] }", "rgb has a number that is not a whole number in 0..255" },
             { "{ rgb = [0, -1, 0] }", "rgb has a number that is not a whole number in 0..255" },
             { "{ rgb = [0.5, 0, 0] }", "rgb has a number that is not a whole number in 0..255" },
             { "{ alpha = 0.5 }", "alpha is not a whole number in 0..255" },
             // a key misspelled, which would read as its default
             { "{ rgb = [0, 0, 0], a = 40 }", "it has the key 'a', which is not rgb or alpha" },
         }) {
        CAPTURE(value);
        auto color = PRESET;
        REQUIRE(readColor(value, color) == error);
        REQUIRE(color == PRESET);
    }
}
