#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "config/Overrides.h"

using f4cf::common::TomlEditor;
using f4cf::config::applyOverride;
using f4cf::config::ConfigKey;
using f4cf::config::nodeOf;
using f4cf::config::Overrides;

namespace
{
    const ConfigKey SCALE{ "Main", "fScale" };
    const ConfigKey MODE{ "Main", "iMode" };
    const ConfigKey LEVEL{ "Debug", "iLogLevel" };

    // the callers of the design: the devbench tool, another mod through the mod's API, and the config menu's preview
    const std::string DEVBENCH = "devbench";
    const std::string OTHER_MOD = "OtherMod";
    const std::string PREVIEW = "preview";

    /**
     * The value that applies for a key, or "none" when no owner overrides it.
     */
    std::string appliedValue(const Overrides& overrides, const ConfigKey& key)
    {
        return overrides.find(key).value_or("none");
    }

    /**
     * The value of a key in a loaded config as TOML text, or "none" when the config does not have the key.
     */
    std::string configValue(const toml::table& config, const ConfigKey& key)
    {
        const auto* node = config[key.section][key.key].node();
        return node ? TomlEditor::Value(*node).text() : "none";
    }

    /**
     * The mod's file as a load has it before the overrides are put in: a scale and a mode, and no log level.
     */
    toml::table loadFile()
    {
        return toml::parse("[Main]\nfScale = 1.0\niMode = 0\n");
    }
}

TEST_CASE("ConfigOverrides: an override applies until its owner clears it")
{
    Overrides overrides;
    REQUIRE(appliedValue(overrides, SCALE) == "none");

    REQUIRE(overrides.set(DEVBENCH, SCALE, "2"));
    REQUIRE(appliedValue(overrides, SCALE) == "2");
    REQUIRE(appliedValue(overrides, MODE) == "none");

    REQUIRE(overrides.clear(DEVBENCH, SCALE));
    REQUIRE(appliedValue(overrides, SCALE) == "none");
    REQUIRE(overrides.applied().empty());
}

TEST_CASE("ConfigOverrides: a new value of an owner's override is told from a new override")
{
    Overrides overrides;

    REQUIRE(overrides.set(PREVIEW, SCALE, "1.1"));
    REQUIRE_FALSE(overrides.set(PREVIEW, SCALE, "1.2"));
    REQUIRE_FALSE(overrides.set(PREVIEW, SCALE, "1.3"));
    REQUIRE(appliedValue(overrides, SCALE) == "1.3");

    // another key, and another owner on the same key, are new
    REQUIRE(overrides.set(PREVIEW, MODE, "1"));
    REQUIRE(overrides.set(DEVBENCH, SCALE, "5"));

    // one override of the key for the owner, however many values it had
    REQUIRE(overrides.clear(PREVIEW, SCALE));
    REQUIRE_FALSE(overrides.clear(PREVIEW, SCALE));
}

TEST_CASE("ConfigOverrides: with two owners on one key the one set last applies, and clearing either leaves the other's")
{
    Overrides overrides;
    overrides.set(OTHER_MOD, SCALE, "2");
    overrides.set(PREVIEW, SCALE, "3");
    REQUIRE(appliedValue(overrides, SCALE) == "3");

    SECTION("the one that applies is cleared: the other applies again")
    {
        REQUIRE(overrides.clear(PREVIEW, SCALE));
        REQUIRE(appliedValue(overrides, SCALE) == "2");

        REQUIRE(overrides.clear(OTHER_MOD, SCALE));
        REQUIRE(appliedValue(overrides, SCALE) == "none");
    }
    SECTION("the other is cleared: the one that applies still does")
    {
        REQUIRE(overrides.clear(OTHER_MOD, SCALE));
        REQUIRE(appliedValue(overrides, SCALE) == "3");

        REQUIRE(overrides.clear(PREVIEW, SCALE));
        REQUIRE(appliedValue(overrides, SCALE) == "none");
    }
    SECTION("the first owner sets a new value: it is the one set last")
    {
        REQUIRE_FALSE(overrides.set(OTHER_MOD, SCALE, "4"));
        REQUIRE(appliedValue(overrides, SCALE) == "4");

        REQUIRE(overrides.clear(OTHER_MOD, SCALE));
        REQUIRE(appliedValue(overrides, SCALE) == "3");
    }
}

TEST_CASE("ConfigOverrides: an owner clears only its own override")
{
    Overrides overrides;
    overrides.set(OTHER_MOD, SCALE, "2");

    REQUIRE_FALSE(overrides.clear(PREVIEW, SCALE));
    REQUIRE_FALSE(overrides.clear(PREVIEW, MODE));
    REQUIRE(appliedValue(overrides, SCALE) == "2");
}

