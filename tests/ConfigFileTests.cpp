#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "common/TomlEditor.h"
#include "config/ConfigFile.h"

using f4cf::common::TomlEditor;
using f4cf::config::ConfigFile;
using LoadResult = f4cf::config::ConfigFile::LoadResult;

namespace
{
    constexpr auto MAIN = "Main";

    // a file as a mod ships it: a comment and two values
    constexpr auto SHIPPED = "# the mod's settings\r\n[Main]\r\nfScale = 1.0\r\niMode = 0\r\n";

    /**
     * A file path of its own for one test, removed when the test ends.
     */
    struct TempFile
    {
        std::string path;

        TempFile()
        {
            static std::atomic counter = 0;
            const auto name = "f4cf_config_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(counter++) + ".toml";
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
     * The file's text with one value set, as the mod and someone else both edit it.
     */
    std::string withValue(const TempFile& file, const char* key, const TomlEditor::Value& value)
    {
        TomlEditor editor(file.read());
        REQUIRE(editor.set({ MAIN, key }, value));
        return editor.text();
    }

    /**
     * The mod saves one value: it reads the file, sets the value and writes the file, as
     * ConfigBase::saveConfigValues does.
     */
    void saveByMod(ConfigFile& configFile, const TempFile& file, const char* key, const TomlEditor::Value& value)
    {
        REQUIRE(configFile.save(file.path, withValue(file, key, value)));
    }

    /**
     * Someone else sets one value in the file, past the mod: a text editor, or Mods Config VR on Save.
     */
    void writeBySomeoneElse(const TempFile& file, const char* key, const TomlEditor::Value& value)
    {
        file.write(withValue(file, key, value));
    }

    /**
     * One event of the file watcher: the file is loaded only if it holds something the mod does not have.
     */
    LoadResult onFileEvent(ConfigFile& configFile, const TempFile& file, toml::table& table)
    {
        std::string error;
        return configFile.load(file.path, table, error, ConfigFile::Source::ChangedFile);
    }

    /**
     * The load of a session override that was set: the file as the mod has it, with no read of the file.
     */
    LoadResult onOverrideSet(ConfigFile& configFile, const TempFile& file, toml::table& table)
    {
        std::string error;
        return configFile.load(file.path, table, error, ConfigFile::Source::Kept);
    }

    /**
     * A load of the file as load() does it.
     */
    LoadResult loadFile(ConfigFile& configFile, const TempFile& file, toml::table& table)
    {
        std::string error;
        return configFile.load(file.path, table, error);
    }

    /**
     * A value of [Main] as TOML text, or "none" when the table does not have it.
     */
    std::string value(const toml::table& table, const char* key)
    {
        const auto* node = table[MAIN][key].node();
        return node ? TomlEditor::Value(*node).text() : "none";
    }
}

TEST_CASE("ConfigFile: the load of a set override does not read the file")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    SECTION("a write by someone else is left for the file watcher to load")
    {
        writeBySomeoneElse(file, "fScale", 2.0f);

        toml::table overrideLoad;
        REQUIRE(onOverrideSet(configFile, file, overrideLoad) == LoadResult::Loaded);
        REQUIRE(value(overrideLoad, "fScale") == "1.0");

        toml::table eventLoad;
        REQUIRE(onFileEvent(configFile, file, eventLoad) == LoadResult::Loaded);
        REQUIRE(value(eventLoad, "fScale") == "2.0");

        toml::table nextOverrideLoad;
        REQUIRE(onOverrideSet(configFile, file, nextOverrideLoad) == LoadResult::Loaded);
        REQUIRE(value(nextOverrideLoad, "fScale") == "2.0");
    }
    SECTION("the file can be gone")
    {
        std::filesystem::remove(file.path);

        toml::table overrideLoad;
        REQUIRE(onOverrideSet(configFile, file, overrideLoad) == LoadResult::Loaded);
        REQUIRE(value(overrideLoad, "fScale") == "1.0");
        REQUIRE(value(overrideLoad, "iMode") == "0");
    }
    SECTION("the mod's own save is in it")
    {
        saveByMod(configFile, file, "iMode", 3);

        toml::table overrideLoad;
        REQUIRE(onOverrideSet(configFile, file, overrideLoad) == LoadResult::Loaded);
        REQUIRE(value(overrideLoad, "iMode") == "3");
        REQUIRE(value(overrideLoad, "fScale") == "1.0");
    }
}

TEST_CASE("ConfigFile: the load of a set override reads the file when nothing is kept")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table table;

