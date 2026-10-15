#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "common/TomlEditor.h"

using f4cf::common::TomlEditor;

namespace
{
    /**
     * The text with the first `from` in it replaced by `to`.
     */
    std::string replaced(std::string text, const std::string& from, const std::string& to)
    {
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        return text.replace(at, from.size(), to);
    }

    /**
     * The text with one value set, which has to succeed.
     */
    std::string setOne(const std::string& text, const std::initializer_list<std::string_view> path, const TomlEditor::Value& value)
    {
        TomlEditor editor(text);
        INFO(editor.error());
        REQUIRE(editor.ok());
        const bool set = editor.set(path, value);
        INFO(editor.error());
        REQUIRE(set);
        return editor.text();
    }

    // a file as a mod ships it: a header block and banners on their own, blank lines, an inline comment, CRLF
    const std::string SHIPPED =
        "# README, a block of its own\r\n"
        "#\r\n"
        "\r\n"
        "[Main]\r\n"
        "\r\n"
        "### Banner\r\n"
        "#---------\r\n"
        "\r\n"
        "# the scale\r\n"
        "fScale = 71.0   # inline\r\n"
        "\r\n"
        "bFlag = false\r\n"
        "iMode = 0\r\n"
        "sButton = \"offhand tap grip\"\r\n"
        "\r\n"
        "# the last comment of the section\r\n"
        "\r\n"
        "[Debug]\r\n"
        "iVersion = 1\r\n";
}

TEST_CASE("TomlEditor: a set changes the value's text and no other byte")
{
    TomlEditor editor(SHIPPED);
    REQUIRE(editor.ok());
    CHECK(editor.set({ "Main", "fScale" }, 72.55f));
    CHECK(editor.set({ "Main", "bFlag" }, true));
    CHECK(editor.set({ "Main", "iMode" }, 2));
    CHECK(editor.set({ "Main", "sButton" }, "primary tap a"));
    CHECK(editor.set({ "Debug", "iVersion" }, 2));

    auto expected = replaced(SHIPPED, "fScale = 71.0", "fScale = 72.55");
    expected = replaced(expected, "bFlag = false", "bFlag = true");
    expected = replaced(expected, "iMode = 0", "iMode = 2");
    expected = replaced(expected, "\"offhand tap grip\"", "\"primary tap a\"");
    expected = replaced(expected, "iVersion = 1", "iVersion = 2");
    CHECK(editor.text() == expected);
    CHECK(editor.table()["Main"]["iMode"].value<int>() == 2);
    CHECK(editor.error().empty());
}

TEST_CASE("TomlEditor: a value is written as TOML text")
{
    SECTION("a float from its own shortest text, and a whole number stays a float")
    {
        CHECK(TomlEditor::Value(8.7f).text() == "8.7");
        CHECK(TomlEditor::Value(36.0f).text() == "36.0");
        CHECK(TomlEditor::Value(-0.0f).text() == "-0.0");
        CHECK(TomlEditor::Value(1e20f).text() == "1e+20");
        CHECK(TomlEditor::Value(0.1).text() == "0.1");
        CHECK(TomlEditor::Value(std::numeric_limits<float>::infinity()).text() == "inf");
    }

    SECTION("integers and bools")
    {
        CHECK(TomlEditor::Value(5).text() == "5");
        CHECK(TomlEditor::Value(std::int64_t{ -3 }).text() == "-3");
        CHECK(TomlEditor::Value(42u).text() == "42");
        CHECK(TomlEditor::Value(std::uint8_t{ 7 }).text() == "7");
        CHECK(TomlEditor::Value(false).text() == "false");
    }

    SECTION("a string in double quotes, its quotes, backslashes and control characters escaped")
    {
        CHECK(TomlEditor::Value(R"(a "b" C:\x)").text() == R"("a \"b\" C:\\x")");
        CHECK(TomlEditor::Value("tab\tline\n\x01").text() == R"("tab\tline\n\u0001")");
        CHECK(TomlEditor::Value("\b\f\r\x7F").text() == R"("\b\f\r\u007F")");
        CHECK(TomlEditor::Value("ключ").text() == "\"ключ\"");
        CHECK(TomlEditor::Value(std::string("s")).text() == "\"s\"");
        CHECK(TomlEditor::Value(std::string_view("v")).text() == "\"v\"");
    }

    SECTION("a date as toml++ writes it")
    {
        CHECK(TomlEditor::Value(toml::value<toml::date>(toml::date{ 2024, 1, 2 })).text() == "2024-01-02");
    }

    SECTION("an array and a table inline")
    {
        CHECK(TomlEditor::Value(toml::array{ 1, 2.5, "x" }).text() == R"([1, 2.5, "x"])");
        CHECK(TomlEditor::Value(toml::table{ { "b", 2 }, { "a", toml::array{} } }).text() == "{ a = [], b = 2 }");
        CHECK(TomlEditor::Value(toml::table{}).text() == "{}");
    }
}

