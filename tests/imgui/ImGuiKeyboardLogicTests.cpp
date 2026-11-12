#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "imgui/ImGuiKeyboardLogic.h"

using f4cf::imgui::internal::KEYBOARD_TEXT_FLOOR;
using f4cf::imgui::internal::sharedUtf8Prefix;
using f4cf::imgui::internal::sharedUtf8Suffix;
using f4cf::imgui::internal::TextEdit;
using f4cf::imgui::internal::textEdit;
using f4cf::imgui::internal::withoutFloor;

namespace
{
    // e with an acute accent and e with a grave accent: two bytes each, with the same first byte
    constexpr std::string_view E_ACUTE = "\xC3\xA9";
    constexpr std::string_view E_GRAVE = "\xC3\xA8";

    bool isEdit(const TextEdit& edit, const std::size_t from, const std::size_t to, const std::string_view insert)
    {
        return edit.from == from && edit.to == to && edit.insert == insert;
    }
}

TEST_CASE("texts share the bytes they start with", "[imgui][keyboard]")
{
    CHECK(sharedUtf8Prefix("abc", "abd") == 2);
    CHECK(sharedUtf8Prefix("abc", "abc") == 3);
    CHECK(sharedUtf8Prefix("ab", "abc") == 2);
    CHECK(sharedUtf8Prefix("abc", "") == 0);
    CHECK(sharedUtf8Prefix("", "") == 0);
}

TEST_CASE("a character that starts the same in two texts and then differs is not shared", "[imgui][keyboard]")
{
    const std::string acute = std::string("ab") + std::string(E_ACUTE);
    const std::string grave = std::string("ab") + std::string(E_GRAVE);
    CHECK(sharedUtf8Prefix(acute, grave) == 2);
}

TEST_CASE("texts share the bytes they end with, up to a length", "[imgui][keyboard]")
{
    CHECK(sharedUtf8Suffix("xbc", "ybc", 3) == 2);
    CHECK(sharedUtf8Suffix("xbc", "ybc", 1) == 1);
    CHECK(sharedUtf8Suffix("abc", "", 3) == 0);
}

TEST_CASE("the end shared starts on a whole character", "[imgui][keyboard]")
{
    // the second byte of both accented e's differs, so only "x" is shared
    const std::string acute = std::string(E_ACUTE) + "x";
    const std::string grave = std::string(E_GRAVE) + "x";
    CHECK(sharedUtf8Suffix(acute, grave, 3) == 1);
    // an accented e both end with is shared whole
    const std::string a = std::string("a") + std::string(E_ACUTE);
    const std::string b = std::string("b") + std::string(E_ACUTE);
    CHECK(sharedUtf8Suffix(a, b, 3) == E_ACUTE.size());
}

TEST_CASE("a field with the keyboard's text needs no edit", "[imgui][keyboard]")
{
    CHECK(textEdit("name", "name").empty());
    CHECK(textEdit("", "").empty());
}

TEST_CASE("characters typed at the end are inserted there", "[imgui][keyboard]")
{
    CHECK(isEdit(textEdit("nam", "name"), 3, 3, "e"));
    CHECK(isEdit(textEdit("", "name"), 0, 0, "name"));
}

TEST_CASE("characters deleted are taken away where they were", "[imgui][keyboard]")
{
    CHECK(isEdit(textEdit("name", "nam"), 3, 4, ""));
    CHECK(isEdit(textEdit("name", "nme"), 1, 2, ""));
    CHECK(isEdit(textEdit("name", ""), 0, 4, ""));
}

TEST_CASE("a character changed in the middle is replaced there", "[imgui][keyboard]")
{
    CHECK(isEdit(textEdit("name", "nome"), 1, 2, "o"));
    CHECK(isEdit(textEdit("name", "nXYme"), 1, 2, "XY"));
}

TEST_CASE("a repeated character is shared once, by the start or by the end", "[imgui][keyboard]")
{
    CHECK(isEdit(textEdit("aa", "a"), 1, 2, ""));
    CHECK(isEdit(textEdit("a", "aa"), 1, 1, "a"));
}

TEST_CASE("a character of more than one byte that changes is replaced whole", "[imgui][keyboard]")
{
    const std::string field = std::string("a") + std::string(E_ACUTE) + "b";
    const std::string keyboard = std::string("a") + std::string(E_GRAVE) + "b";
    CHECK(isEdit(textEdit(field, keyboard), 1, 3, E_GRAVE));
}

TEST_CASE("the keyboard's text is the field's after the floor it starts with", "[imgui][keyboard]")
{
    CHECK(withoutFloor(std::string(KEYBOARD_TEXT_FLOOR) + "name") == "name");
    CHECK(withoutFloor(KEYBOARD_TEXT_FLOOR).empty());
    // the player deleted the floor too
    CHECK(withoutFloor("name") == "name");
}
