#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <optional>
#include <string>
#include <vector>

#include "config/IniMigration.h"

using f4cf::config::MigratedValue;
using f4cf::config::migrateIniValues;

namespace
{
    using Outcome = MigratedValue::Outcome;

    // The INI a newer version of the mod ships: a scale and a mode with their comments, a color that is new in
    // this version, and the version. It no longer has the speed.
    const std::string SHIPPED =
        "[Main]\n"
        "# How big the menu is\n"
        "fScale = 1\n"
        "\n"
        "# 0 = hand, 1 = world\n"
        "iMode = 0\n"
        "sColor = 255,255,255\n"
        "\n"
        "[Debug]\n"
        "iVersion = 7\n";

    // The user's INI of the version before: the scale and the speed changed, the mode as it was shipped, and a
    // section that the newer version no longer has.
    const std::string USERS =
        "[Main]\n"
        "fScale = 1.5\n"
        "iMode = 0\n"
        "fSpeed = 3\n"
        "\n"
        "[Old]\n"
        "bLegacy = true\n"
        "\n"
        "[Debug]\n"
        "iVersion = 6\n";

    void load(CSimpleIniA& ini, const std::string& text)
    {
        REQUIRE(ini.LoadData(text) >= 0);
    }

    /**
     * The value of a key in an INI, or "none" when the INI does not have the key.
     */
    std::string value(const CSimpleIniA& ini, const char* section, const char* key)
    {
        return ini.GetValue(section, key, "none");
    }

    /**
     * What the migration did with a value of the user's INI, or no value when it did not report the key.
     */
    std::optional<Outcome> outcomeOf(const std::vector<MigratedValue>& values, const std::string& section, const std::string& key)
    {
        for (const auto& entry : values) {
            if (entry.section == section && entry.key == key) {
                return entry.outcome;
            }
        }
        return std::nullopt;
    }

    /**
     * The text of an INI as a save writes it, with every line end as "\n".
     */
    std::string saved(const CSimpleIniA& ini)
    {
        std::string text;
        REQUIRE(ini.Save(text) >= 0);
        std::erase(text, '\r');
        return text;
    }

    /**
     * Put one text in place of another, which the text has to hold.
     */
    void replace(std::string& text, const std::string& from, const std::string& to)
    {
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
    }
}

TEST_CASE("ConfigIniMigration: a value the user changed is set in the new INI")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);

    const auto values = migrateIniValues(users, shipped);

    REQUIRE(value(shipped, "Main", "fScale") == "1.5");
    REQUIRE(outcomeOf(values, "Main", "fScale") == Outcome::Carried);
}

TEST_CASE("ConfigIniMigration: a value the user left as shipped, and a key that is new, keep the new INI's value")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);

    const auto values = migrateIniValues(users, shipped);

    REQUIRE(value(shipped, "Main", "iMode") == "0");
    REQUIRE(outcomeOf(values, "Main", "iMode") == Outcome::Unchanged);

    // the user's INI does not have the color, so there is nothing to report for it
    REQUIRE(value(shipped, "Main", "sColor") == "255,255,255");
    REQUIRE_FALSE(outcomeOf(values, "Main", "sColor"));
}

TEST_CASE("ConfigIniMigration: a key and a section that the new INI no longer has are dropped")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);

    const auto values = migrateIniValues(users, shipped);

    REQUIRE(value(shipped, "Main", "fSpeed") == "none");
    REQUIRE(outcomeOf(values, "Main", "fSpeed") == Outcome::Removed);

    REQUIRE(value(shipped, "Old", "bLegacy") == "none");
    REQUIRE(shipped.GetSectionSize("Old") == -1);
    REQUIRE(outcomeOf(values, "Old", "bLegacy") == Outcome::Removed);
}

TEST_CASE("ConfigIniMigration: every value of the user's INI is reported, with the value it had")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);

    const auto values = migrateIniValues(users, shipped);

    REQUIRE(values.size() == 5);
    for (const auto& entry : values) {
        REQUIRE(entry.value == value(users, entry.section.c_str(), entry.key.c_str()));
    }
}

TEST_CASE("ConfigIniMigration: the new INI keeps its comments and the order of its keys")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);

    auto expected = saved(shipped);

    migrateIniValues(users, shipped);

    // the text it had, but for the two values that were carried
    replace(expected, "fScale = 1\n", "fScale = 1.5\n");
    replace(expected, "iVersion = 7\n", "iVersion = 6\n");
    REQUIRE(saved(shipped) == expected);
    REQUIRE(expected.contains("# How big the menu is\nfScale = 1.5\n"));
}

TEST_CASE("ConfigIniMigration: the version is carried as any value, for the caller to set the new one after")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);

    const auto values = migrateIniValues(users, shipped);

    REQUIRE(value(shipped, "Debug", "iVersion") == "6");
    REQUIRE(outcomeOf(values, "Debug", "iVersion") == Outcome::Carried);
}

TEST_CASE("ConfigIniMigration: the user's INI is left as it was")
{
    CSimpleIniA users, shipped;
    load(users, USERS);
    load(shipped, SHIPPED);
    const auto before = saved(users);

    migrateIniValues(users, shipped);

    REQUIRE(saved(users) == before);
}

TEST_CASE("ConfigIniMigration: an INI with the values of the new one carries nothing")
{
    CSimpleIniA users, shipped;
    load(users, SHIPPED);
    load(shipped, SHIPPED);
    const auto before = saved(shipped);

    const auto values = migrateIniValues(users, shipped);

    REQUIRE(saved(shipped) == before);
    for (const auto& entry : values) {
        REQUIRE(entry.outcome == Outcome::Unchanged);
    }
}