TEST_CASE("TomlEditor: a value is replaced however it is written")
{
    SECTION("an integer where a float is read becomes a float")
    {
        CHECK(setOne("[M]\nb = 70\n", { "M", "b" }, 71.5f) == "[M]\nb = 71.5\n");
    }

    SECTION("a literal string stays a literal string when the new string can be one")
    {
        CHECK(setOne("[M]\np = 'C:\\old'\n", { "M", "p" }, "D:\\new") == "[M]\np = 'D:\\new'\n");
        CHECK(setOne("[M]\np = 'C:\\old'\n", { "M", "p" }, "it's") == "[M]\np = \"it's\"\n");
    }

    SECTION("a multi-line string and a multi-line array")
    {
        CHECK(setOne("[M]\nd = \"\"\"\nmulti\nline\"\"\" # after\nx = 1\n", { "M", "d" }, "one") == "[M]\nd = \"one\" # after\nx = 1\n");
        CHECK(setOne("[M]\nf = [1, 2,\n     3]\nx = 1\n", { "M", "f" }, toml::array{ 4, 5 }) == "[M]\nf = [4, 5]\nx = 1\n");
    }

    SECTION("a value in an inline table and a dotted key")
    {
        CHECK(setOne("[M]\ng = { x = 1, y = 2 }\n", { "M", "g", "x" }, 5) == "[M]\ng = { x = 5, y = 2 }\n");
        CHECK(setOne("[M]\nh.i = 3\n", { "M", "h", "i" }, 4) == "[M]\nh.i = 4\n");
    }

    SECTION("a whole inline table")
    {
        CHECK(setOne("[M]\ng = { x = 1 }\n", { "M", "g" }, toml::table{ { "y", 2 } }) == "[M]\ng = { y = 2 }\n");
    }

    SECTION("positions after non-ASCII text, a tab and the UTF-8 signature")
    {
        CHECK(setOne("[M]\n\"ключ\" = 1 # й\n", { "M", "ключ" }, 2) == "[M]\n\"ключ\" = 2 # й\n");
        CHECK(setOne("[M]\ng = { \"ключ\" = 1, x = 1 }\n", { "M", "g", "x" }, 2) == "[M]\ng = { \"ключ\" = 1, x = 2 }\n");
        CHECK(setOne("[M]\n\tt = true\n", { "M", "t" }, false) == "[M]\n\tt = false\n");
        CHECK(setOne("\xEF\xBB\xBF"
                     "a = 1\n[M]\n",
                  { "a" },
                  2) ==
              "\xEF\xBB\xBF"
              "a = 2\n[M]\n");
    }

    SECTION("a later set sees the text of an earlier one")
    {
        TomlEditor editor("[M]\nd = \"\"\"\nmulti\nline\"\"\"\nx = 1\n");
        REQUIRE(editor.set({ "M", "d" }, "one"));
        REQUIRE(editor.set({ "M", "x" }, 2));
        CHECK(editor.text() == "[M]\nd = \"one\"\nx = 2\n");
    }

    SECTION("replaces and adds mixed, each at the positions the one before left")
    {
        TomlEditor editor("[M]\na = 1\n\n[N]\nb = 2\n");
        REQUIRE(editor.set({ "M", "c" }, 3));
        REQUIRE(editor.set({ "N", "b" }, 5));
        REQUIRE(editor.set({ "New", "k" }, true));
        REQUIRE(editor.set({ "M", "a" }, 0));
        REQUIRE(editor.set({ "N", "d" }, "x"));
        CHECK(editor.text() == "[M]\na = 0\nc = 3\n\n[N]\nb = 5\nd = \"x\"\n\n[New]\nk = true\n");
        CHECK(editor.table()["N"]["d"].value<std::string>() == "x");
    }

    SECTION("a string becomes a basic string where a literal one cannot hold it")
    {
        CHECK(setOne("[M]\np = 'x'\n", { "M", "p" }, "a\nb") == "[M]\np = \"a\\nb\"\n");
        CHECK(setOne("[M]\np = '''x'''\n", { "M", "p" }, "y") == "[M]\np = \"y\"\n");
    }

    SECTION("a string node keeps a literal string literal")
    {
        CHECK(setOne("[M]\np = 'C:\\a'\n", { "M", "p" }, toml::value<std::string>("D:\\b")) == "[M]\np = 'D:\\b'\n");
    }

    SECTION("a float that is not a number")
    {
        CHECK(setOne("[M]\nf = 1.0\n", { "M", "f" }, std::numeric_limits<double>::quiet_NaN()) == "[M]\nf = nan\n");
    }
}

