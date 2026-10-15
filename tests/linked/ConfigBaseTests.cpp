#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "TestFiles.h"
#include "common/TomlEditor.h"
#include "config/ConfigBase.h"

using f4cf::common::TomlEditor;
using test_files::readFile;
using test_files::TempFolder;
using test_files::writeFile;

namespace
{
    constexpr auto MAIN = "Main";

    // the resource of the default config in this executable, see Resources.rc
    constexpr WORD DEFAULT_CONFIG_RESOURCE_ID = 101;

    // a user's file of the default config's version, with values of the user's own
    constexpr auto USER_FILE = "# the mod's settings\n[Main]\nfScale = 1.0\niMode = 0\n\n[Debug]\niVersion = 2\n";

    // a user's file of version 1, which had fOld and no iMode
    constexpr auto OLD_USER_FILE = "[Main]\nfScale = 4.0\nfOld = 7.0\n\n[Debug]\niVersion = 1\n";

    // the user's INI config of the mod before it moved to TOML, of the INI's own version 18
    constexpr auto USER_INI = "[Main]\nfScale = 3\niMode = 0x5\n\n[Debug]\niVersion = 18\n";

    // a user's file that is not TOML: a binding written without its quotes
    constexpr auto INVALID_FILE = "[Main]\nfScale = 1.0\nsButton = left tap grip\n\n[Debug]\niVersion = 2\n";

    /**
     * A mod's config with two values of its own.
     */
    class TestConfig : public f4cf::ConfigBase
    {
    public:
        // no DLL of this name is loaded, so the default config is read from this executable
        explicit TestConfig(const std::string& configFilePath)
            : ConfigBase("F4CFTests", configFilePath, DEFAULT_CONFIG_RESOURCE_ID)
        {}

        // a load of the file as load() does it, with no file watch started
        using ConfigBase::loadConfigValues;

        float scale = 0;
        int mode = 0;

        // how many times the values were loaded
        int loads = 0;

    protected:
        virtual void loadConfigInternal(const toml::table& config) override
        {
            scale = config[MAIN]["fScale"].value_or(0.0f);
            mode = config[MAIN]["iMode"].value_or(0);
            loads++;
        }
    };

    std::string configPath(const TempFolder& folder)
    {
        return folder.file("TestMod.toml");
    }

    /**
     * A value of a file on disk as TOML text, read past the config, or "<none>" when it does not have it.
     */
    std::string fileValue(const std::string& path, const char* section, const char* key)
    {
        const auto table = toml::parse(readFile(path));
        const auto* node = table[section][key].node();
        return node ? TomlEditor::Value(*node).text() : "<none>";
    }

    /**
     * Someone else sets one value in the file, past the config: a text editor, or Mods Config VR on Save.
     */
    void writeBySomeoneElse(const std::string& path, const char* key, const int value)
    {
        TomlEditor editor(readFile(path));
        REQUIRE(editor.set({ MAIN, key }, value));
        writeFile(path, editor.text());
    }
}

TEST_CASE("ConfigBase: the default config embedded in the module is loaded with no file")
{
    const TempFolder folder;
    TestConfig config(configPath(folder));

    config.loadEmbeddedDefaultOnly();

    REQUIRE(config.scale == 1.5f);
    REQUIRE(config.mode == 2);
    REQUIRE_FALSE(std::filesystem::exists(configPath(folder)));
}

TEST_CASE("ConfigBase: a session override is in the loaded values and not in the file")
{
    const TempFolder folder;
    writeFile(configPath(folder), USER_FILE);
    TestConfig config(configPath(folder));
    REQUIRE(config.loadConfigValues());
    REQUIRE(config.scale == 1.0f);

    config.setConfigOverride("tests", MAIN, "fScale", 2.5f);

    REQUIRE(config.scale == 2.5f);
    REQUIRE(config.mode == 0);
    REQUIRE(config.hasConfigOverride(MAIN, "fScale"));
    REQUIRE(readFile(configPath(folder)) == USER_FILE);

    SECTION("until its owner clears it")
    {
        REQUIRE_FALSE(config.clearConfigOverride("someone else", MAIN, "fScale"));
        REQUIRE(config.scale == 2.5f);

        REQUIRE(config.clearConfigOverride("tests", MAIN, "fScale"));
        REQUIRE(config.scale == 1.0f);
        REQUIRE_FALSE(config.hasConfigOverride(MAIN, "fScale"));
    }
    SECTION("over a value that is saved to the file after it")
    {
        config.saveConfigValue(MAIN, "fScale", 3.0f);
        config.reload();

        REQUIRE(config.scale == 2.5f);
        REQUIRE(fileValue(configPath(folder), MAIN, "fScale") == "3.0");
    }
    SECTION("given as text, in the type of the file's value")
    {
        config.setConfigOverride("tests", MAIN, "iMode", "7");

        REQUIRE(config.mode == 7);
        REQUIRE(config.getConfigValue(MAIN, "iMode") == "7");
    }
}