TEST_CASE("ConfigOverrides: an owner's clear of all leaves the other owners' overrides")
{
    Overrides overrides;
    overrides.set(DEVBENCH, SCALE, "2");
    overrides.set(DEVBENCH, MODE, "1");
    overrides.set(OTHER_MOD, LEVEL, "0");
    overrides.set(OTHER_MOD, MODE, "7");
    overrides.set(DEVBENCH, MODE, "9");
    REQUIRE(appliedValue(overrides, MODE) == "9");

    REQUIRE(overrides.clearAll(DEVBENCH) == 2);

    REQUIRE(appliedValue(overrides, SCALE) == "none");
    REQUIRE(appliedValue(overrides, MODE) == "7");
    REQUIRE(appliedValue(overrides, LEVEL) == "0");

    REQUIRE(overrides.clearAll(DEVBENCH) == 0);
    REQUIRE(overrides.clearAll(PREVIEW) == 0);
    REQUIRE(overrides.clearAll(OTHER_MOD) == 2);
    REQUIRE(overrides.applied().empty());
}

TEST_CASE("ConfigOverrides: several overrides are set at once, each value in its text form, and the new ones are returned")
{
    Overrides overrides;
    overrides.set(PREVIEW, SCALE, "1.5");
    overrides.set(OTHER_MOD, MODE, "7");

    const auto added = overrides.set(PREVIEW, { { "Main", "fScale", 2.5f }, { "Main", "iMode", 3 }, { "Debug", "iLogLevel", "0" }, { "Main", "bEnabled", true } });

    // the scale was the owner's already, so it only has a new value; the mode is new for this owner
    REQUIRE(added.size() == 3);
    REQUIRE(added[0].section == "Main");
    REQUIRE(added[0].key == "iMode");
    REQUIRE(added[0].value == "3");
    REQUIRE(added[0].owner == PREVIEW);
    REQUIRE(added[1].key == "iLogLevel");
    REQUIRE(added[1].value == "0");
    REQUIRE(added[2].key == "bEnabled");
    REQUIRE(added[2].value == "true");

    REQUIRE(appliedValue(overrides, SCALE) == "2.5");
    REQUIRE(appliedValue(overrides, MODE) == "3");
    REQUIRE(appliedValue(overrides, LEVEL) == "0");
    REQUIRE(appliedValue(overrides, { "Main", "bEnabled" }) == "true");

    SECTION("a key that is twice in the list is new once, and has its last value")
    {
        const ConfigKey speed{ "Main", "fSpeed" };
        const auto addedTwice = overrides.set(PREVIEW, { { "Main", "fSpeed", 1.0f }, { "Main", "fSpeed", 2.0f } });

        REQUIRE(addedTwice.size() == 1);
        REQUIRE(appliedValue(overrides, speed) == "2");
    }
    SECTION("an empty list sets nothing")
    {
        REQUIRE(overrides.set(PREVIEW, std::vector<f4cf::config::ConfigEntry>{}).empty());
        REQUIRE(overrides.applied().size() == 4);
    }
}

TEST_CASE("ConfigOverrides: several keys are cleared at once, and the ones the owner had are returned")
{
    Overrides overrides;
    overrides.set(PREVIEW, SCALE, "3");
    overrides.set(PREVIEW, LEVEL, "0");
    overrides.set(OTHER_MOD, MODE, "7");

    // the mode is another owner's, so it is skipped and stays
    const auto cleared = overrides.clear(PREVIEW, { LEVEL, MODE, SCALE });

    REQUIRE(cleared == std::vector{ LEVEL, SCALE });
    REQUIRE(appliedValue(overrides, SCALE) == "none");
    REQUIRE(appliedValue(overrides, LEVEL) == "none");
    REQUIRE(appliedValue(overrides, MODE) == "7");

    REQUIRE(overrides.clear(PREVIEW, { LEVEL, MODE, SCALE }).empty());
    REQUIRE(overrides.clear(PREVIEW, std::vector<ConfigKey>{}).empty());
}