TEST_CASE("TomlEditor: a key the text does not have is added")
{
    SECTION("after the last value of its section, before its blank lines and comments, in its line endings")
    {
        CHECK(setOne("[M]\n# about a\na = 1\n\n# trailing\n\n[N]\nb = 2\n", { "M", "c" }, 3) == "[M]\n# about a\na = 1\nc = 3\n\n# trailing\n\n[N]\nb = 2\n");
        CHECK(setOne("[M]\r\na = 1\r\n\r\n[N]\r\n", { "M", "c" }, 3) == "[M]\r\na = 1\r\nc = 3\r\n\r\n[N]\r\n");
    }

    SECTION("in its section and not in the section under it")
    {
        CHECK(setOne("[a]\nx = 1\n\n[a.b]\ny = 2\n", { "a", "z" }, 3) == "[a]\nx = 1\nz = 3\n\n[a.b]\ny = 2\n");
    }

    SECTION("after the header of a section with no values")
    {
        CHECK(setOne("[a]\n\n[b]\ny = 1\n", { "a", "x" }, 1) == "[a]\nx = 1\n\n[b]\ny = 1\n");
    }

    SECTION("after the last line when it has no line ending")
    {
        CHECK(setOne("[a]\nx = 1", { "a", "y" }, 2) == "[a]\nx = 1\ny = 2\n");
    }

    SECTION("in a table of dotted keys, as one more dotted key")
    {
        CHECK(setOne("[M]\nh.i = 3\n", { "M", "h", "j" }, 4) == "[M]\nh.i = 3\nh.j = 4\n");
        CHECK(setOne("a.x = 1\n[s]\n", { "a", "y" }, 2) == "a.x = 1\na.y = 2\n[s]\n");
    }

    SECTION("in an inline table, before its closing brace")
    {
        CHECK(setOne("[M]\ng = { x = 1 }\n", { "M", "g", "y" }, 2) == "[M]\ng = { x = 1, y = 2 }\n");
        CHECK(setOne("[M]\ng = {}\n", { "M", "g", "y" }, 2) == "[M]\ng = { y = 2 }\n");
    }

    SECTION("a section it does not have, at the end with a header")
    {
        CHECK(setOne("[a]\nx = 1\n", { "New", "k" }, 1) == "[a]\nx = 1\n\n[New]\nk = 1\n");
        CHECK(setOne("[a]\nx = 1", { "New", "k" }, 1) == "[a]\nx = 1\n\n[New]\nk = 1\n");
        CHECK(setOne("[a]\nx = 1\n\n", { "New", "k" }, 1) == "[a]\nx = 1\n\n[New]\nk = 1\n");
        CHECK(setOne("[a]\nx = 1\n", { "New", "sub", "k" }, 1) == "[a]\nx = 1\n\n[New.sub]\nk = 1\n");
        CHECK(setOne("[a]\nx = 1\n", { "My Section", "k" }, 1) == "[a]\nx = 1\n\n[\"My Section\"]\nk = 1\n");
        CHECK(setOne("", { "S", "k" }, 1) == "[S]\nk = 1\n");
    }

    SECTION("a table only made by the header of a table under it gets a header of its own")
    {
        CHECK(setOne("[a.b]\ny = 1\n", { "a", "k" }, 2) == "[a.b]\ny = 1\n\n[a]\nk = 2\n");
    }

    SECTION("in a section whose header has a dot in a quoted key")
    {
        CHECK(setOne("[\"a.b\"]\nx = 1\n\n[c]\n", { "a.b", "y" }, 2) == "[\"a.b\"]\nx = 1\ny = 2\n\n[c]\n");
    }

    SECTION("in a section written after a section under it")
    {
        CHECK(setOne("[a.b]\ny = 1\n\n[a]\nx = 1\n", { "a", "z" }, 2) == "[a.b]\ny = 1\n\n[a]\nx = 1\nz = 2\n");
    }

    SECTION("next to arrays of tables, which are sections and not values")
    {
        CHECK(setOne("[a]\nx = 1\n\n[[a.items]]\nn = 1\n", { "a", "z" }, 2) == "[a]\nx = 1\nz = 2\n\n[[a.items]]\nn = 1\n");
        CHECK(setOne("[[w]]\nn = 1\n", { "top" }, 1) == "top = 1\n[[w]]\nn = 1\n");
        // an array of inline tables is a value of its section
        CHECK(setOne("[M]\nlist = [{ a = 1 }]\n\n[N]\n", { "M", "z" }, 2) == "[M]\nlist = [{ a = 1 }]\nz = 2\n\n[N]\n");
        CHECK(setOne("[M]\nlist = [{ a = 1 }]\n", { "M", "list" }, toml::array{ toml::table{ { "a", 2 } } }) == "[M]\nlist = [{ a = 2 }]\n");
    }

    SECTION("under an inline table, as a dotted key in its braces")
    {
        CHECK(setOne("[M]\ng = { x = 1 }\n", { "M", "g", "sub", "k" }, 2) == "[M]\ng = { x = 1, sub.k = 2 }\n");
    }

    SECTION("a key that has to be quoted")
    {
        CHECK(setOne("[M]\na = 1\n", { "M", "my key" }, 2) == "[M]\na = 1\n\"my key\" = 2\n");
    }

    SECTION("a key of the root: after its values, or above the first section and the comments right over it")
    {
        CHECK(setOne("x = 1\n[s]\n", { "top" }, 5) == "x = 1\ntop = 5\n[s]\n");
        CHECK(setOne("# file\n\n# about M\n[M]\nx = 1\n", { "top" }, 5) == "# file\n\ntop = 5\n# about M\n[M]\nx = 1\n");
        CHECK(setOne("# only a comment\n", { "top" }, 5) == "# only a comment\ntop = 5\n");
        CHECK(setOne("", { "top" }, 5) == "top = 5\n");
    }
}