TEST_CASE("ConfigBase: a saved value is written to the file with the rest of it, and loads no values")
{
    const TempFolder folder;
    writeFile(configPath(folder), USER_FILE);
    TestConfig config(configPath(folder));
    REQUIRE(config.loadConfigValues());
    const auto loads = config.loads;

    config.saveConfigValue(MAIN, "iMode", 3);

    std::string expected = USER_FILE;
    expected.replace(expected.find("iMode = 0"), 9, "iMode = 3");
    REQUIRE(readFile(configPath(folder)) == expected);
    REQUIRE(config.loads == loads);

    // the config has the file as it saved it, with no read of the file
    std::filesystem::remove(configPath(folder));
    REQUIRE(config.getConfigValue(MAIN, "iMode") == "3");
}

TEST_CASE("ConfigBase: a saved value given as text is written in the type of the file's value")
{
    const TempFolder folder;
    writeFile(configPath(folder), USER_FILE);
    TestConfig config(configPath(folder));
    REQUIRE(config.loadConfigValues());

    // as the DebugAdjuster saves a float, in its fixed text
    config.saveConfigValue(MAIN, "fScale", "1.250");
    REQUIRE(fileValue(configPath(folder), MAIN, "fScale") == "1.25");

    // text that is not of the type is not written, and the rest of the save is
    config.saveConfigValues(MAIN, { { "fScale", "big" }, { "iMode", 4 } });
    REQUIRE(fileValue(configPath(folder), MAIN, "fScale") == "1.25");
    REQUIRE(fileValue(configPath(folder), MAIN, "iMode") == "4");
}

TEST_CASE("ConfigBase: values of several sections are saved together, from a list made at runtime or written in place")
{
    const TempFolder folder;
    writeFile(configPath(folder), USER_FILE);
    TestConfig config(configPath(folder));
    REQUIRE(config.loadConfigValues());

    std::vector<f4cf::config::ConfigEntry> entries;
    for (const auto& [key, value] : { std::pair{ "iMode", 6 } }) {
        entries.push_back({ MAIN, key, value });
    }
    entries.push_back({ "Debug", "fFlowFlag1", 0.5f });
    config.saveConfigValues(entries);

    REQUIRE(fileValue(configPath(folder), MAIN, "iMode") == "6");
    REQUIRE(fileValue(configPath(folder), "Debug", "fFlowFlag1") == "0.5");

    config.saveConfigValues({ { MAIN, "fScale", 2.0f }, { "Debug", "fFlowFlag1", 1.5f } });

    REQUIRE(fileValue(configPath(folder), MAIN, "fScale") == "2.0");
    REQUIRE(fileValue(configPath(folder), "Debug", "fFlowFlag1") == "1.5");
    REQUIRE(readFile(configPath(folder)).starts_with("# the mod's settings\n[Main]\nfScale = 2.0\niMode = 6\n"));
}

TEST_CASE("ConfigBase: a save of the values the file already has does not write it")
{
    const TempFolder folder;
    writeFile(configPath(folder), USER_FILE);
    TestConfig config(configPath(folder));
    REQUIRE(config.loadConfigValues());
    const auto written = std::filesystem::last_write_time(configPath(folder));

    config.saveConfigValues(MAIN, { { "fScale", 1.0f }, { "iMode", 0 } });

    REQUIRE(std::filesystem::last_write_time(configPath(folder)) == written);
    REQUIRE(readFile(configPath(folder)) == USER_FILE);
}

TEST_CASE("ConfigBase: a file that is not TOML is left for its user to fix")
{
    const TempFolder folder;
    const auto path = configPath(folder);

    SECTION("at the first load the values are the embedded default's")
    {
        writeFile(path, INVALID_FILE);
        TestConfig config(path);

        REQUIRE_FALSE(config.loadConfigValues());

        REQUIRE(config.scale == 1.5f);
        REQUIRE(config.mode == 2);
        REQUIRE(readFile(path) == INVALID_FILE);
    }
    SECTION("after a load the values loaded before stay")
    {
        writeFile(path, USER_FILE);
        TestConfig config(path);
        REQUIRE(config.loadConfigValues());
        config.setConfigOverride("tests", MAIN, "iMode", 5);
        const auto loads = config.loads;

        writeFile(path, INVALID_FILE);
        REQUIRE_FALSE(config.loadConfigValues());

        REQUIRE(config.loads == loads);
        REQUIRE(config.scale == 1.0f);
        REQUIRE(config.mode == 5);
    }
    SECTION("a save does not write it")
    {
        writeFile(path, USER_FILE);
        TestConfig config(path);
        REQUIRE(config.loadConfigValues());

        writeFile(path, INVALID_FILE);
        config.saveConfigValue(MAIN, "iMode", 3);

        REQUIRE(readFile(path) == INVALID_FILE);
    }
}

