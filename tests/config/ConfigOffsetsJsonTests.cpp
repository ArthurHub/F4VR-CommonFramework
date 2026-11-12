#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "config/OffsetsJson.h"

using f4cf::config::readOffsetsJson;
using f4cf::config::writeOffsetsJson;

namespace
{
    /**
     * A transform with the members of the game's NiTransform that an offsets JSON holds: the rotation matrix in 3
     * rows of 4, the position and the scale.
     */
    struct Transform
    {
        struct Position
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        float rotate[3][4]{};
        Position translate;
        float scale = 0.0f;
    };

    using Offsets = std::unordered_map<std::string, Transform>;

    // An offsets file as a mod ships it, with numbers that a float holds exactly
    const std::string PIPBOY = R"({
    "PipboyPosition": {
        "rotation": [
            0.5,
            -0.25,
            0.125,
            0.0,
            1.5,
            2.5,
            3.5,
            0.0,
            -4.0,
            5.0,
            6.0,
            0.0
        ],
        "scale": 1.75,
        "x": 9.5,
        "y": -0.5,
        "z": -7.25
    }
})";

    /**
     * The transform of the file above.
     */
    Transform pipboyTransform()
    {
        Transform transform;
        const float rotation[3][4] = { { 0.5f, -0.25f, 0.125f, 0.0f }, { 1.5f, 2.5f, 3.5f, 0.0f }, { -4.0f, 5.0f, 6.0f, 0.0f } };
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                transform.rotate[i][j] = rotation[i][j];
            }
        }
        transform.translate = { 9.5f, -0.5f, -7.25f };
        transform.scale = 1.75f;
        return transform;
    }

    void requireSame(const Transform& a, const Transform& b)
    {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                CAPTURE(i, j);
                REQUIRE(a.rotate[i][j] == b.rotate[i][j]);
            }
        }
        REQUIRE(a.translate.x == b.translate.x);
        REQUIRE(a.translate.y == b.translate.y);
        REQUIRE(a.translate.z == b.translate.z);
        REQUIRE(a.scale == b.scale);
    }

    /**
     * A JSON of one transform that is whole but for what the test then takes out of it or puts in it.
     */
    nlohmann::json wholeJson()
    {
        return nlohmann::json::parse(PIPBOY);
    }
}

TEST_CASE("ConfigOffsetsJson: a transform is read from a file's JSON, its rotation row by row")
{
    Offsets offsets;
    readOffsetsJson(nlohmann::json::parse(PIPBOY), offsets);

    REQUIRE(offsets.size() == 1);
    requireSame(offsets.at("PipboyPosition"), pipboyTransform());
}

TEST_CASE("ConfigOffsetsJson: a transform is written as the JSON it is read from")
{
    REQUIRE(writeOffsetsJson("PipboyPosition", pipboyTransform()).dump(4) == PIPBOY);
}

TEST_CASE("ConfigOffsetsJson: a transform that is written reads back the same, with all the digits of its numbers")
{
    auto transform = pipboyTransform();
    transform.rotate[0][0] = 0.7876706123352051f;
    transform.rotate[2][1] = 1.0f / 3.0f;
    transform.translate.x = -0.1f;
    transform.scale = 1.8300001621246338f;

    Offsets offsets;
    readOffsetsJson(nlohmann::json::parse(writeOffsetsJson("Weapon", transform).dump(4)), offsets);

    requireSame(offsets.at("Weapon"), transform);
}

TEST_CASE("ConfigOffsetsJson: every name of a JSON is read, and a name the map has gets the JSON's transform")
{
    auto json = wholeJson();
    json["HoloPipboyPosition"] = json["PipboyPosition"];
    json["HoloPipboyPosition"]["scale"] = 3.0f;

    Offsets offsets;
    offsets["PipboyPosition"].scale = 99.0f;
    offsets["Weapon"].scale = 2.0f;

    readOffsetsJson(json, offsets);

    REQUIRE(offsets.size() == 3);
    REQUIRE(offsets.at("PipboyPosition").scale == 1.75f);
    REQUIRE(offsets.at("HoloPipboyPosition").scale == 3.0f);
    // not in the JSON, so as it was
    REQUIRE(offsets.at("Weapon").scale == 2.0f);
}

TEST_CASE("ConfigOffsetsJson: a transform that lacks a value, or has something else than a number, fails to read")
{
    Offsets offsets;

    SECTION("no scale")
    {
        auto json = wholeJson();
        json["PipboyPosition"].erase("scale");
        REQUIRE_THROWS(readOffsetsJson(json, offsets));
    }
    SECTION("no position")
    {
        auto json = wholeJson();
        json["PipboyPosition"].erase("y");
        REQUIRE_THROWS(readOffsetsJson(json, offsets));
    }
    SECTION("no rotation")
    {
        auto json = wholeJson();
        json["PipboyPosition"].erase("rotation");
        REQUIRE_THROWS(readOffsetsJson(json, offsets));
    }
    SECTION("a rotation of 9 numbers")
    {
        auto json = wholeJson();
        json["PipboyPosition"]["rotation"] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        REQUIRE_THROWS(readOffsetsJson(json, offsets));
    }
    SECTION("a word for a number")
    {
        auto json = wholeJson();
        json["PipboyPosition"]["x"] = "left";
        REQUIRE_THROWS(readOffsetsJson(json, offsets));
    }
    SECTION("a name with a number in place of a transform")
    {
        REQUIRE_THROWS(readOffsetsJson(nlohmann::json::parse(R"({ "PipboyPosition": 1 })"), offsets));
    }
}

TEST_CASE("ConfigOffsetsJson: a JSON with no names reads nothing")
{
    Offsets offsets;
    readOffsetsJson(nlohmann::json::parse("{}"), offsets);
    REQUIRE(offsets.empty());
}