    SECTION("before the first load")
    {
        REQUIRE(onOverrideSet(configFile, file, table) == LoadResult::Loaded);
        REQUIRE(value(table, "fScale") == "1.0");
    }
    SECTION("after the mod saved over a write by someone else")
    {
        REQUIRE(loadFile(configFile, file, table) == LoadResult::Loaded);
        writeBySomeoneElse(file, "fScale", 2.0f);
        saveByMod(configFile, file, "iMode", 1);

        toml::table overrideLoad;
        REQUIRE(onOverrideSet(configFile, file, overrideLoad) == LoadResult::Loaded);
        REQUIRE(value(overrideLoad, "fScale") == "2.0");
        REQUIRE(value(overrideLoad, "iMode") == "1");
    }

    // what it read is kept, so the file watcher has nothing more to load
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a load reads the file, every time it is asked to")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;

    toml::table first;
    REQUIRE(loadFile(configFile, file, first) == LoadResult::Loaded);
    REQUIRE(value(first, "fScale") == "1.0");

    // a load that is not the file watcher's reads a file that did not change too, as an override's reload needs
    toml::table second;
    REQUIRE(loadFile(configFile, file, second) == LoadResult::Loaded);
    REQUIRE(value(second, "fScale") == "1.0");
}

TEST_CASE("ConfigFile: a missing file fails to load")
{
    const TempFile file;
    ConfigFile configFile;
    toml::table table;
    std::string error;

    REQUIRE(configFile.load(file.path, table, error) == LoadResult::Failed);
    REQUIRE(configFile.load(file.path, table, error, ConfigFile::Source::ChangedFile) == LoadResult::Failed);
    REQUIRE(configFile.load(file.path, table, error, ConfigFile::Source::Kept) == LoadResult::Failed);
}

TEST_CASE("ConfigFile: a file that is not TOML is not loaded, says why, and is reported once")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    // a binding written without its quotes
    file.write("# the mod's settings\r\n[Main]\r\nfScale = 1.0\r\nsButton = left tap grip\r\n");

    toml::table table;
    std::string error;
    REQUIRE(configFile.load(file.path, table, error, ConfigFile::Source::ChangedFile) == LoadResult::Invalid);
    INFO(error);
    REQUIRE(error.find("line 4") != std::string::npos);
    REQUIRE(error.ends_with(": sButton = left tap grip"));
    REQUIRE(error.find(std::filesystem::path(file.path).filename().string()) != std::string::npos);

    // the other events of the same content, and the kept content is still the one loaded before
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
    REQUIRE(onOverrideSet(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "iMode") == "0");

    SECTION("a fix of it is loaded")
    {
        file.write("# the mod's settings\r\n[Main]\r\nfScale = 1.0\r\nsButton = \"left tap grip\"\r\n");

        toml::table fixed;
        REQUIRE(onFileEvent(configFile, file, fixed) == LoadResult::Loaded);
        REQUIRE(value(fixed, "sButton") == "\"left tap grip\"");
    }
    SECTION("a load that is not the file watcher's says why again")
    {
        std::string again;
        REQUIRE(configFile.load(file.path, table, again) == LoadResult::Invalid);
        REQUIRE(again == error);
    }
    SECTION("a read of it fails, with nothing kept")
    {
        const ConfigFile fresh;
        REQUIRE_FALSE(fresh.read(file.path, table));
    }
}

TEST_CASE("ConfigFile: a write by someone else is loaded once, however many events it fires")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    writeBySomeoneElse(file, "fScale", 2.0f);

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "fScale") == "2.0");
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: the mod's own save is not loaded back")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    saveByMod(configFile, file, "iMode", 3);

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);

    // the file has the value, and the rest of it
    REQUIRE(file.read() == "# the mod's settings\r\n[Main]\r\nfScale = 1.0\r\niMode = 3\r\n");
}

TEST_CASE("ConfigFile: a write by someone else is loaded after the mod's saves, with and without their events")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);
    toml::table table;

    SECTION("two saves with one event between them")
    {
        saveByMod(configFile, file, "iMode", 1);
        REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
        saveByMod(configFile, file, "iMode", 2);
    }
    SECTION("two saves whose events never come")
    {
        saveByMod(configFile, file, "iMode", 1);
        saveByMod(configFile, file, "iMode", 2);
    }
    SECTION("a save of the value the file already has")
    {
        saveByMod(configFile, file, "iMode", 2);
        saveByMod(configFile, file, "iMode", 2);
        REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
    }

    writeBySomeoneElse(file, "fScale", 2.0f);

    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "fScale") == "2.0");
    REQUIRE(value(table, "iMode") == "2");
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a write by someone else right after the mod's save is loaded by the save's event")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    // both writes are done before the first event is handled, which waits for the file to be left alone
    saveByMod(configFile, file, "iMode", 1);
    writeBySomeoneElse(file, "fScale", 2.0f);

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "fScale") == "2.0");
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a write by someone else that the mod saved over before loading it is still loaded")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    // the mod's save keeps their value in the file, and the mod does not have it yet
    writeBySomeoneElse(file, "fScale", 2.0f);
    saveByMod(configFile, file, "iMode", 1);

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "fScale") == "2.0");
    REQUIRE(value(table, "iMode") == "1");
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);

    // from here the mod's saves are its own again
    saveByMod(configFile, file, "iMode", 2);
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a file put back as the mod loaded it, after the mod saved over a write by someone else, is loaded")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    // three writes before the first event is handled, the last one the very content the mod loaded
    writeBySomeoneElse(file, "fScale", 2.0f);
    saveByMod(configFile, file, "iMode", 1);
    file.write(SHIPPED);

    // the mod has the value it saved and the file does not, so the file is to be loaded
    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "iMode") == "0");
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: the mod's saves over a write by someone else are not its own until the file is loaded")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    writeBySomeoneElse(file, "fScale", 2.0f);
    saveByMod(configFile, file, "iMode", 1);
    saveByMod(configFile, file, "iMode", 2);

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "fScale") == "2.0");
    REQUIRE(value(table, "iMode") == "2");
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a write that a load already read is not loaded again by its event")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    writeBySomeoneElse(file, "fScale", 2.0f);

    toml::table reload;
    REQUIRE(loadFile(configFile, file, reload) == LoadResult::Loaded);
    REQUIRE(value(reload, "fScale") == "2.0");

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a write that leaves the file as it was is not loaded")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    file.write(file.read());

    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
}

