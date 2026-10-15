#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "TestMath.h"
#include "common/MatrixUtils.h"
#include "config/ConfigReaders.h"
#include "f4vr/WandActivationSphere.h"
#include "render/PrimitiveDraw.h"
#include "vrcf/InputBindingParser.h"

using namespace f4cf;
using config::ConfigReaders;
using test_math::requireNear;

namespace
{
    constexpr auto MAIN = "Main";

    /**
     * A loaded config with the given lines in its [Main] section.
     */
    struct Config
    {
        toml::table table;

        explicit Config(const std::string& lines)
            : table(toml::parse("[Main]\n" + lines))
        {}
    };

    void requireNear(const render::Color& actual, const render::Color& expected)
    {
        CAPTURE(actual.r, actual.g, actual.b, actual.a, expected.r, expected.g, expected.b, expected.a);
        test_math::requireNear(actual.r, expected.r);
        test_math::requireNear(actual.g, expected.g);
        test_math::requireNear(actual.b, expected.b);
        test_math::requireNear(actual.a, expected.a);
    }

    vrcf::InputBinding binding(const std::string_view text)
    {
        const auto parsed = vrcf::parseInputBinding(text);
        REQUIRE(parsed);
        return *parsed;
    }
}

TEST_CASE("ConfigReaders: a transform is a position, Euler angles in degrees and a scale")
{
    const auto fallback = common::MatrixUtils::getTransform(9, 9, 9, 0, 0, 0, 9);

    SECTION("read from its value")
    {
        const Config file("tOffset = \"1, 2.5, -3; 30, 20, 10; 2\"\n");

        const auto transform = ConfigReaders::getTransformValue(file.table, MAIN, "tOffset", fallback);

        requireNear(transform, common::MatrixUtils::getTransform(1, 2.5f, -3, 30, 20, 10, 2));
    }
    SECTION("the default for a key that is missing")
    {
        const Config file("");

        requireNear(ConfigReaders::getTransformValue(file.table, MAIN, "tOffset", fallback), fallback);
    }
    SECTION("the default for a value that is not one, and for one that is not a string")
    {
        const Config file("tOffset = \"1, 2, 3; 30, 20\"\ntNumber = 5\n");

        requireNear(ConfigReaders::getTransformValue(file.table, MAIN, "tOffset", fallback), fallback);
        requireNear(ConfigReaders::getTransformValue(file.table, MAIN, "tNumber", fallback), fallback);
    }
}

TEST_CASE("ConfigReaders: a color is three or four whole numbers of 0 to 255")
{
    const render::Color fallback{ 0.1f, 0.2f, 0.3f, 0.4f };

    SECTION("with its opacity")
    {
        const Config file("sColor = \"255, 0, 51, 102\"\n");

        requireNear(ConfigReaders::getColorValue(file.table, MAIN, "sColor", fallback), { 1.0f, 0.0f, 0.2f, 0.4f });
    }
    SECTION("without it, the default's opacity stays")
    {
        const Config file("sColor = \"255,0,51\"\n");

        requireNear(ConfigReaders::getColorValue(file.table, MAIN, "sColor", fallback), { 1.0f, 0.0f, 0.2f, 0.4f });
    }
    SECTION("the default for a key that is missing, empty or not a color")
    {
        const Config file("sEmpty = \"\"\nsWords = \"red\"\nsTooLarge = \"256,0,0\"\n");

        requireNear(ConfigReaders::getColorValue(file.table, MAIN, "sColor", fallback), fallback);
        requireNear(ConfigReaders::getColorValue(file.table, MAIN, "sEmpty", fallback), fallback);
        requireNear(ConfigReaders::getColorValue(file.table, MAIN, "sWords", fallback), fallback);
        requireNear(ConfigReaders::getColorValue(file.table, MAIN, "sTooLarge", fallback), fallback);
    }
}

TEST_CASE("ConfigReaders: a hand pose is the four numbers of each finger and the two of the palm")
{
    std::array<float, 22> fallback{};
    fallback.fill(0.5f);

    SECTION("read from its value")
    {
        const Config file("hPose = \"1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 17,18,19,20; 21,22\"\n");

        const auto pose = ConfigReaders::getHandPoseValue(file.table, MAIN, "hPose", fallback);

        for (std::size_t i = 0; i < pose.size(); i++) {
            CAPTURE(i);
            REQUIRE(pose[i] == static_cast<float>(i + 1));
        }
    }
    SECTION("the default for a key that is missing, and for a value that is short of a finger")
    {
        const Config file("hShort = \"1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 21,22\"\n");

        REQUIRE(ConfigReaders::getHandPoseValue(file.table, MAIN, "hPose", fallback) == fallback);
        REQUIRE(ConfigReaders::getHandPoseValue(file.table, MAIN, "hShort", fallback) == fallback);
    }
}

