#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "vrui/UIDevLayoutFile.h"

using f4cf::vrui::syncDevLayoutFile;
using f4cf::vrui::UIDevLayoutRoot;

namespace
{
    /**
     * A file path of its own for one test, removed when the test ends.
     */
    struct TempFile
    {
        std::string path;

        TempFile()
        {
            const auto name = "f4cf_devlayout_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini";
            path = (std::filesystem::temp_directory_path() / name).string();
            std::filesystem::remove(path);
        }

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
     * A screen as the UI tree writes it: the root, a row under it and two buttons in the row.
     */
    UIDevLayoutRoot screen(const std::string& name)
    {
        return { name,
            {
                { name, "Pos:(0.00,0.00,0.00), Scale:(1.00)" },
                { name + ".Row", "Pos:(0.00,0.00,1.00)" },
                { name + ".Row.Save", "Size:(3.00,2.00)" },
                { name + ".Row.Exit", "Size:(3.00,2.00)" },
            } };
    }
}

TEST_CASE("UIDevLayoutFile: a missing file is written with a section for each root, in tree order")
{
    const TempFile file;

    const auto lines = syncDevLayoutFile(file.path, { screen("Main"), screen("Beam") });

    REQUIRE(lines.has_value());
    CHECK(lines->size() == 8);
    CHECK(lines->at("Main") == "Pos:(0.00,0.00,0.00), Scale:(1.00)");
    CHECK(lines->at("Beam.Row.Exit") == "Size:(3.00,2.00)");

    const auto text = file.read();
    CHECK(text.starts_with("# VRUI dev layout"));
    const auto main =
        text.find("[Main]\r\nMain = Pos:(0.00,0.00,0.00), Scale:(1.00)\r\nRow = Pos:(0.00,0.00,1.00)\r\nRow.Save = Size:(3.00,2.00)\r\nRow.Exit = Size:(3.00,2.00)\r\n");
    const auto beam = text.find("\r\n\r\n[Beam]\r\nBeam = ");
    CHECK(main != std::string::npos);
    CHECK(beam != std::string::npos);
    CHECK(main < beam);
}

TEST_CASE("UIDevLayoutFile: an edited line is kept, and the file is not written when nothing is missing")
{
    const TempFile file;
    REQUIRE(syncDevLayoutFile(file.path, { screen("Main") }).has_value());

    auto text = file.read();
    text.replace(text.find("Row = Pos:(0.00,0.00,1.00)"), 26, "Row = Pos:(5.00,0.00,1.00)");
    file.write(text);

    const auto lines = syncDevLayoutFile(file.path, { screen("Main") });

    REQUIRE(lines.has_value());
    CHECK(lines->at("Main.Row") == "Pos:(5.00,0.00,1.00)");
    CHECK(file.read() == text);
}

TEST_CASE("UIDevLayoutFile: the lines of a root that is not attached stay")
{
    const TempFile file;
    REQUIRE(syncDevLayoutFile(file.path, { screen("Main") }).has_value());

    const auto lines = syncDevLayoutFile(file.path, { screen("Beam") });

    REQUIRE(lines.has_value());
    CHECK(lines->size() == 8);
    CHECK(lines->contains("Main.Row.Save"));
    CHECK(file.read().find("[Main]") < file.read().find("[Beam]"));
}

TEST_CASE("UIDevLayoutFile: a removed line comes back at the end of its section")
{
    const TempFile file;
    REQUIRE(syncDevLayoutFile(file.path, { screen("Main"), screen("Beam") }).has_value());

    auto text = file.read();
    const std::string removed = "Row = Pos:(0.00,0.00,1.00)\r\n";
    text.erase(text.find(removed), removed.size());
    file.write(text);

    const auto lines = syncDevLayoutFile(file.path, { screen("Main"), screen("Beam") });

    REQUIRE(lines.has_value());
    CHECK(lines->at("Main.Row") == "Pos:(0.00,0.00,1.00)");
    CHECK(file.read().find("[Main]\r\nMain = Pos:(0.00,0.00,0.00), Scale:(1.00)\r\nRow.Save = Size:(3.00,2.00)\r\nRow.Exit = Size:(3.00,2.00)\r\nRow = Pos:(0.00,0.00,1.00)\r\n") !=
          std::string::npos);
}

TEST_CASE("UIDevLayoutFile: a removed section comes back whole, at the end of the file")
{
    const TempFile file;
    REQUIRE(syncDevLayoutFile(file.path, { screen("Main"), screen("Beam") }).has_value());

    auto text = file.read();
    text.erase(text.find("[Main]"), text.find("[Beam]") - text.find("[Main]"));
    file.write(text);

    const auto lines = syncDevLayoutFile(file.path, { screen("Main"), screen("Beam") });

    REQUIRE(lines.has_value());
    CHECK(lines->size() == 8);
    const auto synced = file.read();
    CHECK(synced.find("[Beam]") < synced.find("[Main]\r\nMain = Pos:(0.00,0.00,0.00), Scale:(1.00)\r\nRow = Pos:(0.00,0.00,1.00)\r\n"));
}

TEST_CASE("UIDevLayoutFile: a comment written by hand survives a line being added")
{
    const TempFile file;
    REQUIRE(syncDevLayoutFile(file.path, { screen("Main") }).has_value());

    auto text = file.read();
    text.insert(text.find("Row.Save ="), "# the save button\r\n");
    file.write(text);

    auto grown = screen("Main");
    grown.lines.emplace_back("Main.Row.Reset", "Size:(3.00,2.00)");
    const auto lines = syncDevLayoutFile(file.path, { grown });

    REQUIRE(lines.has_value());
    CHECK(lines->at("Main.Row.Reset") == "Size:(3.00,2.00)");
    const auto synced = file.read();
    CHECK(synced.find("# the save button\r\nRow.Save = Size:(3.00,2.00)") != std::string::npos);
    CHECK(synced.find("Row.Reset = Size:(3.00,2.00)") > synced.find("Row.Exit ="));
}

TEST_CASE("UIDevLayoutFile: a line with its value emptied is not written again")
{
    const TempFile file;
    REQUIRE(syncDevLayoutFile(file.path, { screen("Main") }).has_value());

    auto text = file.read();
    text.replace(text.find("Row = Pos:(0.00,0.00,1.00)"), 26, "Row =");
    file.write(text);

    const auto lines = syncDevLayoutFile(file.path, { screen("Main") });

    REQUIRE(lines.has_value());
    CHECK(lines->at("Main.Row").empty());
    CHECK(file.read() == text);
}
