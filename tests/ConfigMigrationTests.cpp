#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "config/ConfigMigration.h"
#include "linked/TestFiles.h"

using f4cf::common::TomlEditor;
using f4cf::config::backupPathOf;
using f4cf::config::importIniFile;
using f4cf::config::iniToToml;
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

    // The TOML config a mod ships when it moves its config from INI to TOML.
    const std::string SHIPPED_TOML =
        "# The mod's settings\n"
        "[Main]\n"
        "# How big the menu is\n"
        "fScale = 1.0\n"
        "iMode = 0\n"
        "bShow = false\n"
        "sIcon = 'icons\\hand.dds'\n"
        "sName = \"menu\"\n"
        "aChannels = [\"hands\"]\n"
        "\n"
        "[Debug]\n"
        "iVersion = 1\n";

    // The user's INI config of the mod before that, which the mod read with SimpleIni's getters: the section and the
    // scale in another case, the scale as an integer, the mode in hex, a word for the bool, the name as shipped, and a
    // key and a section the TOML config does not have.
    const std::string USERS_INI =
        "[main]\n"
        "FSCALE = 2\n"
        "iMode = 0x10\n"
        "bShow = yes\n"
        "sIcon = icons\\arm.dds\n"
        "sName = menu\n"
        "aChannels = [\"hands\", \"ik\"]\n"
        "fSpeed = 3.0\n"
        "\n"
        "[Old]\n"
        "bLegacy = true\n"
        "\n"
        "[Debug]\n"
        "iVersion = 18\n";

    /**
     * An INI as TOML text, typed by `shipped`, the TOML config the mod ships, which has to succeed.
     */
    std::string asToml(const std::string& ini, const std::string& shipped = SHIPPED_TOML)
    {
        const auto toml = iniToToml(ini, toml::parse(shipped));
        REQUIRE(toml);
        return *toml;
    }
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

TEST_CASE("INI as TOML: a value takes the type the TOML config has for its key, read as SimpleIni reads it")
{
    const auto table = toml::parse(asToml(USERS_INI));
    const auto& main = *table["Main"].as_table();

    REQUIRE(main["fScale"].is_floating_point());
    REQUIRE(main["iMode"].value<int>() == 16);
    REQUIRE(main["bShow"].value<bool>() == true);
    REQUIRE(main["sIcon"].value<std::string>() == "icons\\arm.dds");
    REQUIRE(main["aChannels"].as_array()->size() == 2);
    REQUIRE(table["Debug"]["iVersion"].value<int>() == 18);
}

TEST_CASE("INI as TOML: in the INI's order, with the TOML config's spelling of a name, and a key it does not have a string")
{
    REQUIRE(asToml(USERS_INI) ==
            "[Main]\n"
            "fScale = 2.0\n"
            "iMode = 16\n"
            "bShow = true\n"
            "sIcon = \"icons\\\\arm.dds\"\n"
            "sName = \"menu\"\n"
            "aChannels = [\"hands\", \"ik\"]\n"
            "fSpeed = \"3.0\"\n"
            "\n"
            "[Old]\n"
            "bLegacy = \"true\"\n"
            "\n"
            "[Debug]\n"
            "iVersion = 18\n");
}

TEST_CASE("INI as TOML: a text that is not a value of the type is a string, and a migration keeps the default, as the mod had")
{
    const auto table = toml::parse(asToml("[Main]\nfScale = 1.5x\niMode = two\nbShow = maybe\naChannels = hands\n"));

    for (const auto* key : { "fScale", "iMode", "bShow", "aChannels" }) {
        REQUIRE(table["Main"][key].is_string());
    }
    REQUIRE(table["Main"]["fScale"].value<std::string>() == "1.5x");

    TomlEditor latest(SHIPPED_TOML);
    migrateValues(table, latest);
    REQUIRE(latest.text() == SHIPPED_TOML);
}

TEST_CASE("INI as TOML: an empty text that is not a value of the type, or of a key the TOML config does not have, is left out")
{
    const auto table = toml::parse(asToml("[Main]\nfScale =\nsName =\ntExtra =\n"));

    REQUIRE_FALSE(table["Main"]["fScale"]);
    REQUIRE_FALSE(table["Main"]["tExtra"]);
    // an empty string is a string's value
    REQUIRE(table["Main"]["sName"].value<std::string>() == "");
}

