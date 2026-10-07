#include <catch2/catch_test_macros.hpp>

#include "vrcf/InputBindingParser.h"

using namespace f4cf::vrcf;

namespace
{
    /**
     * A line that has to parse.
     */
    InputBinding parsed(const std::string_view text)
    {
        const auto binding = parseInputBinding(text);
        CAPTURE(text);
        REQUIRE(binding);
        return *binding;
    }
}

TEST_CASE("InputBindingParser: the words of a hand, a type, a button, an axis and a direction")
{
    REQUIRE(parseHand("primary") == Hand::Primary);
    REQUIRE(parseHand("offhand") == Hand::Offhand);
    REQUIRE(parseHand("right") == Hand::Right);
    REQUIRE(parseHand("left") == Hand::Left);
    REQUIRE_FALSE(parseHand("both"));

    REQUIRE(parseActivationType("touch") == ActivationType::Touch);
    REQUIRE(parseActivationType("press") == ActivationType::Press);
    REQUIRE(parseActivationType("click") == ActivationType::Press);
    REQUIRE(parseActivationType("tap") == ActivationType::Tap);
    REQUIRE(parseActivationType("hold") == ActivationType::HoldDown);
    REQUIRE(parseActivationType("held") == ActivationType::HoldDown);
    REQUIRE(parseActivationType("release") == ActivationType::Release);
    REQUIRE(parseActivationType("longpress") == ActivationType::LongPress);
    REQUIRE(parseActivationType("double") == ActivationType::DoublePress);
    REQUIRE(parseActivationType("doublepress") == ActivationType::DoublePress);
    REQUIRE(parseActivationType("axis") == ActivationType::AxisDirection);
    REQUIRE(parseActivationType("thumbstick") == ActivationType::AxisDirection);
    REQUIRE_FALSE(parseActivationType("squeeze"));

    REQUIRE(parseButton("system") == vr::k_EButton_System);
    REQUIRE(parseButton("menu") == vr::k_EButton_ApplicationMenu);
    REQUIRE(parseButton("b") == vr::k_EButton_ApplicationMenu);
    REQUIRE(parseButton("grip") == vr::k_EButton_Grip);
    REQUIRE(parseButton("a") == vr::k_EButton_A);
    REQUIRE(parseButton("trigger") == vr::k_EButton_SteamVR_Trigger);
    REQUIRE(parseButton("thumbstick") == vr::k_EButton_SteamVR_Touchpad);
    REQUIRE(parseButton("touchpad") == vr::k_EButton_SteamVR_Touchpad);
    REQUIRE_FALSE(parseButton("elbow"));

    REQUIRE(parseAxis("thumbstick") == Axis::Thumbstick);
    REQUIRE(parseAxis("trigger") == Axis::Trigger);
    REQUIRE(parseAxis("grip") == Axis::Grip);
    REQUIRE_FALSE(parseAxis("a"));

    REQUIRE(parseDirection("up") == Direction::Up);
    REQUIRE(parseDirection("down") == Direction::Down);
    REQUIRE(parseDirection("left") == Direction::Left);
    REQUIRE(parseDirection("right") == Direction::Right);
    REQUIRE_FALSE(parseDirection("forward"));
}

TEST_CASE("InputBindingParser: a word is read in any letter case, with spaces around it")
{
    REQUIRE(parseHand("  OffHand ") == Hand::Offhand);
    REQUIRE(parseActivationType("LongPress") == ActivationType::LongPress);
    REQUIRE(parseButton("\tTRIGGER") == vr::k_EButton_SteamVR_Trigger);
    REQUIRE(parseAxis("Grip ") == Axis::Grip);
    REQUIRE(parseDirection(" Up") == Direction::Up);
}

TEST_CASE("InputBindingParser: a button binding is a hand, how the button is worked, and the button")
{
    const auto binding = parsed("offhand longpress grip");

    REQUIRE(binding.hand == Hand::Offhand);
    REQUIRE(binding.type == ActivationType::LongPress);
    REQUIRE(binding.button == vr::k_EButton_Grip);
    REQUIRE(binding.isEnabled());

    // what the line does not say stays as an InputBinding has it
    const InputBinding defaults;
    REQUIRE(binding.duration == defaults.duration);
    REQUIRE(binding.suppress == defaults.suppress);
    REQUIRE_FALSE(binding.modifier);

    SECTION("with a duration after the button")
    {
        REQUIRE(parsed("left hold trigger 0.5").duration == 0.5f);
    }
    SECTION("a duration that is not a number is left out")
    {
        REQUIRE(parsed("left hold trigger 0.5x").duration == defaults.duration);
    }
    SECTION("the thumbstick as a button, pressed in")
    {
        const auto click = parsed("primary press thumbstick");
        REQUIRE(click.type == ActivationType::Press);
        REQUIRE(click.button == vr::k_EButton_SteamVR_Touchpad);
    }
}