TEST_CASE("TomlEditor: an edit it cannot make is refused and the text stays")
{
    SECTION("a text that is not TOML")
    {
        TomlEditor editor("[M]\nx = offhand tap grip\n", "Mod.toml");
        CHECK_FALSE(editor.ok());
        INFO(editor.error());
        CHECK(editor.error().find("line 2") != std::string::npos);
        CHECK(editor.error().find("Mod.toml") != std::string::npos);
        CHECK(editor.error().ends_with(": x = offhand tap grip"));
        CHECK_FALSE(editor.set({ "M", "x" }, 1));
        CHECK_FALSE(editor.set({ { { "M", "x" }, 1 } }));
        CHECK(editor.text() == "[M]\nx = offhand tap grip\n");
    }

    SECTION("the line of the error without the UTF-8 signature")
    {
        TomlEditor editor(
            "\xEF\xBB\xBF"
            "x = = 1\n");
        CHECK_FALSE(editor.ok());
        CHECK(editor.error().ends_with(": x = = 1"));
    }

    const std::string text = "[M]\nx = 1\n\n[[w]]\nn = 1\n";
    TomlEditor editor(text);
    REQUIRE(editor.ok());

    SECTION("a section in place of a value")
    {
        CHECK_FALSE(editor.set({ "M" }, 1));
        CHECK(editor.error().find("M") != std::string::npos);
    }

    SECTION("a path through a value that is not a table")
    {
        CHECK_FALSE(editor.set({ "M", "x", "y" }, 1));
        CHECK(editor.error().find("M.x") != std::string::npos);
    }

    SECTION("an array of tables")
    {
        CHECK_FALSE(editor.set({ "w", "n" }, 2));
        CHECK(editor.error().find("array of tables") != std::string::npos);
        CHECK_FALSE(editor.set({ "w" }, 2));
        CHECK(editor.error().find("array of tables") != std::string::npos);
    }

    SECTION("a value TOML cannot hold")
    {
        CHECK_FALSE(editor.set({ "M", "x" }, std::numeric_limits<std::uint64_t>::max()));
        CHECK(editor.error().find("not a TOML value") != std::string::npos);
    }

    SECTION("no key")
    {
        CHECK_FALSE(editor.set(std::span<const std::string_view>{}, 1));
    }

    CHECK(editor.text() == text);
}

