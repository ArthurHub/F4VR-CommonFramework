#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <string>
#include <vector>

#include "ConfigOverrides.h"

using f4cf::config::IniKey;
using f4cf::config::Overrides;

namespace
{
    const IniKey SCALE{ "Main", "fScale" };
    const IniKey MODE{ "Main", "iMode" };
    const IniKey LEVEL{ "Debug", "iLogLevel" };

    // the callers of the design: the devbench tool, another mod through the mod's API, and the config menu's preview
    const std::string DEVBENCH = "devbench";
    const std::string OTHER_MOD = "OtherMod";
    const std::string PREVIEW = "preview";

    /**
     * The value that applies for a key, or "none" when no owner overrides it.
     */
    std::string appliedValue(const Overrides& overrides, const IniKey& key)
    {
        return overrides.find(key).value_or("none");
    }

    /**
     * The value of a key in an INI as the mod reads it, or "none" when the INI does not have the key.
     */
    std::string iniValue(const CSimpleIniA& ini, const IniKey& key)
    {
        return ini.GetValue(key.section.c_str(), key.key.c_str(), "none");
    }

    /**
     * The mod's file as a load has it before the overrides are put in: a scale and a mode, and no log level.
     */
    void loadFile(CSimpleIniA& ini)
    {
        REQUIRE(ini.LoadData(std::string("[Main]\nfScale = 1\niMode = 0\n")) >= 0);
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
    REQUIRE(overrides.clear(PREVIEW, std::vector<IniKey>{}).empty());
}

TEST_CASE("ConfigOverrides: the overrides are put into a loaded INI in place of the file's values")
{
    Overrides overrides;
    CSimpleIniA ini;
    loadFile(ini);

    SECTION("with no override the INI is as the file")
    {
        overrides.applyTo(ini);

        REQUIRE(iniValue(ini, SCALE) == "1");
        REQUIRE(iniValue(ini, MODE) == "0");
        REQUIRE(iniValue(ini, LEVEL) == "none");
    }
    SECTION("an overridden key has the override, and the others the file's")
    {
        overrides.set(PREVIEW, SCALE, "2.5");
        overrides.applyTo(ini);

        REQUIRE(iniValue(ini, SCALE) == "2.5");
        REQUIRE(ini.GetDoubleValue("Main", "fScale", 0) == 2.5);
        REQUIRE(iniValue(ini, MODE) == "0");
    }
    SECTION("a key and a section that the file does not have are added")
    {
        overrides.set(DEVBENCH, LEVEL, "0");
        overrides.applyTo(ini);

        REQUIRE(iniValue(ini, LEVEL) == "0");
        REQUIRE(iniValue(ini, SCALE) == "1");
    }
    SECTION("with two owners on a key it is the one set last")
    {
        overrides.set(OTHER_MOD, SCALE, "2");
        overrides.set(PREVIEW, SCALE, "3");
        overrides.applyTo(ini);

        REQUIRE(iniValue(ini, SCALE) == "3");
    }
    SECTION("a cleared override is not in the next load")
    {
        overrides.set(OTHER_MOD, SCALE, "2");
        overrides.set(PREVIEW, SCALE, "3");
        overrides.clear(PREVIEW, SCALE);

        CSimpleIniA nextLoad;
        loadFile(nextLoad);
        overrides.applyTo(nextLoad);
        REQUIRE(iniValue(nextLoad, SCALE) == "2");

        overrides.clear(OTHER_MOD, SCALE);

        CSimpleIniA lastLoad;
        loadFile(lastLoad);
        overrides.applyTo(lastLoad);
        REQUIRE(iniValue(lastLoad, SCALE) == "1");
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
