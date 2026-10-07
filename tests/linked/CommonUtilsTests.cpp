#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "TestFiles.h"
#include "common/CommonUtils.h"

using namespace f4cf;
using test_files::TempFolder;
using test_files::writeFile;

namespace
{
    using Strings = std::vector<std::string>;

    // the resource of the default INI in this executable, and one that is not there, see Resources.rc
    constexpr WORD INI_RESOURCE_ID = 101;
    constexpr WORD NO_RESOURCE_ID = 103;
}

TEST_CASE("CommonUtils: two floats are equal within an epsilon")
{
    REQUIRE(common::fEqual(1.0f, 1.000001f));
    REQUIRE_FALSE(common::fEqual(1.0f, 1.001f));
    REQUIRE(common::fEqual(1.0f, 1.001f, 0.01f));

    REQUIRE(common::fNotEqual(1.0f, 1.001f));
    REQUIRE_FALSE(common::fNotEqual(1.0f, 1.000001f));
}

TEST_CASE("CommonUtils: a string in lower case, and without the spaces around it")
{
    REQUIRE(common::str_tolower("Hello World 42") == "hello world 42");

    REQUIRE(common::ltrim(" \t text  ") == "text  ");
    REQUIRE(common::rtrim("  text \r\n") == "  text");
    REQUIRE(common::trim("  two words \t") == "two words");
    REQUIRE(common::trim("   ").empty());
    REQUIRE(common::trim("").empty());

    REQUIRE(common::hasNonWhitespaceText(" \t x "));
    REQUIRE_FALSE(common::hasNonWhitespaceText(" \t\r\n"));
    REQUIRE_FALSE(common::hasNonWhitespaceText(""));
}

TEST_CASE("CommonUtils: a list is split into its values, without the spaces around them and without the empty ones")
{
    REQUIRE(common::splitTrimmed("a, b b ,,c , ", ',') == Strings{ "a", "b b", "c" });
    REQUIRE(common::splitTrimmed("one", ',') == Strings{ "one" });
    REQUIRE(common::splitTrimmed("a|b", '|') == Strings{ "a", "b" });
    REQUIRE(common::splitTrimmed("", ',').empty());
    REQUIRE(common::splitTrimmed(" , ,", ',').empty());
}

TEST_CASE("CommonUtils: a config word is compared in lower case and without its separators")
{
    REQUIRE(common::normalizeConfigToken("Cyan-Subtle") == "cyansubtle");
    REQUIRE(common::normalizeConfigToken(" double_CLICK\t") == "doubleclick");
    REQUIRE(common::normalizeConfigToken("when inside") == "wheninside");
    REQUIRE(common::normalizeConfigToken("").empty());
}

TEST_CASE("CommonUtils: a name is made safe for a Windows path")
{
    // letters, digits, and these four are kept
    REQUIRE(common::sanitizePathWindows("My File-1_2.txt") == "My File-1_2.txt");

    // what a path cannot have is dropped, a control character too
    REQUIRE(common::sanitizePathWindows("a<b>c:d\"e/f\\g|h?i*j\tk") == "abcdefghijk");

    // anything else is written as the two hex digits of each byte, a character past ASCII by its UTF-8 bytes
    REQUIRE(common::sanitizePathWindows("a+b") == "a2bb");
    REQUIRE(common::sanitizePathWindows("caf\xC3\xA9") == "cafc3a9");

    // a name cannot end with a dot or a space
    REQUIRE(common::sanitizePathWindows("name. .") == "name");
    REQUIRE(common::sanitizePathWindows("...").empty());
}

TEST_CASE("CommonUtils: a number as text with a fixed count of decimals")
{
    REQUIRE(common::toStringWithPrecision(3.14159) == "3.14");
    REQUIRE(common::toStringWithPrecision(-0.456, 1) == "-0.5");
    REQUIRE(common::toStringWithPrecision(2, 3) == "2.000");
    REQUIRE(common::toStringWithPrecision(41.7, 0) == "42");
}

TEST_CASE("CommonUtils: the folders of a path are created, without its file name")
{
    const TempFolder folder;

    common::createDirDeep(folder.file("a/b/config.ini"));
    REQUIRE(std::filesystem::is_directory(folder.file("a/b")));
    REQUIRE_FALSE(std::filesystem::exists(folder.file("a/b/config.ini")));

    common::createDirDeep(folder.file("c/d"));
    REQUIRE(std::filesystem::is_directory(folder.file("c/d")));

    // one that is there is left as it is
    writeFile(folder.file("a/b/kept.txt"), "kept");
    common::createDirDeep(folder.file("a/b/config.ini"));
    REQUIRE(test_files::readFile(folder.file("a/b/kept.txt")) == "kept");
}

TEST_CASE("CommonUtils: a file is moved, and never over another file")
{
    const TempFolder folder;
    writeFile(folder.file("from.txt"), "from");

    SECTION("to a name that is free")
    {
        common::moveFileSafe(folder.file("from.txt"), folder.file("to.txt"));

        REQUIRE_FALSE(std::filesystem::exists(folder.file("from.txt")));
        REQUIRE(test_files::readFile(folder.file("to.txt")) == "from");
    }
    SECTION("both stay when the name is taken")
    {
        writeFile(folder.file("to.txt"), "to");

        common::moveFileSafe(folder.file("from.txt"), folder.file("to.txt"));

        REQUIRE(test_files::readFile(folder.file("from.txt")) == "from");
        REQUIRE(test_files::readFile(folder.file("to.txt")) == "to");
    }
    SECTION("a file that is not there, or a folder to move to that is not there, is no error")
    {
        common::moveFileSafe(folder.file("missing.txt"), folder.file("to.txt"));
        REQUIRE_FALSE(std::filesystem::exists(folder.file("to.txt")));

        common::moveFileSafe(folder.file("from.txt"), folder.file("no-such-folder/to.txt"));
        REQUIRE(test_files::readFile(folder.file("from.txt")) == "from");
    }
}

