#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <initializer_list>
#include <string>
#include <utility>

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
    SECTION("a duration that is not a number fails the line, and is not left out")
    {
        REQUIRE_FALSE(parseInputBinding("left hold trigger 0.5x"));
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

TEST_CASE("InputBindingParser: a line that does not parse says why, naming the word that is wrong")
{
    const auto errorOf = [](const std::string_view text) {
        const auto binding = parseInputBinding(text);
        CAPTURE(text);
        REQUIRE_FALSE(binding);
        return binding.error();
    };
    const auto contains = [](const std::string& error, const std::string_view part) {
        CAPTURE(error, part);
        return error.find(part) != std::string::npos;
    };

    // a word that is none of its part's words, with the words that would fit
    CHECK(contains(errorOf("nobody press trigger"), "'nobody' is not a hand, one of primary, offhand"));
    CHECK(contains(errorOf("primary squeeze trigger"), "'squeeze' is not a type, one of tap,"));
    CHECK(contains(errorOf("primary longpress thumbstik"), "'thumbstik' is not a button, one of trigger,"));
    CHECK(contains(errorOf("primary axis elbow up"), "'elbow' is not an axis"));
    CHECK(contains(errorOf("primary thumbstick sideways"), "'sideways' is not a direction, one of up,"));
    CHECK(contains(errorOf("primary press trigger +elbow"), "'+elbow' is not a modifier"));
    CHECK(contains(errorOf("primary press trigger +offhand:elbow"), "'elbow' is not a button"));

    // a part that is missing, by what it comes after
    CHECK(contains(errorOf("suppress"), "the hand and the type are missing"));
    CHECK(contains(errorOf("primary"), "the type is missing after 'primary'"));
    CHECK(contains(errorOf("primary press"), "the button is missing after 'press'"));
    CHECK(contains(errorOf("primary axis"), "the axis is missing after 'axis'"));
    CHECK(contains(errorOf("primary axis trigger"), "the direction is missing after 'trigger'"));
    CHECK(contains(errorOf("primary press trigger +offhand"), "'+offhand' has no button after it"));

    // a number that is not one, and a word past the end of the line
    CHECK(contains(errorOf("primary longpress a 1.2s"), "'1.2s' is not a number, the duration in seconds"));
    CHECK(contains(errorOf("right axis trigger up hard"), "'hard' is not a number, the threshold"));
    CHECK(contains(errorOf("primary longpress a 1.2 extra"), "'extra' is past the end of the line, which is <hand> <type> <button> [duration]"));
    CHECK(contains(errorOf("primary thumbstick up 0.5 more"), "'more' is past the end of the line, which is <hand> thumbstick"));
    // a second modifier or suppress, wherever it is
    CHECK(contains(errorOf("primary press trigger +grip +a"), "'+a' is a second modifier"));
    CHECK(contains(errorOf("primary +grip press trigger +a 0.5"), "'+a' is a second modifier"));
    CHECK(contains(errorOf("primary press trigger suppress nosuppress"), "'nosuppress' is a second suppress or nosuppress"));
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

namespace
{
    /**
     * A binding read from the TOML text of its table, as a config has it.
     */
    std::expected<InputBinding, std::string> fromTable(const std::string& value)
    {
        const auto table = toml::parse("v = " + value);
        return parseInputBindingTable(*table.get("v"));
    }

    /**
     * Why the TOML text of a table is not a binding, or "(read)" when it is one.
     */
    std::string tableErrorOf(const std::string& value)
    {
        const auto binding = fromTable(value);
        return binding ? std::string("(read)") : binding.error();
    }
}

TEST_CASE("InputBindingParser: a table is the binding its line is, key by key")
{
    for (const auto& [table, line] : std::initializer_list<std::pair<std::string, std::string>>{
             { R"({ hand = "offhand", type = "tap", button = "trigger" })", "offhand tap trigger" },
             { R"({ hand = "offhand", type = "hold", button = "grip", duration = 0.3 })", "offhand holddown grip 0.3" },
             { R"({ hand = "left", type = "tap", button = "grip", suppress = true })", "left tap grip suppress" },
             { R"({ hand = "primary", type = "longpress", button = "thumbstick", duration = 1.5, modifier = { hand = "offhand", button = "thumbstick" } })",
                 "primary longpress thumbstick 1.5 +offhand:thumbstick" },
             { R"({ hand = "primary", type = "press", button = "trigger", modifier = { button = "grip" } })", "primary press trigger +grip" },
             { R"({ hand = "right", type = "axis", axis = "trigger", direction = "up", threshold = 0.7 })", "right axis trigger up 0.7" },
             // the axis is the thumbstick when left out, and the words are read with their aliases in any case
             { R"({ hand = "Primary", type = "axis", direction = "UP" })", "primary thumbstick up" },
             { R"({ hand = "main", type = "click", button = "menu" })", "primary press b" },
             { R"({ type = "none" })", "none" },
         }) {
        CAPTURE(table);
        const auto binding = fromTable(table);
        REQUIRE(binding);
        REQUIRE(*binding == parsed(line));
    }
}

TEST_CASE("InputBindingParser: a table that is not a binding says why, naming the key or the word that is wrong")
{
    for (const auto& [table, error] : std::initializer_list<std::pair<std::string, std::string>>{
             { R"("offhand tap grip")", R"(it is not a table, as { hand = "offhand", type = "tap", button = "grip" })" },
             { R"({ hand = "offhand", type = "tap", buton = "grip" })",
                 "it has the key 'buton', which is not one of hand, type, button, axis, direction, duration, threshold, suppress, modifier" },
             { R"({ hand = "offhand", button = "grip" })", "the type is missing, one of tap, press, longpress, double, hold, release, touch, axis, none" },
             { R"({ hand = "offhand", type = "squeeze", button = "grip" })", "'squeeze' is not a type, one of tap, press, longpress, double, hold, release, touch, axis, none" },
             { R"({ type = "tap", button = "grip" })", "the hand is missing, one of primary, offhand, left, right" },
             { R"({ hand = "middle", type = "tap", button = "grip" })", "'middle' is not a hand, one of primary, offhand, left, right" },
             { R"({ hand = "offhand", type = "tap" })", "the button is missing, one of trigger, grip, a, b, thumbstick, menu, system" },
             { R"({ hand = "offhand", type = "tap", button = 2 })", "button is not a string, one of trigger, grip, a, b, thumbstick, menu, system" },
             { R"({ hand = "offhand", type = "longpress", button = "grip", duration = "long" })", "duration is not a number" },
             { R"({ hand = "offhand", type = "tap", button = "grip", direction = "up" })",
                 "direction is not a key of a tap binding, which has hand, type, button, duration, suppress and modifier" },
             { R"({ hand = "offhand", type = "axis", button = "grip", direction = "up" })",
                 "button is not a key of a axis binding, which has hand, type, axis, direction, threshold, suppress and modifier" },
             { R"({ hand = "offhand", type = "axis" })", "the direction is missing, one of up, down, left, right" },
             { R"({ hand = "offhand", type = "tap", button = "grip", suppress = "yes" })", "suppress is not true or false" },
             { R"({ hand = "offhand", type = "tap", button = "grip", modifier = "grip" })", R"(modifier is not a table, as { hand = "offhand", button = "grip" })" },
             { R"({ hand = "offhand", type = "tap", button = "grip", modifier = { hand = "left" } })",
                 "modifier has no button, one of trigger, grip, a, b, thumbstick, menu, system" },
             { R"({ type = "none", hand = "left" })", "a none binding has no key but its type" },
         }) {
        CAPTURE(table);
        REQUIRE(tableErrorOf(table) == error);
    }
}

TEST_CASE("InputBindingParser: a binding's table text reads back as the binding, in its canonical words and with no default written")
{
    for (const auto& [line, table] : std::initializer_list<std::pair<std::string, std::string>>{
             { "offhand tap trigger", R"({ hand = "offhand", type = "tap", button = "trigger" })" },
             { "OFFHAND HOLDDOWN GRIP 0.3", R"({ hand = "offhand", type = "hold", button = "grip", duration = 0.3 })" },
             { "left tap grip suppress", R"({ hand = "left", type = "tap", button = "grip", suppress = true })" },
             { "primary longpress thumbstick 2 +offhand:thumbstick",
                 R"({ hand = "primary", type = "longpress", button = "thumbstick", duration = 2.0, modifier = { hand = "offhand", button = "thumbstick" } })" },
             { "primary press b +grip", R"({ hand = "primary", type = "press", button = "menu", modifier = { button = "grip" } })" },
             { "primary thumbstick up", R"({ hand = "primary", type = "axis", axis = "thumbstick", direction = "up" })" },
             { "right axis trigger down 0.5", R"({ hand = "right", type = "axis", axis = "trigger", direction = "down", threshold = 0.5 })" },
             { "none", R"({ type = "none" })" },
         }) {
        CAPTURE(line);
        const auto binding = parsed(line);
        REQUIRE(inputBindingTomlText(binding) == table);
        REQUIRE(fromTable(table) == binding);
    }
}
