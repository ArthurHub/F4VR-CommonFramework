#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

#include "config/ConfigValue.h"

using f4cf::common::TomlEditor;
using f4cf::config::appliedText;
using f4cf::config::ConfigValue;
using f4cf::config::typedLike;

namespace
{
    // a file with a value of each type
    const toml::table VALUES = toml::parse(R"(
b = true
i = 3
f = 1.5
s = "grip"
a = [1, 2]
)");

    const toml::node* existing(const char* key)
    {
        return VALUES.get(key);
    }

    /**
     * The value a text is taken as, as TOML text, or "none" when it is not of the existing value's type.
     */
    std::string typed(const toml::node* node, const std::string_view text)
    {
        const auto value = typedLike(node, text);
        return value ? TomlEditor::Value(*value->get("v")).text() : "none";
    }

    /**
     * The TOML text a value is written as over an existing one, or "none" when it can't be.
     */
    std::string written(const ConfigValue& value, const toml::node* node)
    {
        const auto toml = value.toTomlLike(node);
        return toml ? toml->text() : "none";
    }
}

TEST_CASE("ConfigValue: the text form of each type")
{
    REQUIRE(ConfigValue(true).toString() == "true");
    REQUIRE(ConfigValue(false).toString() == "false");
    REQUIRE(ConfigValue(42).toString() == "42");
    REQUIRE(ConfigValue(-7).toString() == "-7");
    REQUIRE(ConfigValue(2.5f).toString() == "2.5");
    REQUIRE(ConfigValue(1.0f).toString() == "1");
    REQUIRE(ConfigValue(0.1f).toString() == "0.1");
    REQUIRE(ConfigValue(std::string("some text")).toString() == "some text");

    // a string literal is a string, and not the bool a pointer converts to
    REQUIRE(ConfigValue("grip").toString() == "grip");
    REQUIRE(ConfigValue("").toString().empty());
}

TEST_CASE("ConfigValue: the text form reads back in the type of the key as the same value")
{
    SECTION("a bool")
    {
        REQUIRE(typed(existing("b"), ConfigValue(true).toString()) == "true");
        REQUIRE(typed(existing("b"), ConfigValue(false).toString()) == "false");
    }
    SECTION("an int")
    {
        for (const int value : { 0, 1, -1, 42, -100000, (std::numeric_limits<int>::max)(), (std::numeric_limits<int>::min)() }) {
            REQUIRE(typed(existing("i"), ConfigValue(value).toString()) == std::to_string(value));
        }
    }
    SECTION("a float, with all of its digits")
    {
        for (const float value : { 0.0f, 1.0f, -1.0f, 0.1f, 2.5f, -3.75f, 0.05f, 1.0f / 3.0f, 1e-7f, 123456.789f, 3.4e38f }) {
            const auto read = typedLike(existing("f"), ConfigValue(value).toString());
            REQUIRE(read);
            REQUIRE(static_cast<float>(read->get("v")->as_floating_point()->get()) == value);
        }
    }
    SECTION("a string")
    {
        REQUIRE(typed(existing("s"), ConfigValue("primary longpress thumbstick 1.5").toString()) == "\"primary longpress thumbstick 1.5\"");
    }
}

