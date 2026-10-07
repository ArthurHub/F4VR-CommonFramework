#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <string>

#include "TestMath.h"
#include "common/MatrixUtils.h"
#include "config/IniReaders.h"
#include "f4vr/WandActivationSphere.h"
#include "render/PrimitiveDraw.h"
#include "vrcf/InputBindingParser.h"

using namespace f4cf;
using config::IniReaders;
using test_math::requireNear;

namespace
{
    constexpr auto MAIN = "Main";

    /**
     * A loaded INI with the given lines in its [Main] section.
     */
    struct Ini
    {
        CSimpleIniA ini;

        explicit Ini(const std::string& lines)
        {
            REQUIRE(ini.LoadData("[Main]\n" + lines) >= 0);
        }
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

TEST_CASE("IniReaders: a transform is a position, Euler angles in degrees and a scale")
{
    const auto fallback = common::MatrixUtils::getTransform(9, 9, 9, 0, 0, 0, 9);

    SECTION("read from its value")
    {
        const Ini file("tOffset = 1, 2.5, -3; 30, 20, 10; 2\n");

        const auto transform = IniReaders::getTransformValue(file.ini, MAIN, "tOffset", fallback);

        requireNear(transform, common::MatrixUtils::getTransform(1, 2.5f, -3, 30, 20, 10, 2));
    }
    SECTION("the default for a key that is missing")
    {
        const Ini file("");

        requireNear(IniReaders::getTransformValue(file.ini, MAIN, "tOffset", fallback), fallback);
    }
    SECTION("the default for a value that is not one")
    {
        const Ini file("tOffset = 1, 2, 3; 30, 20\n");

        requireNear(IniReaders::getTransformValue(file.ini, MAIN, "tOffset", fallback), fallback);
    }
}

TEST_CASE("IniReaders: a color is three or four whole numbers of 0 to 255")
{
    const render::Color fallback{ 0.1f, 0.2f, 0.3f, 0.4f };

    SECTION("with its opacity")
    {
        const Ini file("sColor = 255, 0, 51, 102\n");

        requireNear(IniReaders::getColorValue(file.ini, MAIN, "sColor", fallback), { 1.0f, 0.0f, 0.2f, 0.4f });
    }
    SECTION("without it, the default's opacity stays")
    {
        const Ini file("sColor = 255,0,51\n");

        requireNear(IniReaders::getColorValue(file.ini, MAIN, "sColor", fallback), { 1.0f, 0.0f, 0.2f, 0.4f });
    }
    SECTION("the default for a key that is missing, empty or not a color")
    {
        const Ini file("sEmpty =\nsWords = red\nsTooLarge = 256,0,0\n");

        requireNear(IniReaders::getColorValue(file.ini, MAIN, "sColor", fallback), fallback);
        requireNear(IniReaders::getColorValue(file.ini, MAIN, "sEmpty", fallback), fallback);
        requireNear(IniReaders::getColorValue(file.ini, MAIN, "sWords", fallback), fallback);
        requireNear(IniReaders::getColorValue(file.ini, MAIN, "sTooLarge", fallback), fallback);
    }
}

TEST_CASE("IniReaders: a hand pose is the four numbers of each finger and the two of the palm")
{
    std::array<float, 22> fallback{};
    fallback.fill(0.5f);

    SECTION("read from its value")
    {
        const Ini file("hPose = 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 17,18,19,20; 21,22\n");

        const auto pose = IniReaders::getHandPoseValue(file.ini, MAIN, "hPose", fallback);

        for (std::size_t i = 0; i < pose.size(); i++) {
            CAPTURE(i);
            REQUIRE(pose[i] == static_cast<float>(i + 1));
        }
    }
    SECTION("the default for a key that is missing, and for a value that is short of a finger")
    {
        const Ini file("hShort = 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 21,22\n");

        REQUIRE(IniReaders::getHandPoseValue(file.ini, MAIN, "hPose", fallback) == fallback);
        REQUIRE(IniReaders::getHandPoseValue(file.ini, MAIN, "hShort", fallback) == fallback);
    }
}

TEST_CASE("IniReaders: an input binding is read as the binding grammar has it")
{
    const auto fallback = binding("left press grip");
    const Ini file("sBinding = right double a\nsOff = none\nsEmpty =\nsWrong = right squeeze a\n");

    REQUIRE(IniReaders::getInputBindingValue(file.ini, MAIN, "sBinding", fallback) == binding("right double a"));

    // off by its word and by an empty value, which is not a missing key
    REQUIRE_FALSE(IniReaders::getInputBindingValue(file.ini, MAIN, "sOff", fallback).isEnabled());
    REQUIRE_FALSE(IniReaders::getInputBindingValue(file.ini, MAIN, "sEmpty", fallback).isEnabled());

    // the default for a key that is missing, and for a line that does not parse
    REQUIRE(IniReaders::getInputBindingValue(file.ini, MAIN, "sMissing", fallback) == fallback);
    REQUIRE(IniReaders::getInputBindingValue(file.ini, MAIN, "sWrong", fallback) == fallback);
}

TEST_CASE("IniReaders: an activation sphere with nothing in its section is its defaults")
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
    const Ini file("");

