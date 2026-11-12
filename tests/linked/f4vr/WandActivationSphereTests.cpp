#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "f4vr/WandActivationSphere.h"
#include "vrcf/InputBindingParser.h"

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

TEST_CASE("WandActivationSphere: which bindings a binding icon shows is read by its word")
{
    using Icons = ActivationBindingIcons;

    REQUIRE(parseActivationBindingIcons("all", Icons::Inside) == Icons::All);
    REQUIRE(parseActivationBindingIcons("Both", Icons::Inside) == Icons::All);
    REQUIRE(parseActivationBindingIcons("primary", Icons::Inside) == Icons::Primary);
    REQUIRE(parseActivationBindingIcons("SECONDARY", Icons::Inside) == Icons::Secondary);
    REQUIRE(parseActivationBindingIcons("inside", Icons::All) == Icons::Inside);
    REQUIRE(parseActivationBindingIcons("when-inside", Icons::All) == Icons::Inside);

    // the fallback for no word, and for one it does not know
    REQUIRE(parseActivationBindingIcons("", Icons::Primary) == Icons::Primary);
    REQUIRE(parseActivationBindingIcons("some", Icons::Primary) == Icons::Primary);
}

// The bindings here name the left or the right hand: the primary and the off hand are resolved by the game's
// left-handed setting, which a test cannot read.
TEST_CASE("WandActivationSphere: a binding icon shows the enabled bindings it is set to, each prompt once")
{
    using Icons = ActivationBindingIcons;
    using FrameBinding = WandActivationSphere::FrameBinding;
    const auto binding = [](const std::string_view text) {
        const auto parsed = f4cf::vrcf::parseInputBinding(text);
        REQUIRE(parsed);
        return *parsed;
    };
    const auto leftTrigger = binding("left tap trigger");
    const auto rightTrigger = binding("right tap trigger");
    const auto leftLongTrigger = binding("left longpress trigger");
    const auto disabled = f4cf::vrcf::VRControllersManager::DisabledBinding;

    // a body grab: either hand's trigger, only the left hand inside
    const std::vector<FrameBinding> bodyGrab{ { .binding = leftTrigger, .inside = true }, { .binding = rightTrigger, .inside = false } };
    REQUIRE(WandActivationSphere::selectIconBindings(bodyGrab, Icons::All) == std::vector{ leftTrigger, rightTrigger });
    REQUIRE(WandActivationSphere::selectIconBindings(bodyGrab, Icons::Inside) == std::vector{ leftTrigger });
    REQUIRE(WandActivationSphere::selectIconBindings(bodyGrab, Icons::Primary) == std::vector{ leftTrigger });
    REQUIRE(WandActivationSphere::selectIconBindings(bodyGrab, Icons::Secondary) == std::vector{ rightTrigger });

    // a binding gated off for the frame shows under no choice
    const std::vector<FrameBinding> gated{ { .binding = disabled, .inside = false }, { .binding = leftLongTrigger, .inside = true } };
    REQUIRE(WandActivationSphere::selectIconBindings(gated, Icons::All) == std::vector{ leftLongTrigger });
    REQUIRE(WandActivationSphere::selectIconBindings(gated, Icons::Primary).empty());

    // a press and a tap of one button are one picture, which shows once
    const std::vector<FrameBinding> same{ { .binding = leftTrigger, .inside = true }, { .binding = binding("left press trigger"), .inside = true } };
    REQUIRE(WandActivationSphere::selectIconBindings(same, Icons::All) == std::vector{ leftTrigger });
}

TEST_CASE("WandActivationSphere: a row of binding icons is aligned by the side of its anchor")
{
    const std::vector widths{ 1.0f, 2.0f };
    const auto near = [](const std::vector<float>& actual, const std::vector<float>& expected) {
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CAPTURE(i);
            REQUIRE(std::abs(actual[i] - expected[i]) < 0.0001f);
        }
    };

    // to the right of the anchor, to its left, and centered on zero
    near(WandActivationSphere::layoutIconRow(widths, 0.5f, 1.0f), { 1.5f, 3.5f });
    near(WandActivationSphere::layoutIconRow(widths, 0.5f, -1.0f), { -4.0f, -2.0f });
    near(WandActivationSphere::layoutIconRow(widths, 0.5f, 0.0f), { -1.25f, 0.75f });

    // one icon on zero is centered on it, and no icons are no centers
    near(WandActivationSphere::layoutIconRow(std::vector{ 2.0f }, 0.5f, 0.0f), { 0.0f });
    REQUIRE(WandActivationSphere::layoutIconRow({}, 0.5f, 1.0f).empty());
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
