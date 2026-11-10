#include <catch2/catch_test_macros.hpp>

#include "f4vr/WandActivationSphere.h"

using namespace f4cf::f4vr;

TEST_CASE("WandActivationSphere: when a visual is drawn is read by its word")
{
    using Visibility = ActivationSphereVisibility;
    const auto fallback = Visibility::WhenInside;

    for (const auto text : { "never", "off", "false", "none" }) {
        CAPTURE(text);
        REQUIRE(parseActivationSphereVisibility(text, fallback) == Visibility::Never);
    }
    for (const auto text : { "always", "on", "true" }) {
        CAPTURE(text);
        REQUIRE(parseActivationSphereVisibility(text, Visibility::Never) == Visibility::Always);
    }
    for (const auto text : { "wheninside", "inside", "proximity", "active", "near" }) {
        CAPTURE(text);
        REQUIRE(parseActivationSphereVisibility(text, Visibility::Never) == Visibility::WhenInside);
    }
    for (const auto text : { "whenavailable", "available" }) {
        CAPTURE(text);
        REQUIRE(parseActivationSphereVisibility(text, fallback) == Visibility::WhenAvailable);
    }

    // in any letter case and with any separators
    REQUIRE(parseActivationSphereVisibility(" When_Inside ", Visibility::Never) == Visibility::WhenInside);
    REQUIRE(parseActivationSphereVisibility("WHEN-AVAILABLE", fallback) == Visibility::WhenAvailable);

    // the fallback for no word, and for one it does not know
    REQUIRE(parseActivationSphereVisibility("", fallback) == fallback);
    REQUIRE(parseActivationSphereVisibility("  ", Visibility::Always) == Visibility::Always);
    REQUIRE(parseActivationSphereVisibility("sometimes", fallback) == fallback);
}

TEST_CASE("WandActivationSphere: what a sphere turns with is read by its word")
{
    using Orientation = ActivationSphereOrientation;

    REQUIRE(parseActivationSphereOrientation("hmd", Orientation::World) == Orientation::Hmd);
    REQUIRE(parseActivationSphereOrientation("Head", Orientation::World) == Orientation::Hmd);
    REQUIRE(parseActivationSphereOrientation("body", Orientation::World) == Orientation::Body);
    REQUIRE(parseActivationSphereOrientation("skeleton", Orientation::World) == Orientation::Body);
    REQUIRE(parseActivationSphereOrientation(" WORLD ", Orientation::Hmd) == Orientation::World);

    // the fallback for no word, and for one it does not know
    REQUIRE(parseActivationSphereOrientation("", Orientation::World) == Orientation::World);
    REQUIRE(parseActivationSphereOrientation("sideways", Orientation::Body) == Orientation::Body);
}

TEST_CASE("WandActivationSphere: an icon style without a texture draws the framework's hand icon")
{
    ActivationIconStyle style;
    const auto defaultTexture = style.textureOrDefault();

    REQUIRE(defaultTexture.ends_with(".dds"));

    style.texture = "my-icon.dds";
    REQUIRE(style.textureOrDefault() == "my-icon.dds");
}

TEST_CASE("WandActivationSphere: the menu points at a sphere while its ray is on a row of its section")
{
    WandActivationConfig config;
    config.section = "BodyGrab";
    using Ids = std::vector<std::string>;

    // a row of the section, the section's heading, and a row of a section inside a group around it
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "kPrimaryBinding:BodyGrab", "BodyGrab" }, false));
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "BodyGrab" }, false));
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "sShowSphere:BodyGrab", "BodyGrab", "spheres" }, true));

    // nothing, and the rows of another section
    REQUIRE_FALSE(WandActivationSphere::isPointedAt(config, Ids{}, false));
    REQUIRE_FALSE(WandActivationSphere::isPointedAt(config, Ids{ "tZone:HeadGrab", "HeadGrab" }, false));
    REQUIRE_FALSE(WandActivationSphere::isPointedAt(config, Ids{ "bodygrab" }, false));

    // with no power-armor zone, the zone is the one in use in power armor too
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "tZone:BodyGrab", "BodyGrab" }, false));
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "tZone:BodyGrab", "BodyGrab" }, true));

    // with one, each zone's row only while it is the one in use
    config.zonePA = RE::NiTransform{};
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "tZone:BodyGrab", "BodyGrab" }, false));
    REQUIRE_FALSE(WandActivationSphere::isPointedAt(config, Ids{ "tZone:BodyGrab", "BodyGrab" }, true));
    REQUIRE_FALSE(WandActivationSphere::isPointedAt(config, Ids{ "tZonePA:BodyGrab", "BodyGrab" }, false));
    REQUIRE(WandActivationSphere::isPointedAt(config, Ids{ "tZonePA:BodyGrab", "BodyGrab" }, true));

    // a config read from no section is never pointed at
    REQUIRE_FALSE(WandActivationSphere::isPointedAt(WandActivationConfig{}, Ids{ "" }, false));
}
