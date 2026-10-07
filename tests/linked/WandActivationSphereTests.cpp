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