TEST_CASE("INI as TOML: a key before the INI's first section is a key of the root")
{
    const auto toml = iniToToml("bTop = on\n\n[Main]\niMode = 2\n", toml::parse("bTop = false\n\n[Main]\niMode = 1\n"));

    REQUIRE(toml == "bTop = true\n\n[Main]\niMode = 2\n");
}

TEST_CASE("INI as TOML: a name that can't be a bare key is in quotes")
{
    REQUIRE(iniToToml("[My Section]\nmy key = 1\n", toml::table{}) == "[\"My Section\"]\n\"my key\" = \"1\"\n");
}

TEST_CASE("INI as TOML: an INI that can't be TOML is an error")
{
    // a key of the root and a section of the same name
    REQUIRE_FALSE(iniToToml("Main = 1\n\n[Main]\nx = 1\n", toml::table{}));
}

TEST_CASE("INI as TOML: a migration sets the INI's values in the TOML config the mod ships, in its layout")
{
    TomlEditor latest(SHIPPED_TOML);
    const auto values = migrateValues(toml::parse(asToml(USERS_INI)), latest);

    // the text it had, but for the values that were carried, the version too, which ConfigBase sets after
    auto expected = SHIPPED_TOML;
    replace(expected, "fScale = 1.0\n", "fScale = 2.0\n");
    replace(expected, "iMode = 0\n", "iMode = 16\n");
    replace(expected, "bShow = false\n", "bShow = true\n");
    replace(expected, "sIcon = 'icons\\hand.dds'\n", "sIcon = 'icons\\arm.dds'\n");
    replace(expected, "aChannels = [\"hands\"]\n", "aChannels = [\"hands\", \"ik\"]\n");
    replace(expected, "iVersion = 1\n", "iVersion = 18\n");
    REQUIRE(latest.text() == expected);
    REQUIRE(outcomeOf(values, "Main.sName") == Outcome::Unchanged);
    REQUIRE(outcomeOf(values, "Main.fSpeed") == Outcome::Removed);
    REQUIRE(outcomeOf(values, "Old.bLegacy") == Outcome::Removed);
}

TEST_CASE("INI as TOML: a transform's text is the table of its numbers, which a migration carries as any value")
{
    constexpr auto SHIPPED_ZONE =
        "[Zone]\n"
        "tZone = { pos = [0.0, 0.0, 0.0], scale = 18.0 }\n"
        "tZonePA = { pos = [1.0, 1.0, 1.0] }\n";
    const auto old = toml::parse(asToml("[Zone]\ntZone = 5,-2,1;0,0.3,0;24.00\n", SHIPPED_ZONE));

    TomlEditor latest(SHIPPED_ZONE);
    const auto values = migrateValues(old, latest);

    REQUIRE(outcomeOf(values, "Zone.tZone") == Outcome::Carried);
    // each number as the text has it, not as the float nearest to it, and the INI's key the user does not have keeps
    // its default
    REQUIRE(latest.text() ==
            "[Zone]\n"
            "tZone = { pos = [5.0, -2.0, 1.0], rot = [0.0, 0.3, 0.0], scale = 24.0 }\n"
            "tZonePA = { pos = [1.0, 1.0, 1.0] }\n");
}

TEST_CASE("INI as TOML: a text that is not a transform, or of a table that is not one, is a string, and a migration keeps the default")
{
    constexpr auto SHIPPED_ZONE =
        "[Zone]\n"
        "tZone = { scale = 18.0 }\n"
        "g = { x = 1 }\n";
    const auto old = toml::parse(asToml("[Zone]\ntZone = 5,-2,1\ng = 1,2,3;4,5,6;7\n", SHIPPED_ZONE));
    REQUIRE(old["Zone"]["tZone"].value<std::string>() == "5,-2,1");
    REQUIRE(old["Zone"]["g"].value<std::string>() == "1,2,3;4,5,6;7");

    TomlEditor latest(SHIPPED_ZONE);
    REQUIRE(outcomeOf(migrateValues(old, latest), "Zone.tZone") == Outcome::NotCarried);
    REQUIRE(latest.text() == SHIPPED_ZONE);
}

