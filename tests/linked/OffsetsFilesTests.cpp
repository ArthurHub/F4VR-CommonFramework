#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

#include "TestFiles.h"
#include "TestMath.h"
#include "common/MatrixUtils.h"
#include "config/OffsetsFiles.h"

using namespace f4cf;
using config::OffsetsFiles;
using test_files::TempFolder;
using test_math::requireNear;

namespace
{
    using Offsets = std::unordered_map<std::string, RE::NiTransform>;

    // the resource of OffsetsFilesTests.json in this executable, and one that is not there, see Resources.rc
    constexpr WORD OFFSETS_RESOURCE_ID = 102;
    constexpr WORD NO_RESOURCE_ID = 103;

    const auto GRIP = common::MatrixUtils::getTransform(1, 2.5f, -3, 30, 20, 10, 2);
    const auto SCOPE = common::MatrixUtils::getTransform(-4, 0, 6, -40, 15, 70, 0.5f);
}

TEST_CASE("OffsetsFiles: a saved transform is loaded back as it was, under its name")
{
    const TempFolder folder;
    const auto file = folder.file("grip.json");

    REQUIRE(OffsetsFiles::saveOffsetsToJsonFile("grip", GRIP, file));

    Offsets offsets;
    OffsetsFiles::loadOffsetJsonFile(file, offsets);
    REQUIRE(offsets.size() == 1);
    requireNear(offsets.at("grip"), GRIP, 0.0f);
}

TEST_CASE("OffsetsFiles: a file is loaded into the map over the name it has, and beside the others")
{
    const TempFolder folder;
    const auto file = folder.file("grip.json");
    REQUIRE(OffsetsFiles::saveOffsetsToJsonFile("grip", GRIP, file));
    Offsets offsets{ { "grip", SCOPE }, { "scope", SCOPE } };

    OffsetsFiles::loadOffsetJsonFile(file, offsets);

    REQUIRE(offsets.size() == 2);
    requireNear(offsets.at("grip"), GRIP, 0.0f);
    requireNear(offsets.at("scope"), SCOPE, 0.0f);
}

TEST_CASE("OffsetsFiles: every file of a folder is loaded, and its folders are not")
{
    const TempFolder folder;
    REQUIRE(OffsetsFiles::saveOffsetsToJsonFile("grip", GRIP, folder.file("grip.json")));
    REQUIRE(OffsetsFiles::saveOffsetsToJsonFile("scope", SCOPE, folder.file("scope.json")));
    std::filesystem::create_directories(folder.file("more"));
    REQUIRE(OffsetsFiles::saveOffsetsToJsonFile("stock", GRIP, folder.file("more/stock.json")));

    const auto offsets = OffsetsFiles::loadOffsetsFromFilesystem(folder.path.string());

    REQUIRE(offsets.size() == 2);
    requireNear(offsets.at("grip"), GRIP, 0.0f);
    requireNear(offsets.at("scope"), SCOPE, 0.0f);
}

TEST_CASE("OffsetsFiles: a file that is missing or is not JSON loads nothing")
{
    const TempFolder folder;
    Offsets offsets{ { "scope", SCOPE } };

    OffsetsFiles::loadOffsetJsonFile(folder.file("missing.json"), offsets);

    test_files::writeFile(folder.file("broken.json"), "{ \"grip\": { \"x\": 1,");
    OffsetsFiles::loadOffsetJsonFile(folder.file("broken.json"), offsets);

    REQUIRE(offsets.size() == 1);
    requireNear(offsets.at("scope"), SCOPE, 0.0f);
}

TEST_CASE("OffsetsFiles: a JSON file that is short of a transform's numbers throws, with the file in the message")
{
    const TempFolder folder;
    const auto file = folder.file("short.json");
    test_files::writeFile(file, R"({ "grip": { "rotation": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0], "x": 1, "y": 2, "z": 3 } })");
    Offsets offsets;

    try {
        OffsetsFiles::loadOffsetJsonFile(file, offsets);
        FAIL("the load did not throw");
    } catch (const std::runtime_error& error) {
        REQUIRE(std::string(error.what()).find("short.json") != std::string::npos);
    }
}

TEST_CASE("OffsetsFiles: a save to a folder that is not there fails")
{
    const TempFolder folder;

    REQUIRE_FALSE(OffsetsFiles::saveOffsetsToJsonFile("grip", GRIP, folder.file("no-such-folder/grip.json")));
}

TEST_CASE("OffsetsFiles: the offsets embedded in the module are loaded for the resources that are there")
{
    const auto offsets = OffsetsFiles::loadEmbeddedOffsets(OFFSETS_RESOURCE_ID, NO_RESOURCE_ID);

    REQUIRE(offsets.size() == 1);
    requireNear(offsets.at("embedded"), common::MatrixUtils::getTransform(1, 2, 3, 0, 0, 0, 4));

    REQUIRE(OffsetsFiles::loadEmbeddedOffsets(NO_RESOURCE_ID, NO_RESOURCE_ID).empty());
}
