#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <limits>
#include <string>

#include "config/IniValue.h"

using f4cf::config::IniValue;

namespace
{
    constexpr auto SECTION_NAME = "Main";
    constexpr auto KEY = "value";

    /**
     * An INI with the value in its string form, as a session override puts it into a loaded INI.
     */
    void setAsString(CSimpleIniA& ini, const IniValue& value)
    {
        ini.SetValue(SECTION_NAME, KEY, value.toString().c_str());
    }

    std::string text(const CSimpleIniA& ini)
    {
        return ini.GetValue(SECTION_NAME, KEY, "none");
    }
}

TEST_CASE("ConfigIniValue: the string form of each type")
{
    REQUIRE(IniValue(true).toString() == "true");
    REQUIRE(IniValue(false).toString() == "false");
    REQUIRE(IniValue(42).toString() == "42");
    REQUIRE(IniValue(-7).toString() == "-7");
    REQUIRE(IniValue(2.5f).toString() == "2.5");
    REQUIRE(IniValue(1.0f).toString() == "1");
    REQUIRE(IniValue(0.1f).toString() == "0.1");
    REQUIRE(IniValue(std::string("some text")).toString() == "some text");

    // a string literal is a string, and not the bool a pointer converts to
    REQUIRE(IniValue("grip").toString() == "grip");
    REQUIRE(IniValue("").toString().empty());
}

TEST_CASE("ConfigIniValue: the string form reads back through SimpleIni's getters as the same value")
{
    CSimpleIniA ini;

    SECTION("a bool")
    {
        setAsString(ini, true);
        REQUIRE(ini.GetBoolValue(SECTION_NAME, KEY, false) == true);
        setAsString(ini, false);
        REQUIRE(ini.GetBoolValue(SECTION_NAME, KEY, true) == false);
    }
    SECTION("an int")
    {
        // in parentheses, as SimpleIni brings in the max and min macros of Windows
        for (const int value : { 0, 1, -1, 42, -100000, (std::numeric_limits<int>::max)(), (std::numeric_limits<int>::min)() }) {
            setAsString(ini, value);
            REQUIRE(ini.GetLongValue(SECTION_NAME, KEY, 12345) == value);
        }
    }
    SECTION("a float, with all of its digits")
    {
        for (const float value : { 0.0f, 1.0f, -1.0f, 0.1f, 2.5f, -3.75f, 0.05f, 1.0f / 3.0f, 1e-7f, 123456.789f, 3.4e38f }) {
            setAsString(ini, value);
            REQUIRE(static_cast<float>(ini.GetDoubleValue(SECTION_NAME, KEY, 999.0)) == value);
        }
    }
    SECTION("a string")
    {
        setAsString(ini, "primary longpress thumbstick 1.5");
        REQUIRE(text(ini) == "primary longpress thumbstick 1.5");
    }
}

TEST_CASE("ConfigIniValue: a value is written to the INI with the setter of its type, and reads back")
{
    CSimpleIniA ini;

    IniValue(true).applyTo(ini, SECTION_NAME, KEY);
    REQUIRE(text(ini) == "true");
    REQUIRE(ini.GetBoolValue(SECTION_NAME, KEY, false) == true);

    IniValue(false).applyTo(ini, SECTION_NAME, KEY);
    REQUIRE(text(ini) == "false");

    IniValue(-42).applyTo(ini, SECTION_NAME, KEY);
    REQUIRE(text(ini) == "-42");
    REQUIRE(ini.GetLongValue(SECTION_NAME, KEY, 0) == -42);

    // SimpleIni writes a number with six decimals
    IniValue(2.5f).applyTo(ini, SECTION_NAME, KEY);
    REQUIRE(text(ini) == "2.500000");
    REQUIRE(ini.GetDoubleValue(SECTION_NAME, KEY, 0.0) == 2.5);

    IniValue("grip").applyTo(ini, SECTION_NAME, KEY);
    REQUIRE(text(ini) == "grip");
}