TEST_CASE("TomlEditor: a set of several values is the same as a set of each in order")
{
    SECTION("replaces, adds, a new section and the same value twice")
    {
        const std::vector<TomlEditor::Edit> edits{
            { { "Main", "fScale" }, 72.55f },
            { { "Main", "added" }, 1 },
            { { "Main", "bFlag" }, true },
            { { "New", "k" }, "v" },
            { { "Debug", "iVersion" }, 2 },
            { { "New", "k2" }, false },
            { { "Main", "bFlag" }, false },
        };
        TomlEditor each(SHIPPED);
        for (const auto& edit : edits) {
            REQUIRE(each.set(edit.path, edit.value));
        }
        TomlEditor all(SHIPPED);
        CHECK(all.set(edits));
        CHECK(all.error().empty());
        CHECK(all.text() == each.text());
        CHECK(all.table()["New"]["k2"].value<bool>() == false);
    }

    SECTION("a whole inline table and a value in it, which overlap, are set one at a time")
    {
        TomlEditor editor("[M]\ng = { x = 1 }\n");
        CHECK(editor.set({ { { "M", "g" }, toml::table{ { "x", 2 }, { "y", 3 } } }, { { "M", "g", "x" }, 5 } }));
        CHECK(editor.text() == "[M]\ng = { x = 5, y = 3 }\n");
    }

    SECTION("an edit that fails: the others are made, and the error names it")
    {
        TomlEditor editor("[M]\na = 1\nb = 2\n");
        CHECK_FALSE(editor.set({ { { "M", "a" }, 5 }, { { "M" }, 1 }, { {}, 1 }, { { "M", "b" }, 6 } }));
        CHECK(editor.text() == "[M]\na = 5\nb = 6\n");
        INFO(editor.error());
        CHECK(editor.error().find("M: is a table") != std::string::npos);
        CHECK(editor.error().find("no key to set") != std::string::npos);
    }
}

TEST_CASE("TomlEditor: a value the text already holds stays as it is written")
{
    SECTION("written another way")
    {
        CHECK(setOne("[M]\nf = 71.000 # x\n", { "M", "f" }, 71.0f) == "[M]\nf = 71.000 # x\n");
        CHECK(setOne("[M]\ne = 0x10\n", { "M", "e" }, 16) == "[M]\ne = 0x10\n");
        CHECK(setOne("[M]\ns = 'a'\n", { "M", "s" }, "a") == "[M]\ns = 'a'\n");
        CHECK(setOne("[M]\ng = {y=2,x=1}\n", { "M", "g" }, toml::table{ { "x", 1 }, { "y", 2 } }) == "[M]\ng = {y=2,x=1}\n");
    }

    SECTION("an integer does not hold the float of its number, which a float key reads")
    {
        CHECK(setOne("[M]\nf = 1\n", { "M", "f" }, 1.0f) == "[M]\nf = 1.0\n");
    }

    SECTION("several, and an edit back to what the text holds undoes an earlier one")
    {
        TomlEditor editor(SHIPPED);
        CHECK(editor.set({ { { "Main", "fScale" }, 71.0f }, { { "Main", "bFlag" }, false }, { { "Main", "iMode" }, 0 } }));
        CHECK(editor.set({ { { "Main", "iMode" }, 3 }, { { "Main", "iMode" }, 0 } }));
        CHECK(editor.text() == SHIPPED);
        CHECK(editor.error().empty());
    }

    SECTION("a section is still refused")
    {
        TomlEditor editor("[M]\nx = 1\n");
        CHECK_FALSE(editor.set({ "M" }, toml::table{ { "x", 1 } }));
    }
}

TEST_CASE("TomlEditor: a set after a refused one works")
{
    TomlEditor editor("[M]\nx = 1\n");
    CHECK_FALSE(editor.set({ "M" }, 1));
    CHECK_FALSE(editor.error().empty());
    CHECK(editor.set({ "M", "x" }, 2));
    CHECK(editor.error().empty());
    CHECK(editor.text() == "[M]\nx = 2\n");
}