TEST_CASE("ConfigValue: a text is taken in the type of the file's value for its key")
{
    SECTION("a bool as a person writes it")
    {
        for (const auto* text : { "true", "TRUE", "yes", "on", "1", " true " }) {
            CAPTURE(text);
            REQUIRE(typed(existing("b"), text) == "true");
        }
        for (const auto* text : { "false", "No", "off", "0" }) {
            CAPTURE(text);
            REQUIRE(typed(existing("b"), text) == "false");
        }
        REQUIRE(typed(existing("b"), "maybe") == "none");
    }
    SECTION("a number")
    {
        REQUIRE(typed(existing("i"), "7") == "7");
        REQUIRE(typed(existing("i"), "+7") == "7");
        REQUIRE(typed(existing("i"), "1.5") == "none");
        REQUIRE(typed(existing("i"), "seven") == "none");
        REQUIRE(typed(existing("f"), "1.230") == "1.23");
        REQUIRE(typed(existing("f"), "2") == "2.0");
        REQUIRE(typed(existing("f"), "") == "none");
        REQUIRE(typed(existing("f"), "1.5x") == "none");
    }
    SECTION("a string as it is, quotes and all")
    {
        REQUIRE(typed(existing("s"), "left \"tap\" grip") == R"("left \"tap\" grip")");
        REQUIRE(typed(existing("s"), "1.5") == "\"1.5\"");
    }
    SECTION("an array as TOML, of its type only")
    {
        REQUIRE(typed(existing("a"), "[3, 4]") == "[3, 4]");
        REQUIRE(typed(existing("a"), "3") == "none");
        REQUIRE(typed(existing("a"), "[3") == "none");
    }
    SECTION("with no value to take the type of: TOML, and a string when it is not")
    {
        REQUIRE(typed(nullptr, "1.5") == "1.5");
        REQUIRE(typed(nullptr, "3") == "3");
        REQUIRE(typed(nullptr, "true") == "true");
        REQUIRE(typed(nullptr, "[1, 2]") == "[1, 2]");
        REQUIRE(typed(nullptr, "offhand tap grip") == "\"offhand tap grip\"");
        REQUIRE(typed(nullptr, "") == "\"\"");
    }
}

TEST_CASE("ConfigValue: a value is written over the file's in its type")
{
    SECTION("a typed value as it is, and an int over a float as a float")
    {
        REQUIRE(written(true, existing("b")) == "true");
        REQUIRE(written(5, existing("i")) == "5");
        REQUIRE(written(5, existing("f")) == "5.0");
        REQUIRE(written(0.1f, existing("f")) == "0.1");
        REQUIRE(written(0.1f, nullptr) == "0.1");
    }
    SECTION("a string in the file's type, or not at all when it is not of it")
    {
        REQUIRE(written("1.230", existing("f")) == "1.23");
        REQUIRE(written("on", existing("b")) == "true");
        REQUIRE(written("abc", existing("f")) == "none");
        REQUIRE(written("grip", existing("s")) == "\"grip\"");
        REQUIRE(written("grip", nullptr) == "\"grip\"");
        REQUIRE(written("", existing("s")) == "\"\"");
    }
}

TEST_CASE("ConfigValue: a text as it applies over the file's value, in text again")
{
    REQUIRE(appliedText(existing("b"), "no") == "false");
    REQUIRE(appliedText(existing("i"), "07") == "7");
    REQUIRE(appliedText(existing("f"), "1") == "1.0");
    // a string as it is, not in TOML's quotes
    REQUIRE(appliedText(existing("s"), "offhand tap grip") == "offhand tap grip");
    REQUIRE(appliedText(existing("a"), "[3]") == "[3]");
    REQUIRE(appliedText(nullptr, "1.5") == "1.5");
    REQUIRE(appliedText(nullptr, "offhand tap grip") == "offhand tap grip");

    REQUIRE_FALSE(appliedText(existing("f"), "abc"));
    REQUIRE_FALSE(appliedText(existing("i"), "1.5"));
    REQUIRE_FALSE(appliedText(existing("b"), "maybe"));
}

TEST_CASE("ConfigValue: a binding takes its line, written as its table")
{
    const auto file = toml::parse(R"(k = { hand = "primary", type = "press", button = "a" })"
                                  "\n"
                                  "t = { pos = [1.0, 2.0, 3.0] }\n");
    const auto* binding = file.get("k");

    REQUIRE(appliedText(binding, "offhand longpress grip 1.5") == R"({ hand = "offhand", type = "longpress", button = "grip", duration = 1.5 })");
    REQUIRE(appliedText(binding, "none") == R"({ type = "none" })");
    // its table as TOML, as it is
    REQUIRE(appliedText(binding, R"({ hand = "left", type = "tap", button = "b" })") == R"({ hand = "left", type = "tap", button = "b" })");

    // a line that is not a binding, and a line over a table that is not a binding
    REQUIRE_FALSE(appliedText(binding, "offhand squeeze grip"));
    REQUIRE_FALSE(appliedText(file.get("t"), "none"));
}
