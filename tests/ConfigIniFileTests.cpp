#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "ConfigIniFile.h"

using f4cf::config::IniFile;
using LoadResult = f4cf::config::IniFile::LoadResult;

namespace
{
    constexpr auto MAIN = "Main";

    // a file as a mod ships it: a comment and two values
    constexpr auto SHIPPED = "# the mod's settings\r\n[Main]\r\nfScale = 1\r\niMode = 0\r\n";

    /**
     * A file path of its own for one test, removed when the test ends.
     */
    struct TempFile
    {
        std::string path;

        TempFile()
        {
            static std::atomic counter = 0;
            const auto name = "f4cf_config_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(counter++) + ".ini";
            path = (std::filesystem::temp_directory_path() / name).string();
            std::filesystem::remove(path);
        }

        explicit TempFile(const std::string& text)
            : TempFile()
        {
            write(text);
        }

        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;

        ~TempFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }

        std::string read() const
        {
            std::ostringstream text;
            text << std::ifstream(path, std::ios::binary).rdbuf();
            return text.str();
        }

        void write(const std::string& text) const
        {
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }
    };

    /**
     * The mod saves one value: it reads the file, sets the value and writes the file, as
     * ConfigBase::saveIniConfigValues does.
     */
    void saveByMod(IniFile& iniFile, const TempFile& file, const char* key, const char* value)
    {
        CSimpleIniA ini;
        REQUIRE(ini.LoadFile(file.path.c_str()) >= 0);
        ini.SetValue(MAIN, key, value);
        REQUIRE(iniFile.save(file.path, ini));
    }

    /**
     * Someone else sets one value in the file, past the mod: a text editor, or Mods Config VR on Save.
     */
    void writeBySomeoneElse(const TempFile& file, const char* key, const char* value)
    {
        CSimpleIniA ini;
        REQUIRE(ini.LoadFile(file.path.c_str()) >= 0);
        ini.SetValue(MAIN, key, value);
        REQUIRE(ini.SaveFile(file.path.c_str()) >= 0);
    }

    /**
     * One event of the file watcher: the file is loaded only if it holds something the mod does not have.
     */
    LoadResult onFileEvent(IniFile& iniFile, const TempFile& file, CSimpleIniA& ini)
    {
        return iniFile.load(file.path, ini, true);
    }

    std::string value(const CSimpleIniA& ini, const char* key)
    {
        return ini.GetValue(MAIN, key, "");
    }
}

TEST_CASE("ConfigIniFile: a load reads the file, every time it is asked to")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;

    CSimpleIniA first;
    REQUIRE(iniFile.load(file.path, first) == LoadResult::Loaded);
    REQUIRE(value(first, "fScale") == "1");

    // a load that is not the file watcher's reads a file that did not change too, as an override's reload needs
    CSimpleIniA second;
    REQUIRE(iniFile.load(file.path, second) == LoadResult::Loaded);
    REQUIRE(value(second, "fScale") == "1");
}

TEST_CASE("ConfigIniFile: a missing file fails to load")
{
    const TempFile file;
    IniFile iniFile;
    CSimpleIniA ini;

    REQUIRE(iniFile.load(file.path, ini) == LoadResult::Failed);
    REQUIRE(iniFile.load(file.path, ini, true) == LoadResult::Failed);
}

TEST_CASE("ConfigIniFile: a write by someone else is loaded once, however many events it fires")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    writeBySomeoneElse(file, "fScale", "2");

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "fScale") == "2");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: the mod's own save is not loaded back")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    saveByMod(iniFile, file, "iMode", "3");

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);

    // the file has the value, and the rest of it
    CSimpleIniA saved;
    REQUIRE(saved.LoadFile(file.path.c_str()) >= 0);
    REQUIRE(value(saved, "iMode") == "3");
    REQUIRE(value(saved, "fScale") == "1");
    REQUIRE(file.read().starts_with("# the mod's settings"));
}

