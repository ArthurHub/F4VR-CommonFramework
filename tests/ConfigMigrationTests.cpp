#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "config/ConfigMigration.h"

using f4cf::common::TomlEditor;
using f4cf::config::MigratedValue;
using f4cf::config::migrateValues;

namespace
{
    using Outcome = MigratedValue::Outcome;

    // The config a newer version of the mod ships: a scale and a mode with their comments, a color that is new in
    // this version, a list that was a string before, and the version. It no longer has the speed.
    const std::string SHIPPED =
        "[Main]\n"
        "# How big the menu is\n"
        "fScale = 1.0\n"
        "\n"
        "# 0 = hand, 1 = world\n"
        "iMode = 0\n"
        "sColor = \"255,255,255\"\n"
        "aChannels = [\"hands\"]\n"
        "\n"
        "[Debug]\n"
        "iVersion = 7\n";

    // The user's config of the version before: the scale and the speed changed, the mode as it was shipped, an
    // integer for the float, the list as a string, and a section that the newer version no longer has.
    const std::string USERS =
        "[Main]\n"
        "fScale = 2\n"
        "iMode = 0\n"
        "fSpeed = 3.0\n"
        "aChannels = \"hands, ik\"\n"
        "\n"
        "[Old]\n"
        "bLegacy = true\n"
        "\n"
        "[Debug]\n"
        "iVersion = 6\n";

    /**
     * What the migration did with a value of the user's config, or no value when it did not report the key.
     */
    std::optional<Outcome> outcomeOf(const std::vector<MigratedValue>& values, const std::string& path)
    {
        for (const auto& entry : values) {
            if (entry.pathText() == path) {
                return entry.outcome;
            }
        }
        return std::nullopt;
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

    struct Migrated
    {
        TomlEditor latest{ SHIPPED };
        std::vector<MigratedValue> values;

        explicit Migrated(const std::string& users)
        {
            REQUIRE(latest.ok());
            values = migrateValues(toml::parse(users), latest);
        }
    };
}

TEST_CASE("ConfigMigration: a value the user changed is set in the new config, an integer for a float as a float")
{
    const Migrated migrated(USERS);

    REQUIRE(migrated.latest.table()["Main"]["fScale"].is_floating_point());
    REQUIRE(outcomeOf(migrated.values, "Main.fScale") == Outcome::Carried);
    REQUIRE(migrated.latest.text().contains("fScale = 2.0\n"));
}

TEST_CASE("ConfigMigration: a value the user left as shipped, and a key that is new, keep the new config's value")
{
    const Migrated migrated(USERS);

    REQUIRE(outcomeOf(migrated.values, "Main.iMode") == Outcome::Unchanged);

    // the user's config does not have the color, so there is nothing to report for it
    REQUIRE(migrated.latest.table()["Main"]["sColor"].value<std::string>() == "255,255,255");
    REQUIRE_FALSE(outcomeOf(migrated.values, "Main.sColor"));
}

TEST_CASE("ConfigMigration: a key and a section that the new config no longer has are dropped")
{
    const Migrated migrated(USERS);

    REQUIRE_FALSE(migrated.latest.table()["Main"]["fSpeed"]);
    REQUIRE(outcomeOf(migrated.values, "Main.fSpeed") == Outcome::Removed);

    REQUIRE_FALSE(migrated.latest.table()["Old"]);
    REQUIRE(outcomeOf(migrated.values, "Old.bLegacy") == Outcome::Removed);
}

TEST_CASE("ConfigMigration: a value whose type changed is left to the mod's own migration")
{
    const Migrated migrated(USERS);

    REQUIRE(outcomeOf(migrated.values, "Main.aChannels") == Outcome::NotCarried);
    REQUIRE(migrated.latest.table()["Main"]["aChannels"].is_array());
}

TEST_CASE("ConfigMigration: every value of the user's config is reported, with the value it had")
{
    const Migrated migrated(USERS);

    REQUIRE(migrated.values.size() == 6);
    const auto users = toml::parse(USERS);
    for (const auto& entry : migrated.values) {
        REQUIRE(entry.value == TomlEditor::Value(*users.at_path(entry.pathText()).node()).text());
    }
}

TEST_CASE("ConfigMigration: the new config keeps its comments and the order of its keys")
{
    const Migrated migrated(USERS);

    // the text it had, but for the two values that were carried
    auto expected = SHIPPED;
    replace(expected, "fScale = 1.0\n", "fScale = 2.0\n");
    replace(expected, "iVersion = 7\n", "iVersion = 6\n");
    REQUIRE(migrated.latest.text() == expected);
}

TEST_CASE("ConfigMigration: the version is carried as any value, for the caller to set the new one after")
{
    const Migrated migrated(USERS);

    REQUIRE(migrated.latest.table()["Debug"]["iVersion"].value<int>() == 6);
    REQUIRE(outcomeOf(migrated.values, "Debug.iVersion") == Outcome::Carried);
}

TEST_CASE("ConfigMigration: a config with the values of the new one carries nothing")
{
    const Migrated migrated(SHIPPED);

    REQUIRE(migrated.latest.text() == SHIPPED);
    for (const auto& entry : migrated.values) {
        REQUIRE(entry.outcome == Outcome::Unchanged);
    }
}

TEST_CASE("ConfigMigration: values in a table of dotted keys and in an inline table are carried by their path")
{
    TomlEditor latest("[Main]\nh.i = 1\ng = { x = 1 }\n");
    const auto values = migrateValues(toml::parse("[Main]\nh.i = 5\ng = { x = 2 }\n"), latest);

    REQUIRE(outcomeOf(values, "Main.h.i") == Outcome::Carried);
    REQUIRE(outcomeOf(values, "Main.g") == Outcome::Carried);
    REQUIRE(latest.text() == "[Main]\nh.i = 5\ng = { x = 2 }\n");
}