TEST_CASE("ConfigOverrides: the overrides are put into a loaded config in place of the file's values")
{
    Overrides overrides;
    auto config = loadFile();

    SECTION("with no override the config is as the file")
    {
        overrides.applyTo(config);

        REQUIRE(configValue(config, SCALE) == "1.0");
        REQUIRE(configValue(config, MODE) == "0");
        REQUIRE(configValue(config, LEVEL) == "none");
    }
    SECTION("an overridden key has the override in the type of the file's value, and the others the file's")
    {
        overrides.set(PREVIEW, SCALE, "2");
        overrides.set(PREVIEW, MODE, "3");
        overrides.applyTo(config);

        REQUIRE(configValue(config, SCALE) == "2.0");
        REQUIRE(config["Main"]["fScale"].is_floating_point());
        REQUIRE(configValue(config, MODE) == "3");
        REQUIRE(config["Main"]["iMode"].is_integer());
    }
    SECTION("a value that is not of the file's type is a string, which the mod's read of the type does not take")
    {
        overrides.set(PREVIEW, SCALE, "big");
        overrides.applyTo(config);

        REQUIRE(configValue(config, SCALE) == "\"big\"");
        REQUIRE_FALSE(config["Main"]["fScale"].value<double>());
    }
    SECTION("a key and a section that the file does not have are added, as the TOML the value is")
    {
        overrides.set(DEVBENCH, LEVEL, "0");
        overrides.set(DEVBENCH, { "Main", "sButton" }, "left tap grip");
        overrides.applyTo(config);

        REQUIRE(configValue(config, LEVEL) == "0");
        REQUIRE(configValue(config, { "Main", "sButton" }) == "\"left tap grip\"");
        REQUIRE(configValue(config, SCALE) == "1.0");
    }
    SECTION("with two owners on a key it is the one set last")
    {
        overrides.set(OTHER_MOD, SCALE, "2");
        overrides.set(PREVIEW, SCALE, "3");
        overrides.applyTo(config);

        REQUIRE(configValue(config, SCALE) == "3.0");
    }
    SECTION("a cleared override is not in the next load")
    {
        overrides.set(OTHER_MOD, SCALE, "2");
        overrides.set(PREVIEW, SCALE, "3");
        overrides.clear(PREVIEW, SCALE);

        auto nextLoad = loadFile();
        overrides.applyTo(nextLoad);
        REQUIRE(configValue(nextLoad, SCALE) == "2.0");

        overrides.clear(OTHER_MOD, SCALE);

        auto lastLoad = loadFile();
        overrides.applyTo(lastLoad);
        REQUIRE(configValue(lastLoad, SCALE) == "1.0");
    }
}

TEST_CASE("ConfigOverrides: the applied overrides are one for each key, with the owner that set it")
{
    Overrides overrides;
    overrides.set(OTHER_MOD, SCALE, "2");
    overrides.set(PREVIEW, SCALE, "3");
    overrides.set(DEVBENCH, LEVEL, "0");

    const auto applied = overrides.applied();

    // ordered by section and key
    REQUIRE(applied.size() == 2);
    REQUIRE(applied[0].section == "Debug");
    REQUIRE(applied[0].key == "iLogLevel");
    REQUIRE(applied[0].value == "0");
    REQUIRE(applied[0].owner == DEVBENCH);
    REQUIRE(applied[1].section == "Main");
    REQUIRE(applied[1].key == "fScale");
    REQUIRE(applied[1].value == "3");
    REQUIRE(applied[1].owner == PREVIEW);
}

TEST_CASE("ConfigKey: the section and the key are TOML dotted keys, whose keys make the path from the root")
{
    using Path = std::vector<std::string>;

    REQUIRE(ConfigKey{ "Main", "fScale" }.path() == Path{ "Main", "fScale" });
    REQUIRE(ConfigKey{ "Main", "hand.x" }.path() == Path{ "Main", "hand", "x" });
    REQUIRE(ConfigKey{ "Main.hand", "x" }.path() == Path{ "Main", "hand", "x" });
    REQUIRE(ConfigKey{ "Main", "hand . x" }.path() == Path{ "Main", "hand", "x" });
    // a name with a dot in it is in quotes, as in TOML
    REQUIRE(ConfigKey{ "Main", "\"my.key\"" }.path() == Path{ "Main", "my.key" });
    // an empty section is the root
    REQUIRE(ConfigKey{ "", "iVersion" }.path() == Path{ "iVersion" });

    REQUIRE_FALSE(ConfigKey{ "Main", "" }.path());
    REQUIRE_FALSE(ConfigKey{ "Main", "two words" }.path());
    REQUIRE_FALSE(ConfigKey{ "Main", "x = 1" }.path());
}