TEST_CASE("ConfigIniFile: a write by someone else is loaded after the mod's saves, with and without their events")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);
    CSimpleIniA ini;

    SECTION("two saves with one event between them")
    {
        saveByMod(iniFile, file, "iMode", "1");
        REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
        saveByMod(iniFile, file, "iMode", "2");
    }
    SECTION("two saves whose events never come")
    {
        saveByMod(iniFile, file, "iMode", "1");
        saveByMod(iniFile, file, "iMode", "2");
    }
    SECTION("a save of the value the file already has")
    {
        saveByMod(iniFile, file, "iMode", "2");
        saveByMod(iniFile, file, "iMode", "2");
        REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
    }

    writeBySomeoneElse(file, "fScale", "2");

    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "fScale") == "2");
    REQUIRE(value(ini, "iMode") == "2");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: a write by someone else right after the mod's save is loaded by the save's event")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    // both writes are done before the first event is handled, which waits for the file to be left alone
    saveByMod(iniFile, file, "iMode", "1");
    writeBySomeoneElse(file, "fScale", "2");

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "fScale") == "2");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: a write by someone else that the mod saved over before loading it is still loaded")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    // the mod's save keeps their value in the file, and the mod does not have it yet
    writeBySomeoneElse(file, "fScale", "2");
    saveByMod(iniFile, file, "iMode", "1");

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "fScale") == "2");
    REQUIRE(value(ini, "iMode") == "1");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);

    // from here the mod's saves are its own again
    saveByMod(iniFile, file, "iMode", "2");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: a file put back as the mod loaded it, after the mod saved over a write by someone else, is loaded")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    // three writes before the first event is handled, the last one the very content the mod loaded
    writeBySomeoneElse(file, "fScale", "2");
    saveByMod(iniFile, file, "iMode", "1");
    file.write(SHIPPED);

    // the mod has the value it saved and the file does not, so the file is to be loaded
    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "iMode") == "0");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: the mod's saves over a write by someone else are not its own until the file is loaded")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    writeBySomeoneElse(file, "fScale", "2");
    saveByMod(iniFile, file, "iMode", "1");
    saveByMod(iniFile, file, "iMode", "2");

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "fScale") == "2");
    REQUIRE(value(ini, "iMode") == "2");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: a write that a load already read is not loaded again by its event")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    writeBySomeoneElse(file, "fScale", "2");

    CSimpleIniA reload;
    REQUIRE(iniFile.load(file.path, reload) == LoadResult::Loaded);
    REQUIRE(value(reload, "fScale") == "2");

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: a write that leaves the file as it was is not loaded")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    file.write(file.read());

    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
}

TEST_CASE("ConfigIniFile: a save writes the bytes SimpleIni's SaveFile writes")
{
    const TempFile file;
    const TempFile expected;

    SECTION("a plain file")
    {
        file.write("# the mod's settings\n\n[Main]\n# how large\nfScale = 1\n\n[Debug]\niVersion = 4\n");
    }
    SECTION("a file with the UTF-8 signature")
    {
        file.write("\xEF\xBB\xBF[Main]\r\nsName = caf\xC3\xA9\r\nfScale = 1\r\n");
    }

    CSimpleIniA reference;
    REQUIRE(reference.LoadFile(file.path.c_str()) >= 0);
    reference.SetValue(MAIN, "fScale", "2.5");
    REQUIRE(reference.SaveFile(expected.path.c_str()) >= 0);

    IniFile iniFile;
    saveByMod(iniFile, file, "fScale", "2.5");

    REQUIRE(file.read() == expected.read());
}

TEST_CASE("ConfigIniFile: a save to a folder that is not there fails, and the next write by someone else is loaded")
{
    const TempFile file(SHIPPED);
    IniFile iniFile;
    CSimpleIniA startup;
    REQUIRE(iniFile.load(file.path, startup) == LoadResult::Loaded);

    CSimpleIniA toSave;
    REQUIRE(toSave.LoadFile(file.path.c_str()) >= 0);
    toSave.SetValue(MAIN, "iMode", "1");
    REQUIRE_FALSE(iniFile.save((std::filesystem::temp_directory_path() / "f4cf_no_such_folder" / "config.ini").string(), toSave));

    // the save that failed left nothing behind: the file is as loaded, and a change of it is a change
    CSimpleIniA ini;
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Unchanged);
    writeBySomeoneElse(file, "fScale", "2");
    REQUIRE(onFileEvent(iniFile, file, ini) == LoadResult::Loaded);
    REQUIRE(value(ini, "fScale") == "2");
}