TEST_CASE("ConfigBase: the subscribers are called once for all the loads since the last call")
{
    const TempFolder folder;
    writeFile(configPath(folder), USER_FILE);
    TestConfig config(configPath(folder));
    int calls = 0;
    config.subscribeForConfigChangedEvent("tests", [&](const std::string&) {
        calls++;
    });

    // a load of the file, of an override and of a reload: each only marks that it ran
    REQUIRE(config.loadConfigValues());
    config.setConfigOverride("tests", MAIN, "iMode", 1);
    config.reload();
    REQUIRE(calls == 0);

    config.notifySubscribersOfReload();
    REQUIRE(calls == 1);

    config.notifySubscribersOfReload();
    REQUIRE(calls == 1);
}

TEST_CASE("ConfigBase: load brings the file up to date, and a change of it on disk is loaded after that")
{
    using namespace std::chrono_literals;

    const TempFolder folder;
    const auto path = configPath(folder);

    // Never destroyed: load() starts the file watch on a thread of its own that keeps the config, as a mod's
    // config lives for as long as the game does.
    auto& config = *new TestConfig(path);
    int calls = 0;
    config.subscribeForConfigChangedEvent("tests", [&](const std::string&) {
        calls++;
    });

    SECTION("a first run creates the file from the embedded default")
    {
        config.load();

        REQUIRE(config.scale == 1.5f);
        REQUIRE(config.mode == 2);
        REQUIRE(fileValue(path, MAIN, "fScale") == "1.5");
    }
    SECTION("a file of an older version gets the default's layout with the user's values, and is kept as a backup")
    {
        writeFile(path, OLD_USER_FILE);

        config.load();

        REQUIRE(config.scale == 4.0f);
        REQUIRE(config.mode == 2);
        REQUIRE(fileValue(path, MAIN, "fScale") == "4.0");
        REQUIRE(fileValue(path, MAIN, "iMode") == "2");
        REQUIRE(fileValue(path, MAIN, "fOld") == "<none>");
        REQUIRE(fileValue(path, "Debug", "iVersion") == "2");
        REQUIRE(readFile(path).starts_with("# The default config"));
        REQUIRE(readFile(folder.file("TestMod_backup_v1.toml")) == OLD_USER_FILE);
    }
    SECTION("the mod's INI config and no file: the INI is version 0 of the file, which is updated as any older version")
    {
        writeFile(folder.file("TestMod.ini"), USER_INI);

        config.load();

        REQUIRE(config.mode == 5);
        REQUIRE(fileValue(path, MAIN, "fScale") == "3.0");
        REQUIRE(fileValue(path, "Debug", "iVersion") == "2");
        REQUIRE(readFile(path).starts_with("# The default config"));

        // the INI with its own version, and the INI as TOML, as the update keeps the file it updates
        REQUIRE_FALSE(std::filesystem::exists(folder.file("TestMod.ini")));
        REQUIRE(readFile(folder.file("TestMod_backup_v18.ini")) == USER_INI);
        const auto imported = folder.file("TestMod_backup_v0.toml");
        REQUIRE(readFile(imported).starts_with("# TestMod.ini as TOML"));
        REQUIRE(fileValue(imported, MAIN, "iMode") == "5");
        REQUIRE(fileValue(imported, "Debug", "iVersion") == "0");
    }
    SECTION("the mod's INI config beside the file is left as it is")
    {
        writeFile(path, USER_FILE);
        writeFile(folder.file("TestMod.ini"), USER_INI);

        config.load();

        REQUIRE(config.mode == 0);
        REQUIRE(readFile(path) == USER_FILE);
        REQUIRE(readFile(folder.file("TestMod.ini")) == USER_INI);
    }

    // the loads so far are load()'s own
    config.notifySubscribersOfReload();
    REQUIRE(calls == 1);

    // the watch may start after a change, which it then misses: change the file until a change is loaded
    constexpr auto FIRST_MODE = 100;
    for (auto mode = FIRST_MODE; mode < FIRST_MODE + 10 && calls == 1; mode++) {
        std::this_thread::sleep_for(100ms);
        writeBySomeoneElse(path, "iMode", mode);

        // as a mod's frames do
        for (const auto end = std::chrono::steady_clock::now() + 1s; calls == 1 && std::chrono::steady_clock::now() < end;) {
            std::this_thread::sleep_for(20ms);
            config.notifySubscribersOfReload();
        }
    }

    REQUIRE(calls == 2);
    REQUIRE(config.mode >= FIRST_MODE);

    config.unsubscribeFromConfigChangedEvent("tests");
}