TEST_CASE("ConfigKey: a value in a nested table and in an inline table is found by its path")
{
    const auto config = toml::parse("[Main]\ng = { x = 1 }\n\n[Main.hand]\nfX = 1.5\n");

    REQUIRE(TomlEditor::Value(*nodeOf(config, { "Main", "hand.fX" })).text() == "1.5");
    REQUIRE(TomlEditor::Value(*nodeOf(config, { "Main", "g.x" })).text() == "1");
    REQUIRE_FALSE(nodeOf(config, { "Main", "hand.fY" }));
    REQUIRE_FALSE(nodeOf(config, { "Main", "g.x.y" }));
}

TEST_CASE("ConfigOverrides: an override of a value in a nested table is put by its path, in the file's type")
{
    auto config = toml::parse("[Main]\ng = { x = 1 }\nfScale = 1.0\n\n[Main.hand]\nfX = 1.5\n");

    REQUIRE(applyOverride(config, { "Main", "hand.fX" }, "2"));
    REQUIRE(applyOverride(config, { "Main", "g.x" }, "5"));
    REQUIRE(TomlEditor::Value(*nodeOf(config, { "Main", "hand.fX" })).text() == "2.0");
    REQUIRE(TomlEditor::Value(*nodeOf(config, { "Main", "g.x" })).text() == "5");

    // a table the file does not have is added
    REQUIRE(applyOverride(config, { "Main", "arm.fY" }, "3.5"));
    REQUIRE(TomlEditor::Value(*nodeOf(config, { "Main", "arm.fY" })).text() == "3.5");

    // a path through a value that is not a table, and a key that is not a TOML key, are left out
    REQUIRE_FALSE(applyOverride(config, { "Main", "fScale.x" }, "1"));
    REQUIRE_FALSE(applyOverride(config, { "Main", "two words" }, "1"));
    REQUIRE(TomlEditor::Value(*nodeOf(config, { "Main", "fScale" })).text() == "1.0");
}

TEST_CASE("ConfigKey: two spellings of one path are one key, its first key the section")
{
    REQUIRE(ConfigKey{ "Main.hand", "x" }.canonical() == ConfigKey{ "Main", "hand.x" });
    REQUIRE(ConfigKey{ "Main", "hand . x" }.canonical() == ConfigKey{ "Main", "hand.x" });
    REQUIRE(ConfigKey{ "Main", "\"my.key\"" }.canonical() == ConfigKey{ "Main", "\"my.key\"" });
    REQUIRE(ConfigKey{ "Main", "fScale" }.canonical() == ConfigKey{ "Main", "fScale" });
    REQUIRE(ConfigKey{ "", "iVersion" }.canonical() == ConfigKey{ "", "iVersion" });
}

TEST_CASE("ConfigOverrides: two spellings of one key's path are one override")
{
    Overrides overrides;

    REQUIRE(overrides.set(DEVBENCH, { "Main", "hand.x" }, "1.5"));
    // the same owner's override of the same value, so not a new one
    REQUIRE_FALSE(overrides.set(DEVBENCH, { "Main.hand", "x" }, "2.5"));

    REQUIRE(appliedValue(overrides, { "Main", "hand . x" }) == "2.5");
    REQUIRE(overrides.applied().size() == 1);
    REQUIRE(overrides.clear(DEVBENCH, ConfigKey{ "Main.hand", "x" }));
    REQUIRE(appliedValue(overrides, { "Main", "hand.x" }) == "none");
}

TEST_CASE("ConfigOverrides: a key the config does not have means the one beside it with another type prefix")
{
    const auto config = toml::parse("[Main]\nkOpen = 1\nfScale = 1.0\niScale = 1\nhand.kGrab = 1\n");
    using f4cf::config::keyWithOtherPrefix;

    REQUIRE(keyWithOtherPrefix(config, { "Main", "sOpen" }) == ConfigKey{ "Main", "kOpen" });
    REQUIRE(keyWithOtherPrefix(config, { "Main", "hand.sGrab" }) == ConfigKey{ "Main", "hand.kGrab" });
    // a key it has is itself, as is one with two keys of other prefixes, one with none, and one with no prefix
    REQUIRE(keyWithOtherPrefix(config, { "Main", "kOpen" }) == ConfigKey{ "Main", "kOpen" });
    REQUIRE(keyWithOtherPrefix(config, { "Main", "sScale" }) == ConfigKey{ "Main", "sScale" });
    REQUIRE(keyWithOtherPrefix(config, { "Main", "sClose" }) == ConfigKey{ "Main", "sClose" });
    REQUIRE(keyWithOtherPrefix(config, { "Main", "open" }) == ConfigKey{ "Main", "open" });
}