TEST_CASE("ConfigFile: a save writes the text it is given, byte for byte")
{
    const TempFile file;
    ConfigFile configFile;
    const std::string text = "\xEF\xBB\xBF# the mod's settings\r\n\r\n[Main]\r\nsName = \"caf\xC3\xA9\"   # inline\r\nfScale = 1.0";

    REQUIRE(configFile.save(file.path, text));
    REQUIRE(file.read() == text);
}

TEST_CASE("ConfigFile: a save to a folder that is not there fails, and the next write by someone else is loaded")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    REQUIRE_FALSE(configFile.save((std::filesystem::temp_directory_path() / "f4cf_no_such_folder" / "config.toml").string(), withValue(file, "iMode", 1)));

    // the save that failed left nothing behind: the file is as loaded, and a change of it is a change
    toml::table table;
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Unchanged);
    writeBySomeoneElse(file, "fScale", 2.0f);
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
    REQUIRE(value(table, "fScale") == "2.0");
}

TEST_CASE("ConfigFile: a read gives the config as the mod has it, with no read of the file")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table startup;
    REQUIRE(loadFile(configFile, file, startup) == LoadResult::Loaded);

    SECTION("a write by someone else is not in it, and is still for the file watcher to load")
    {
        writeBySomeoneElse(file, "fScale", 2.0f);

        toml::table read;
        REQUIRE(configFile.read(file.path, read));
        REQUIRE(value(read, "fScale") == "1.0");

        toml::table eventLoad;
        REQUIRE(onFileEvent(configFile, file, eventLoad) == LoadResult::Loaded);
        REQUIRE(value(eventLoad, "fScale") == "2.0");

        toml::table nextRead;
        REQUIRE(configFile.read(file.path, nextRead));
        REQUIRE(value(nextRead, "fScale") == "2.0");
    }
    SECTION("the file can be gone")
    {
        std::filesystem::remove(file.path);

        toml::table read;
        REQUIRE(configFile.read(file.path, read));
        REQUIRE(value(read, "fScale") == "1.0");
        REQUIRE(value(read, "iMode") == "0");
    }
    SECTION("the mod's own save is in it")
    {
        saveByMod(configFile, file, "iMode", 3);

        toml::table read;
        REQUIRE(configFile.read(file.path, read));
        REQUIRE(value(read, "iMode") == "3");
        REQUIRE(value(read, "fScale") == "1.0");
    }
}

TEST_CASE("ConfigFile: a read with nothing kept reads the file, and keeps nothing")
{
    const TempFile file(SHIPPED);
    ConfigFile configFile;
    toml::table table;

    SECTION("before the first load")
    {
        toml::table read;
        REQUIRE(configFile.read(file.path, read));
        REQUIRE(value(read, "fScale") == "1.0");
    }
    SECTION("after the mod saved over a write by someone else")
    {
        REQUIRE(loadFile(configFile, file, table) == LoadResult::Loaded);
        writeBySomeoneElse(file, "fScale", 2.0f);
        saveByMod(configFile, file, "iMode", 1);

        toml::table read;
        REQUIRE(configFile.read(file.path, read));
        REQUIRE(value(read, "fScale") == "2.0");
        REQUIRE(value(read, "iMode") == "1");
    }

    // the read loaded no values, so the file is still for the file watcher to load
    REQUIRE(onFileEvent(configFile, file, table) == LoadResult::Loaded);
}

TEST_CASE("ConfigFile: a read of a missing file with nothing kept fails")
{
    const TempFile file;
    const ConfigFile configFile;
    toml::table table;

    REQUIRE_FALSE(configFile.read(file.path, table));
}