TEST_CASE("INI file import: the INI as TOML at version 0, which says what it is, and the INI kept by its own version")
{
    const test_files::TempFolder folder;
    const auto ini = folder.file("Mod.ini");
    const auto file = folder.file("Mod.toml");
    test_files::writeFile(ini, USERS_INI);

    const auto imported = importIniFile(ini, file, toml::parse(SHIPPED_TOML));

    REQUIRE(imported);
    REQUIRE(imported->version == 18);
    REQUIRE(imported->backup == folder.path / "Mod_backup_v18.ini");
    REQUIRE(imported->backupError.empty());
    REQUIRE_FALSE(std::filesystem::exists(ini));
    REQUIRE(test_files::readFile(folder.file("Mod_backup_v18.ini")) == USERS_INI);

    const auto text = test_files::readFile(file);
    REQUIRE(text.starts_with("# Mod.ini as TOML"));
    const auto table = toml::parse(text);
    REQUIRE(table["Debug"]["iVersion"].value<int>() == 0);
    REQUIRE(table["Main"]["iMode"].value<int>() == 16);
}

TEST_CASE("INI file import: an INI with no version is of version 0")
{
    const test_files::TempFolder folder;
    test_files::writeFile(folder.file("Mod.ini"), "[Main]\niMode = 3\n");

    const auto imported = importIniFile(folder.file("Mod.ini"), folder.file("Mod.toml"), toml::parse(SHIPPED_TOML));

    REQUIRE(imported);
    REQUIRE(imported->version == 0);
    REQUIRE(std::filesystem::exists(folder.file("Mod_backup_v0.ini")));
    REQUIRE(toml::parse(test_files::readFile(folder.file("Mod.toml")))["Debug"]["iVersion"].value<int>() == 0);
}

TEST_CASE("INI file import: an INI that can't be read or be TOML, or a file that can't be written, leaves the INI and no file")
{
    const test_files::TempFolder folder;
    const auto ini = folder.file("Mod.ini");
    const auto file = folder.file("Mod.toml");

    SECTION("no INI")
    {
        REQUIRE_FALSE(importIniFile(ini, file, toml::table{}));
    }
    SECTION("an INI that can't be TOML: a key of the root and a section of the same name")
    {
        constexpr auto BROKEN = "Main = 1\n\n[Main]\nx = 1\n";
        test_files::writeFile(ini, BROKEN);

        const auto imported = importIniFile(ini, file, toml::table{});

        REQUIRE_FALSE(imported);
        REQUIRE(imported.error().starts_with("Mod.ini can't be TOML"));
        REQUIRE(test_files::readFile(ini) == BROKEN);
    }
    SECTION("a file in a folder that does not exist")
    {
        test_files::writeFile(ini, USERS_INI);

        REQUIRE_FALSE(importIniFile(ini, folder.file("missing/Mod.toml"), toml::parse(SHIPPED_TOML)));
        REQUIRE(test_files::readFile(ini) == USERS_INI);
    }

    REQUIRE_FALSE(std::filesystem::exists(file));
}

TEST_CASE("INI file import: an INI that can't be moved to its backup is not a failure, and the result says why")
{
    const test_files::TempFolder folder;
    const auto ini = folder.file("Mod.ini");
    test_files::writeFile(ini, USERS_INI);
    // a folder where the backup would go
    std::filesystem::create_directory(folder.file("Mod_backup_v18.ini"));

    const auto imported = importIniFile(ini, folder.file("Mod.toml"), toml::parse(SHIPPED_TOML));

    REQUIRE(imported);
    REQUIRE_FALSE(imported->backupError.empty());
    REQUIRE(test_files::readFile(ini) == USERS_INI);
    REQUIRE(std::filesystem::exists(folder.file("Mod.toml")));
}

TEST_CASE("Config backup: a file of a version is kept beside it, with the version in its name")
{
    REQUIRE(backupPathOf("C:/configs/Mod.toml", 3) == std::filesystem::path("C:/configs/Mod_backup_v3.toml"));
    REQUIRE(backupPathOf("C:/configs/Mod.ini", 18) == std::filesystem::path("C:/configs/Mod_backup_v18.ini"));
}