    const auto config = IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults);

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

TEST_CASE("IniReaders: an activation sphere is read from the keys of its section")
{
    const f4vr::WandActivationConfig defaults;
    const Ini file(
        "tZone = 1,2,3; 0,0,0; 4\n"
        "tZonePA = 5,6,7; 0,0,0; 8\n"
        "sPrimaryBinding = right press trigger suppress\n"
        "sSecondaryBinding = right longpress trigger\n"
        "sEntryHaptic = none\n"
        "sPrimaryHaptic = Success\n"
        "sSecondaryHaptic = double-click\n"
        "sShowSphere = always\n"
        "sSphereOrientation = world\n"
        "sShowIcon = never\n");

    const auto config = IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults);

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

TEST_CASE("IniReaders: an activation sphere with no power armor zone uses its zone in power armor")
{
    const f4vr::WandActivationConfig defaults;
    const Ini file("tZone = 1,2,3; 0,0,0; 4\n");

    const auto config = IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults);

    REQUIRE_FALSE(config.zonePA);
    requireNear(config.zoneFor(true), config.zone);
}

TEST_CASE("IniReaders: the look of an activation sphere is a preset with single values over it")
{
    f4vr::WandActivationConfig defaults;
    defaults.sphereStyle.scale = 0.5f;
    const auto preset = *f4vr::findSphereStylePreset("cyan-subtle");

    SECTION("a preset replaces the look and keeps the size")
    {
        const Ini file("sSphereStyle = Cyan Subtle\n");

        auto expected = preset;
        expected.scale = 0.5f;
        REQUIRE(IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults).sphereStyle == expected);
    }
    SECTION("each key of its own sets one value")
    {
        const Ini file(
            "sSphereStyle = cyan-subtle\n"
            "sSphereNif = my-sphere.nif\n"
            "sSphereTexture = my-sphere.dds\n"
            "sSphereColor = 255, 0, 51, 102\n"
            "fSphereGlow = 0.25\n"
            "sSphereFalloff = 0.1, 0.9\n"
            "fSphereScale = 0.75\n");

        const auto style = IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults).sphereStyle;

        REQUIRE(style.nif == "my-sphere.nif");
        REQUIRE(style.texture == "my-sphere.dds");
        requireNear(style.color[0], 1.0f);
        requireNear(style.color[1], 0.0f);
        requireNear(style.color[2], 0.2f);
        requireNear(style.color[3], 0.4f);
        requireNear(style.glow, 0.25f);
        requireNear(style.centerOpacity, 0.1f);
        requireNear(style.rimOpacity, 0.9f);
        requireNear(style.scale, 0.75f);
    }
    SECTION("a texture of none keeps the one the mesh names")
    {
        const Ini file("sSphereTexture = None\n");

        REQUIRE(IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults).sphereStyle.texture.empty());
    }
    SECTION("an unknown preset and values out of their range are left out")
    {
        const Ini file(
            "sSphereStyle = cyan-loud\n"
            "sSphereColor = cyan\n"
            "fSphereGlow = 1.5\n"
            "sSphereFalloff = 0.1\n"
            "fSphereScale = 0\n");

        REQUIRE(IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults).sphereStyle == defaults.sphereStyle);
    }
}

TEST_CASE("IniReaders: the look of an activation sphere's icon is single values over the default")
{
    const f4vr::WandActivationConfig defaults;

    SECTION("each key sets one value")
    {
        const Ini file("sIcon = my-icon.dds\nsIconColor = 255, 0, 51, 102\nfIconSize = 2.5\n");

        const auto style = IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults).iconStyle;

        REQUIRE(style.texture == "my-icon.dds");
        REQUIRE(style.textureOrDefault() == "my-icon.dds");
        requireNear(style.color[0], 1.0f);
        requireNear(style.color[2], 0.2f);
        requireNear(style.color[3], 0.4f);
        requireNear(style.size, 2.5f);
    }
    SECTION("values out of their range are left out")
    {
        const Ini file("sIconColor = 1,2\nfIconSize = -1\n");

        REQUIRE(IniReaders::loadWandActivationConfig(file.ini, MAIN, defaults).iconStyle == defaults.iconStyle);
    }
}
