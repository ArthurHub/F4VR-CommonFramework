#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "TestFiles.h"
#include "config/ConfigBase.h"

using test_files::readFile;
using test_files::TempFolder;
using test_files::writeFile;

namespace
{
    constexpr auto MAIN = "Main";

    // the resource of the default INI in this executable, see Resources.rc
    constexpr WORD DEFAULT_INI_RESOURCE_ID = 101;

    // a user's file of the default INI's version, with values of the user's own
    constexpr auto USER_FILE = "# the mod's settings\n[Main]\nfScale = 1\niMode = 0\n\n[Debug]\niVersion = 2\n";

    // a user's file of version 1, which had fOld and no iMode
    constexpr auto OLD_USER_FILE = "[Main]\nfScale = 4\nfOld = 7\n\n[Debug]\niVersion = 1\n";

    /**
     * A mod's config with two values of its own.
     */
    class TestConfig : public f4cf::ConfigBase
    {
    public:
        // no DLL of this name is loaded, so the default INI is read from this executable
        explicit TestConfig(const std::string& iniFilePath)
            : ConfigBase("F4CFTests", iniFilePath, DEFAULT_INI_RESOURCE_ID)
        {}

        // a load of the file as load() does it, with no file watch started
        using ConfigBase::loadIniConfigValues;

        float scale = 0;
        int mode = 0;

        // how many times the values were loaded
        int loads = 0;

    protected:
        virtual void loadIniConfigInternal(const CSimpleIniA& ini) override
        {
            scale = static_cast<float>(ini.GetDoubleValue(MAIN, "fScale", 0));
            mode = static_cast<int>(ini.GetLongValue(MAIN, "iMode", 0));
            loads++;
        }
    };

    std::string iniPath(const TempFolder& folder)
    {
        return folder.file("TestMod.ini");
    }

    /**
     * The values of a file on disk, read past the config.
     */
    struct FileValues
    {
        CSimpleIniA ini;

        explicit FileValues(const std::string& path)
        {
            REQUIRE(ini.LoadFile(path.c_str()) >= 0);
        }

        std::string value(const char* section, const char* key) const
        {
            return ini.GetValue(section, key, "<none>");
        }
    };

    /**
     * Someone else sets one value in the file, past the config: a text editor, or Mods Config VR on Save.
     */
    void writeBySomeoneElse(const std::string& path, const char* key, const int value)
    {
        CSimpleIniA ini;
        REQUIRE(ini.LoadFile(path.c_str()) >= 0);
        ini.SetLongValue(MAIN, key, value);
        REQUIRE(ini.SaveFile(path.c_str()) >= 0);
    }
}

TEST_CASE("ConfigBase: the default INI embedded in the module is loaded with no file")
{
    const TempFolder folder;
    TestConfig config(iniPath(folder));

    config.loadEmbeddedDefaultOnly();

    REQUIRE(config.scale == 1.5f);
    REQUIRE(config.mode == 2);
    REQUIRE_FALSE(std::filesystem::exists(iniPath(folder)));
}

TEST_CASE("ConfigBase: a session override is in the loaded values and not in the file")
{
    const TempFolder folder;
    writeFile(iniPath(folder), USER_FILE);
    TestConfig config(iniPath(folder));
    REQUIRE(config.loadIniConfigValues());
    REQUIRE(config.scale == 1.0f);

    config.setConfigOverride("tests", MAIN, "fScale", 2.5f);

    REQUIRE(config.scale == 2.5f);
    REQUIRE(config.mode == 0);
    REQUIRE(config.hasConfigOverride(MAIN, "fScale"));
    REQUIRE(readFile(iniPath(folder)) == USER_FILE);

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
        config.saveIniConfigValue(MAIN, "fScale", 3.0f);
        config.reload();

        REQUIRE(config.scale == 2.5f);
        REQUIRE(FileValues(iniPath(folder)).ini.GetDoubleValue(MAIN, "fScale", 0) == 3.0);
    }
}

TEST_CASE("ConfigBase: a saved value is written to the file with the rest of it, and loads no values")
{
    const TempFolder folder;
    writeFile(iniPath(folder), USER_FILE);
    TestConfig config(iniPath(folder));
    REQUIRE(config.loadIniConfigValues());
    const auto loads = config.loads;

    config.saveIniConfigValue(MAIN, "iMode", 3);

    const FileValues file(iniPath(folder));
    REQUIRE(file.value(MAIN, "iMode") == "3");
    REQUIRE(file.value(MAIN, "fScale") == "1");
    REQUIRE(readFile(iniPath(folder)).starts_with("# the mod's settings"));
    REQUIRE(config.loads == loads);

    // the config has the INI as it saved it, with no read of the file
    std::filesystem::remove(iniPath(folder));
    REQUIRE(config.getConfigValue(MAIN, "iMode") == "3");
}

TEST_CASE("ConfigBase: the subscribers are called once for all the loads since the last call")
{
    const TempFolder folder;
    writeFile(iniPath(folder), USER_FILE);
    TestConfig config(iniPath(folder));
    int calls = 0;
    config.subscribeForIniChangedEvent("tests", [&](const std::string&) {
        calls++;
    });

    // a load of the file, of an override and of a reload: each only marks that it ran
    REQUIRE(config.loadIniConfigValues());
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
    const auto path = iniPath(folder);

    // Never destroyed: load() starts the file watch on a thread of its own that keeps the config, as a mod's
    // config lives for as long as the game does.
    auto& config = *new TestConfig(path);
    int calls = 0;
    config.subscribeForIniChangedEvent("tests", [&](const std::string&) {
        calls++;
    });

    SECTION("a first run creates the file from the embedded default")
    {
        config.load();

        REQUIRE(config.scale == 1.5f);
        REQUIRE(config.mode == 2);
        REQUIRE(FileValues(path).value(MAIN, "fScale") == "1.5");
    }
    SECTION("a file of an older version gets the default's layout with the user's values, and is kept as a backup")
    {
        writeFile(path, OLD_USER_FILE);

        config.load();

        REQUIRE(config.scale == 4.0f);
        REQUIRE(config.mode == 2);
        const FileValues file(path);
        REQUIRE(file.value(MAIN, "fScale") == "4");
        REQUIRE(file.value(MAIN, "iMode") == "2");
        REQUIRE(file.value(MAIN, "fOld") == "<none>");
        REQUIRE(file.value("Debug", "iVersion") == "2");
        REQUIRE(readFile(folder.file("TestMod_backup_v1.ini")) == OLD_USER_FILE);
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

    config.unsubscribeFromIniChangedEvent("tests");
}