TEST_CASE("ConfigReaders: a list of strings is read in its order")
{
    const std::vector<std::string> fallback{ "fallback" };
    const Config file("aNames = [\"skelly\", \"perf\", \"skelly\"]\naEmpty = []\naMixed = [\"one\", 2, \"three\"]\naText = \"skelly, perf\"\n");

    REQUIRE(ConfigReaders::getStringListValue(file.table, MAIN, "aNames", fallback) == std::vector<std::string>{ "skelly", "perf", "skelly" });
    // an empty array is a value, not a missing key
    REQUIRE(ConfigReaders::getStringListValue(file.table, MAIN, "aEmpty", fallback).empty());
    // an item that is not a string is skipped, and the others stay
    REQUIRE(ConfigReaders::getStringListValue(file.table, MAIN, "aMixed", fallback) == std::vector<std::string>{ "one", "three" });

    // the default for a key that is missing, and for a value that is not an array
    REQUIRE(ConfigReaders::getStringListValue(file.table, MAIN, "aMissing", fallback) == fallback);
    REQUIRE(ConfigReaders::getStringListValue(file.table, MAIN, "aText", fallback) == fallback);
}

TEST_CASE("ConfigReaders: an input binding is read as the binding grammar has it")
{
    const auto fallback = binding("left press grip");
    const Config file("sBinding = \"right double a\"\nsOff = \"none\"\nsEmpty = \"\"\nsWrong = \"right squeeze a\"\nsNumber = 2\n");

    REQUIRE(ConfigReaders::getInputBindingValue(file.table, MAIN, "sBinding", fallback) == binding("right double a"));

    // off by its word and by an empty value, which is not a missing key
    REQUIRE_FALSE(ConfigReaders::getInputBindingValue(file.table, MAIN, "sOff", fallback).isEnabled());
    REQUIRE_FALSE(ConfigReaders::getInputBindingValue(file.table, MAIN, "sEmpty", fallback).isEnabled());

    // the default for a key that is missing, for a line that does not parse, and for a value that is not a string
    REQUIRE(ConfigReaders::getInputBindingValue(file.table, MAIN, "sMissing", fallback) == fallback);
    REQUIRE(ConfigReaders::getInputBindingValue(file.table, MAIN, "sWrong", fallback) == fallback);
    REQUIRE(ConfigReaders::getInputBindingValue(file.table, MAIN, "sNumber", fallback) == fallback);
}

TEST_CASE("ConfigReaders: an activation sphere with nothing in its section is its defaults")
{
    f4vr::WandActivationConfig defaults;
    defaults.zone = common::MatrixUtils::getTransform(1, 2, 3, 0, 0, 0, 4);
    defaults.zonePA = common::MatrixUtils::getTransform(5, 6, 7, 0, 0, 0, 8);
    defaults.primary = binding("left press grip");
    defaults.secondary = binding("left longpress grip");
    defaults.entryHaptic = vrcf::HapticPattern::Buzz;
    defaults.primaryHaptic = std::nullopt;
    defaults.secondaryHaptic = vrcf::HapticPattern::Error;
    defaults.showSphere = f4vr::ActivationSphereVisibility::WhenAvailable;
    defaults.sphereStyle = *f4vr::findSphereStylePreset("red-full");
    defaults.sphereStyle.scale = 0.5f;
    defaults.sphereOrientation = f4vr::ActivationSphereOrientation::World;
    defaults.showIcon = f4vr::ActivationSphereVisibility::Always;
    defaults.iconStyle = { .texture = "my-icon.dds", .color = { 0.1f, 0.2f, 0.3f, 0.4f }, .size = 2.0f };
    const Config file("");

    const auto config = ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults);

    requireNear(config.zone, defaults.zone);
    REQUIRE(config.zonePA);
    requireNear(*config.zonePA, *defaults.zonePA);
    REQUIRE(config.primary == defaults.primary);
    REQUIRE(config.secondary == defaults.secondary);
    REQUIRE(config.entryHaptic == defaults.entryHaptic);
    REQUIRE(config.primaryHaptic == defaults.primaryHaptic);
    REQUIRE(config.secondaryHaptic == defaults.secondaryHaptic);
    REQUIRE(config.showSphere == defaults.showSphere);
    REQUIRE(config.sphereStyle == defaults.sphereStyle);
    REQUIRE(config.sphereOrientation == defaults.sphereOrientation);
    REQUIRE(config.showIcon == defaults.showIcon);
    REQUIRE(config.iconStyle == defaults.iconStyle);
}