TEST_CASE("InputBindingParser: an axis binding is a hand, the axis, and the direction it is pushed in")
{
    const InputBinding defaults;

    SECTION("the thumbstick by its own word")
    {
        const auto binding = parsed("primary thumbstick up");

        REQUIRE(binding.hand == Hand::Primary);
        REQUIRE(binding.type == ActivationType::AxisDirection);
        REQUIRE(binding.axis == Axis::Thumbstick);
        REQUIRE(binding.direction == Direction::Up);
        REQUIRE(binding.threshold == defaults.threshold);
    }
    SECTION("an axis by name, with how far it has to be pushed")
    {
        const auto binding = parsed("right axis trigger up 0.7");

        REQUIRE(binding.hand == Hand::Right);
        REQUIRE(binding.type == ActivationType::AxisDirection);
        REQUIRE(binding.axis == Axis::Trigger);
        REQUIRE(binding.direction == Direction::Up);
        REQUIRE(binding.threshold == 0.7f);
    }
    SECTION("the stick, to a side")
    {
        const auto binding = parsed("left stick left");

        REQUIRE(binding.axis == Axis::Thumbstick);
        REQUIRE(binding.direction == Direction::Left);
    }
}

TEST_CASE("InputBindingParser: the words of a line are apart by spaces, tabs, commas or colons, in any letter case")
{
    const auto expected = parsed("primary press trigger");

    REQUIRE(parsed("primary,press:trigger") == expected);
    REQUIRE(parsed("  Primary\tPRESS ,  Trigger  ") == expected);
}

TEST_CASE("InputBindingParser: a modifier is a button that is held while the binding is worked")
{
    SECTION("on the binding's own hand")
    {
        const auto binding = parsed("primary press trigger +grip");

        REQUIRE(binding.button == vr::k_EButton_SteamVR_Trigger);
        REQUIRE(binding.modifier);
        REQUIRE(binding.modifier->button == vr::k_EButton_Grip);
        REQUIRE_FALSE(binding.modifier->hand);
    }
    SECTION("on a hand that it names")
    {
        const auto binding = parsed("primary press trigger +offhand:grip");

        REQUIRE(binding.hand == Hand::Primary);
        REQUIRE(binding.modifier);
        REQUIRE(binding.modifier->button == vr::k_EButton_Grip);
        REQUIRE(binding.modifier->hand == Hand::Offhand);
    }
    SECTION("before a duration, which is still read")
    {
        const auto binding = parsed("left hold trigger +a 0.5");

        REQUIRE(binding.modifier);
        REQUIRE(binding.modifier->button == vr::k_EButton_A);
        REQUIRE(binding.duration == 0.5f);
    }
    SECTION("that names no button, or no button after its hand, fails the line")
    {
        REQUIRE_FALSE(parseInputBinding("primary press trigger +"));
        REQUIRE_FALSE(parseInputBinding("primary press trigger +elbow"));
        REQUIRE_FALSE(parseInputBinding("primary press trigger +offhand"));
        REQUIRE_FALSE(parseInputBinding("primary press trigger +offhand:elbow"));
    }
}

TEST_CASE("InputBindingParser: suppress and nosuppress say whether the button is hidden from the game")
{
    REQUIRE(parsed("primary press trigger suppress").suppress);
    REQUIRE_FALSE(parsed("primary press trigger nosuppress").suppress);

    // anywhere on the line, and the rest of it is read as without it
    auto expected = parsed("primary thumbstick up 0.5");
    expected.suppress = true;
    REQUIRE(parsed("primary suppress thumbstick up 0.5") == expected);
}

TEST_CASE("InputBindingParser: an empty line and the words for off give the binding that never triggers")
{
    for (const auto text : { "", "   ", "none", "off", "disabled", " None ", "OFF" }) {
        const auto binding = parsed(text);

        REQUIRE(binding.type == ActivationType::Disabled);
        REQUIRE_FALSE(binding.isEnabled());
        REQUIRE(binding == VRControllersManager::DisabledBinding);
    }

    // "off" is also the off hand, as the first word of a line
    REQUIRE(parsed("off press grip").hand == Hand::Offhand);
}

TEST_CASE("InputBindingParser: a line without a hand, a type or what the type needs does not parse")
{
    for (const auto text : {
             "primary",
             "press trigger",
             "nobody press trigger",
             "primary squeeze trigger",
             "primary press",
             "primary press elbow",
             "primary axis",
             "primary axis elbow up",
             "primary axis trigger",
             "primary thumbstick",
             "primary thumbstick sideways",
             "suppress primary",
         }) {
        CAPTURE(text);
        REQUIRE_FALSE(parseInputBinding(text));
    }
}

TEST_CASE("InputBindingParser: the name of a button and of an axis parses back to it")
{
    for (const auto button :
        { vr::k_EButton_System, vr::k_EButton_ApplicationMenu, vr::k_EButton_Grip, vr::k_EButton_A, vr::k_EButton_SteamVR_Trigger, vr::k_EButton_SteamVR_Touchpad }) {
        CAPTURE(buttonName(button));
        REQUIRE(parseButton(buttonName(button)) == button);
    }
    for (const auto axis : { Axis::Thumbstick, Axis::Trigger, Axis::Grip }) {
        CAPTURE(axisName(axis));
        REQUIRE(parseAxis(axisName(axis)) == axis);
    }

    // one the grammar has no word for is named by its number
    REQUIRE(buttonName(vr::k_EButton_DPad_Left) == "button3");
    REQUIRE(axisName(Axis::Unknown2) == "axis3");
}

TEST_CASE("InputBindingParser: the buttons of a mask are named lowest id first")
{
    const auto mask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger) | vr::ButtonMaskFromId(vr::k_EButton_Grip) | vr::ButtonMaskFromId(vr::k_EButton_A);

    REQUIRE(buttonNames(mask) == std::vector<std::string>{ "grip", "a", "trigger" });
    REQUIRE(buttonNames(0).empty());
}
