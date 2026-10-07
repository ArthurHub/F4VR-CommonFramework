#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "f4vr/SphereStyle.h"

using namespace f4cf;

TEST_CASE("SphereStyle: a preset is a color and a strength, on the framework's sphere mesh")
{
    const auto style = f4vr::findSphereStylePreset("cyan-subtle");

    REQUIRE(style);
    REQUIRE(style->nif == "f4cf\\activation-sphere.nif");
    REQUIRE(style->texture == "f4cf\\activation-sphere.dds");

    // the color's three numbers, and the strength's opacity
    REQUIRE(style->color == std::array{ 0.1f, 0.68f, 1.0f, 0.105f });
    REQUIRE(style->glow == 0.4f);
    REQUIRE(style->centerOpacity == 0.025f);
    REQUIRE(style->rimOpacity == 0.82f);
    REQUIRE(style->scale == 1.0f);
}

TEST_CASE("SphereStyle: every color has every strength, each a look of its own")
{
    std::vector<f4vr::SphereStyle> styles;
    for (const std::string color : { "white", "gray", "cyan", "green", "purple", "red", "amber", "gold" }) {
        for (const std::string strength : { "full", "medium", "subtle", "low" }) {
            CAPTURE(color, strength);
            const auto style = f4vr::findSphereStylePreset(color + "-" + strength);

            REQUIRE(style);
            REQUIRE(std::ranges::find(styles, *style) == styles.end());
            styles.push_back(*style);
        }
    }
}

TEST_CASE("SphereStyle: a preset's name is read in any letter case and with any separators")
{
    const auto expected = f4vr::findSphereStylePreset("cyan-subtle");

    REQUIRE(f4vr::findSphereStylePreset("Cyan Subtle") == expected);
    REQUIRE(f4vr::findSphereStylePreset("CYAN_SUBTLE") == expected);
    REQUIRE(f4vr::findSphereStylePreset("cyansubtle") == expected);
}

TEST_CASE("SphereStyle: a name that is not a preset has no style")
{
    for (const auto name : { "", "cyan", "subtle", "cyan-loud", "pink-subtle", "cyan-subtle-more", "debug-subtle" }) {
        CAPTURE(name);
        REQUIRE_FALSE(f4vr::findSphereStylePreset(name));
    }
}

TEST_CASE("SphereStyle: the debug preset is an evenly lit grid at full glow")
{
    const auto style = f4vr::findSphereStylePreset("Debug");

    REQUIRE(style);
    REQUIRE(style->texture == "f4cf\\debug-sphere.dds");
    REQUIRE(style->glow == 1.0f);
    REQUIRE(style->centerOpacity == style->rimOpacity);
    REQUIRE(f4vr::getDebugSphereStyle() == *style);
}

TEST_CASE("SphereStyle: the default style is the white-subtle preset, and its mesh is the one a style without a mesh uses")
{
    REQUIRE(f4vr::getDefaultSphereStyle() == *f4vr::findSphereStylePreset("white-subtle"));

    f4vr::SphereStyle style;
    REQUIRE(style.nifOrDefault() == f4vr::getDefaultSphereStyle().nif);

    style.nif = "my-sphere.nif";
    REQUIRE(style.nifOrDefault() == "my-sphere.nif");
}