TEST_CASE("ConfigReaders: an activation sphere is read from the keys of its section")
{
    const f4vr::WandActivationConfig defaults;
    const Config file(
        "tZone = \"1,2,3; 0,0,0; 4\"\n"
        "tZonePA = \"5,6,7; 0,0,0; 8\"\n"
        "sPrimaryBinding = \"right press trigger suppress\"\n"
        "sSecondaryBinding = \"right longpress trigger\"\n"
        "sEntryHaptic = \"none\"\n"
        "sPrimaryHaptic = \"Success\"\n"
        "sSecondaryHaptic = \"double-click\"\n"
        "sShowSphere = \"always\"\n"
        "sSphereOrientation = \"world\"\n"
        "sShowIcon = \"never\"\n");

    const auto config = ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults);

    requireNear(config.zone, common::MatrixUtils::getTransform(1, 2, 3, 0, 0, 0, 4));
    REQUIRE(config.zonePA);
    requireNear(*config.zonePA, common::MatrixUtils::getTransform(5, 6, 7, 0, 0, 0, 8));
    requireNear(config.zoneFor(false), config.zone);
    requireNear(config.zoneFor(true), *config.zonePA);
    REQUIRE(config.primary == binding("right press trigger suppress"));
    REQUIRE(config.secondary == binding("right longpress trigger"));
    REQUIRE_FALSE(config.entryHaptic);
    REQUIRE(config.primaryHaptic == vrcf::HapticPattern::Success);
    REQUIRE(config.secondaryHaptic == vrcf::HapticPattern::DoubleClick);
    REQUIRE(config.showSphere == f4vr::ActivationSphereVisibility::Always);
    REQUIRE(config.sphereOrientation == f4vr::ActivationSphereOrientation::World);
    REQUIRE(config.showIcon == f4vr::ActivationSphereVisibility::Never);

    // the look of the sphere and of the icon has no key here
    REQUIRE(config.sphereStyle == defaults.sphereStyle);
    REQUIRE(config.iconStyle == defaults.iconStyle);
}

TEST_CASE("ConfigReaders: an activation sphere with no power armor zone uses its zone in power armor")
{
    const f4vr::WandActivationConfig defaults;
    const Config file("tZone = \"1,2,3; 0,0,0; 4\"\n");

    const auto config = ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults);

    REQUIRE_FALSE(config.zonePA);
    requireNear(config.zoneFor(true), config.zone);
}

TEST_CASE("ConfigReaders: the look of an activation sphere is a preset with single values over it")
{
    f4vr::WandActivationConfig defaults;
    defaults.sphereStyle.scale = 0.5f;
    const auto preset = *f4vr::findSphereStylePreset("cyan-subtle");

    SECTION("a preset replaces the look and keeps the size")
    {
        const Config file("sSphereStyle = \"Cyan Subtle\"\n");

        auto expected = preset;
        expected.scale = 0.5f;
        REQUIRE(ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults).sphereStyle == expected);
    }
    SECTION("each key of its own sets one value, a number as an integer or a float")
    {
        const Config file(
            "sSphereStyle = \"cyan-subtle\"\n"
            "sSphereNif = \"my-sphere.nif\"\n"
            "sSphereTexture = \"my-sphere.dds\"\n"
            "sSphereColor = \"255, 0, 51, 102\"\n"
            "fSphereGlow = 0.25\n"
            "sSphereFalloff = \"0.1, 0.9\"\n"
            "fSphereScale = 1\n");

        const auto style = ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults).sphereStyle;

        REQUIRE(style.nif == "my-sphere.nif");
        REQUIRE(style.texture == "my-sphere.dds");
        requireNear(style.color[0], 1.0f);
        requireNear(style.color[1], 0.0f);
        requireNear(style.color[2], 0.2f);
        requireNear(style.color[3], 0.4f);
        requireNear(style.glow, 0.25f);
        requireNear(style.centerOpacity, 0.1f);
        requireNear(style.rimOpacity, 0.9f);
        requireNear(style.scale, 1.0f);
    }
    SECTION("a texture of none keeps the one the mesh names")
    {
        const Config file("sSphereTexture = \"None\"\n");

        REQUIRE(ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults).sphereStyle.texture.empty());
    }
    SECTION("an unknown preset, values out of their range and values of another type are left out")
    {
        const Config file(
            "sSphereStyle = \"cyan-loud\"\n"
            "sSphereColor = \"cyan\"\n"
            "fSphereGlow = 1.5\n"
            "sSphereFalloff = \"0.1\"\n"
            "fSphereScale = \"big\"\n");

        REQUIRE(ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults).sphereStyle == defaults.sphereStyle);
    }
}

TEST_CASE("ConfigReaders: the look of an activation sphere's icon is single values over the default")
{
    const f4vr::WandActivationConfig defaults;

    SECTION("each key sets one value")
    {
        const Config file("sIcon = \"my-icon.dds\"\nsIconColor = \"255, 0, 51, 102\"\nfIconSize = 2.5\n");

        const auto style = ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults).iconStyle;

        REQUIRE(style.texture == "my-icon.dds");
        REQUIRE(style.textureOrDefault() == "my-icon.dds");
        requireNear(style.color[0], 1.0f);
        requireNear(style.color[2], 0.2f);
        requireNear(style.color[3], 0.4f);
        requireNear(style.size, 2.5f);
    }
    SECTION("values out of their range are left out")
    {
        const Config file("sIconColor = \"1,2\"\nfIconSize = -1\n");

        REQUIRE(ConfigReaders::loadWandActivationConfig(file.table, MAIN, defaults).iconStyle == defaults.iconStyle);
    }
}