TEST_CASE("CommonUtils: the files of a folder are moved to another, and its folders are not")
{
    const TempFolder folder;
    std::filesystem::create_directories(folder.file("from/inner"));
    std::filesystem::create_directories(folder.file("to"));
    writeFile(folder.file("from/one.txt"), "one");
    writeFile(folder.file("from/two.txt"), "two");
    writeFile(folder.file("from/inner/three.txt"), "three");
    writeFile(folder.file("to/two.txt"), "taken");

    common::moveAllFilesInFolderSafe(folder.file("from"), folder.file("to"));

    REQUIRE(test_files::readFile(folder.file("to/one.txt")) == "one");
    REQUIRE_FALSE(std::filesystem::exists(folder.file("from/one.txt")));

    // a name that is taken stays on both sides
    REQUIRE(test_files::readFile(folder.file("to/two.txt")) == "taken");
    REQUIRE(test_files::readFile(folder.file("from/two.txt")) == "two");

    REQUIRE(test_files::readFile(folder.file("from/inner/three.txt")) == "three");
    REQUIRE_FALSE(std::filesystem::exists(folder.file("to/inner")));

    // a folder that is not there is no error
    common::moveAllFilesInFolderSafe(folder.file("missing"), folder.file("to"));
}

TEST_CASE("CommonUtils: a list file gives its values in lower case, one to a line")
{
    const TempFolder folder;
    writeFile(folder.file("list.txt"), "Head\n  LeftArm:0  \n\nTorso\n");

    REQUIRE(common::loadListFromFile(folder.file("list.txt")) == Strings{ "head", "leftarm:0", "torso" });
    REQUIRE(common::loadListFromFile(folder.file("missing.txt")).empty());
}

TEST_CASE("CommonUtils: a path in the user's Documents folder")
{
    const auto path = common::getRelativePathInDocuments(R"(\My Games\Test)");

    REQUIRE(path.ends_with(R"(\My Games\Test)"));
    REQUIRE(std::filesystem::path(path).is_absolute());
}

TEST_CASE("CommonUtils: a resource embedded in the module is read as text")
{
    // no DLL of this name is loaded, so the resources are read from this executable
    REQUIRE(common::getEmbededResourceAsString("F4CFTests", INI_RESOURCE_ID).find("[Main]") != std::string::npos);
    REQUIRE_THROWS_AS(common::getEmbededResourceAsString("F4CFTests", NO_RESOURCE_ID), std::runtime_error);

    REQUIRE(common::getEmbeddedResourceAsStringIfExists(INI_RESOURCE_ID).value_or("").find("[Main]") != std::string::npos);
    REQUIRE_FALSE(common::getEmbeddedResourceAsStringIfExists(NO_RESOURCE_ID));
}

TEST_CASE("CommonUtils: a file is created from a resource only when it is not there")
{
    const TempFolder folder;
    const auto file = folder.file("config.ini");

    common::createFileFromResourceIfNotExists(file, "F4CFTests", INI_RESOURCE_ID, true);
    REQUIRE(test_files::readFile(file).find("[Main]") != std::string::npos);

    writeFile(file, "the user's own");
    common::createFileFromResourceIfNotExists(file, "F4CFTests", INI_RESOURCE_ID, true);
    REQUIRE(test_files::readFile(file) == "the user's own");
}

TEST_CASE("CommonUtils: the time now, and whether a time span has passed since a start")
{
    const auto millis = common::nowMillis();
    const auto nanos = common::nowNanosec();

    // both count from 1970, and were read within a second of each other
    REQUIRE(millis > 1'600'000'000'000ull);
    REQUIRE(nanos / 1'000'000 >= millis);
    REQUIRE(nanos / 1'000'000 - millis < 1000);

    REQUIRE(common::isNowTimePassed(millis - 1000, 500));
    REQUIRE_FALSE(common::isNowTimePassed(millis - 1000, 60'000));

    // a start that is still to come has not passed
    REQUIRE_FALSE(common::isNowTimePassed(millis + 60'000, 0));
}

TEST_CASE("CommonUtils: a time as text")
{
    using namespace std::chrono;

    // the middle of a month, so the local time zone does not change the month
    const system_clock::time_point time = sys_days{ 2021y / June / 15 } + 12h;

    REQUIRE(common::toDateTimeString(time, "%Y-%m") == "2021-06");
    REQUIRE(std::regex_match(common::toDateTimeString(time), std::regex(R"(2021-06-1[56] \d\d:\d\d:\d\d)")));
    REQUIRE(common::toDateTimeString(clock_cast<file_clock>(time), "%Y-%m") == "2021-06");

    // the time now as hours, minutes and seconds
    REQUIRE(std::regex_match(common::getCurrentTimeString(), std::regex(R"(\d\d:\d\d:\d\d)")));
}

TEST_CASE("CommonUtils: strings are ordered without their letter case")
{
    std::map<std::string, int, common::CaseInsensitiveComparator> values;
    values["Grip"] = 1;
    values["grip"] = 2;
    values["SCOPE"] = 3;

    REQUIRE(values.size() == 2);
    REQUIRE(values.at("GRIP") == 2);
    REQUIRE(values.at("scope") == 3);
}

TEST_CASE("CommonUtils: a DLL is found loaded by its name")
{
    REQUIRE(common::isDLLModLoaded("kernel32.dll"));
    REQUIRE_FALSE(common::isDLLModLoaded("F4CFNoSuchMod.dll"));
}
