#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "imgui/ImGuiKeyboardLogic.h"

using f4cf::imgui::internal::EditKey;
using f4cf::imgui::internal::KeyAction;
using f4cf::imgui::internal::keyAt;
using f4cf::imgui::internal::KEYBOARD_TEXT_FLOOR;
using f4cf::imgui::internal::KeyboardKey;
using f4cf::imgui::internal::KeyboardPage;
using f4cf::imgui::internal::keyboardRows;
using f4cf::imgui::internal::KeyboardTyping;
using f4cf::imgui::internal::KeyRect;
using f4cf::imgui::internal::KeyRepeat;
using f4cf::imgui::internal::KeyStroke;
using f4cf::imgui::internal::KeyStrokeQueue;
using f4cf::imgui::internal::layoutKeys;
using f4cf::imgui::internal::nearestBoundary;
using f4cf::imgui::internal::scrollToShow;
using f4cf::imgui::internal::sharedUtf8Prefix;
using f4cf::imgui::internal::sharedUtf8Suffix;
using f4cf::imgui::internal::TextEdit;
using f4cf::imgui::internal::textEdit;
using f4cf::imgui::internal::utf8Boundaries;
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

namespace
{
    constexpr KeyboardPage PAGES[] = { KeyboardPage::Letters, KeyboardPage::Symbols, KeyboardPage::Accents };

    template <typename Predicate>
    const KeyboardKey& findKey(const KeyboardPage page, Predicate predicate)
    {
        const KeyboardKey* found = nullptr;
        for (const auto row : keyboardRows(page)) {
            for (const KeyboardKey& key : row) {
                if (!found && predicate(key)) {
                    found = &key;
                }
            }
        }
        REQUIRE(found != nullptr);
        return *found;
    }

    const KeyboardKey& keyWith(const KeyboardPage page, const KeyAction action)
    {
        return findKey(page, [action](const KeyboardKey& key) {
            return key.action == action;
        });
    }

    const KeyboardKey& keyLabeled(const KeyboardPage page, const std::string_view label)
    {
        return findKey(page, [label](const KeyboardKey& key) {
            return key.label == label;
        });
    }

    bool near(const float a, const float b)
    {
        return std::abs(a - b) < 0.001f;
    }

    std::string typed(const std::vector<KeyStroke>& strokes)
    {
        return strokes.size() == 1 ? strokes.front().text : std::string("<not one stroke>");
    }
}

TEST_CASE("every page has the editing keys, and no row wider than the letters'", "[imgui][keyboard]")
{
    for (const KeyboardPage page : PAGES) {
        for (const KeyAction action : { KeyAction::Space, KeyAction::Backspace, KeyAction::Clear, KeyAction::Left, KeyAction::Right, KeyAction::Done, KeyAction::Close }) {
            CHECK(keyWith(page, action).action == action);
        }
        for (const auto row : keyboardRows(page)) {
            float units = 0.0f;
            for (const KeyboardKey& key : row) {
                units += key.units;
            }
            CHECK(units <= 11.5f);
        }
    }
}

TEST_CASE("each page key shows another page", "[imgui][keyboard]")
{
    for (const KeyboardPage page : PAGES) {
        for (const auto row : keyboardRows(page)) {
            for (const KeyboardKey& key : row) {
                if (key.action == KeyAction::ToPage) {
                    CHECK(key.page != page);
                }
            }
        }
    }
}

TEST_CASE("the keys fill the area, and a key of more units spans the gaps it covers", "[imgui][keyboard]")
{
    constexpr float width = 1150.0f;
    constexpr float height = 500.0f;
    constexpr float gap = 10.0f;
    const auto keys = layoutKeys(KeyboardPage::Letters, width, height, gap);
    REQUIRE(!keys.empty());
    for (const KeyRect& rect : keys) {
        CHECK(rect.x >= -0.01f);
        CHECK(rect.y >= -0.01f);
        CHECK(rect.x + rect.width <= width + 0.01f);
        CHECK(rect.y + rect.height <= height + 0.01f);
    }
    // the digits' row is the widest: from one edge to the other
    CHECK(std::abs(keys.front().x) < 0.01f);
    const auto& backspace = *std::ranges::find_if(keys, [](const KeyRect& rect) {
        return rect.key->action == KeyAction::Backspace;
    });
    CHECK(std::abs(backspace.x + backspace.width - width) < 0.01f);
    // a key of 1.5 units is one and a half keys and half a gap wide
    const float unit = keys.front().width;
    CHECK(std::abs(backspace.width - (1.5f * unit + 0.5f * gap)) < 0.01f);
    // the last row reaches the bottom
    CHECK(std::abs(keys.back().y + keys.back().height - height) < 0.01f);
}

TEST_CASE("a press is on the key it is on, or on the key beside the gap it is in", "[imgui][keyboard]")
{
    constexpr float gap = 10.0f;
    const auto keys = layoutKeys(KeyboardPage::Letters, 1150.0f, 500.0f, gap);
    const KeyRect& first = keys[0];
    const KeyRect& second = keys[1];
    CHECK(keyAt(keys, first.x + first.width * 0.5f, first.y + first.height * 0.5f, gap) == &first);
    // in the gap between the first two keys, either side of its middle
    const float gapMiddle = first.x + first.width + gap * 0.5f;
    CHECK(keyAt(keys, gapMiddle - 1.0f, first.y + 1.0f, gap) == &first);
    CHECK(keyAt(keys, gapMiddle + 1.0f, first.y + 1.0f, gap) == &second);
    CHECK(keyAt(keys, -50.0f, 10.0f, gap) == nullptr);
    CHECK(keyAt(keys, 10.0f, 600.0f, gap) == nullptr);
}

TEST_CASE("a letter is typed small, and capital with Shift, for one character", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    const KeyboardKey& a = keyLabeled(KeyboardPage::Letters, "a");
    CHECK(typed(typing.press(a)) == "a");
    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Shift));
    CHECK(typing.shift());
    CHECK(typing.isOn(keyWith(KeyboardPage::Letters, KeyAction::Shift)));
    CHECK(typing.labelOf(a) == "A");
    CHECK(typed(typing.press(a)) == "A");
    CHECK(!typing.shift());
    CHECK(typed(typing.press(a)) == "a");
}

TEST_CASE("Caps shifts every letter until it is pressed again, and Shift with Caps types a small one", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    const KeyboardKey& a = keyLabeled(KeyboardPage::Letters, "a");
    const KeyboardKey& caps = keyWith(KeyboardPage::Letters, KeyAction::Caps);
    typing.press(caps);
    CHECK(typed(typing.press(a)) == "A");
    CHECK(typed(typing.press(a)) == "A");
    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Shift));
    CHECK(typed(typing.press(a)) == "a");
    typing.press(caps);
    CHECK(typed(typing.press(a)) == "a");
}

TEST_CASE("Caps does not shift a digit, and Shift types its symbol", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    const KeyboardKey& one = keyLabeled(KeyboardPage::Letters, "1");
    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Caps));
    CHECK(typed(typing.press(one)) == "1");
    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Shift));
    CHECK(typed(typing.press(one)) == "!");
}

TEST_CASE("an accented letter is typed capital with Shift", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    const KeyboardKey& eAcute = keyLabeled(KeyboardPage::Accents, "\xC3\xA9");
    CHECK(typed(typing.press(eAcute)) == "\xC3\xA9");
    typing.press(keyWith(KeyboardPage::Accents, KeyAction::Shift));
    CHECK(typed(typing.press(eAcute)) == "\xC3\x89");
}

TEST_CASE("a page key shows its page, and a space lets go of Shift", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    CHECK(typing.page() == KeyboardPage::Letters);
    typing.press(keyLabeled(KeyboardPage::Letters, "?123"));
    CHECK(typing.page() == KeyboardPage::Symbols);
    typing.press(keyLabeled(KeyboardPage::Symbols, "abc"));
    CHECK(typing.page() == KeyboardPage::Letters);

    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Shift));
    CHECK(typed(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Space))) == " ");
    CHECK(!typing.shift());
}

TEST_CASE("the editing keys are key strokes, and Clear selects all and then deletes", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    using Strokes = std::vector<KeyStroke>;
    CHECK(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Backspace)) == Strokes{ { .keys = { EditKey::Backspace } } });
    CHECK(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Left)) == Strokes{ { .keys = { EditKey::Left } } });
    CHECK(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Right)) == Strokes{ { .keys = { EditKey::Right } } });
    CHECK(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Clear)) == Strokes{ { .keys = { EditKey::Ctrl, EditKey::A } }, { .keys = { EditKey::Backspace } } });
    CHECK(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Done)) == Strokes{ { .keys = { EditKey::Enter } } });
    CHECK(typing.press(keyWith(KeyboardPage::Letters, KeyAction::Close)) == Strokes{ { .keys = { EditKey::Escape } } });
}

TEST_CASE("Backspace and the caret keys repeat, and the others do not", "[imgui][keyboard]")
{
    CHECK(keyWith(KeyboardPage::Letters, KeyAction::Backspace).repeats());
    CHECK(keyWith(KeyboardPage::Letters, KeyAction::Left).repeats());
    CHECK(keyWith(KeyboardPage::Letters, KeyAction::Right).repeats());
    CHECK(!keyWith(KeyboardPage::Letters, KeyAction::Clear).repeats());
    CHECK(!keyLabeled(KeyboardPage::Letters, "a").repeats());
}

TEST_CASE("reset goes back to the letters with Shift and Caps off", "[imgui][keyboard]")
{
    KeyboardTyping typing;
    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Caps));
    typing.press(keyWith(KeyboardPage::Letters, KeyAction::Shift));
    typing.press(keyLabeled(KeyboardPage::Letters, "?123"));
    typing.reset();
    CHECK(typing.page() == KeyboardPage::Letters);
    CHECK(!typing.shift());
    CHECK(!typing.caps());
}

TEST_CASE("a held key fires as it is pressed, then after the delay at the interval", "[imgui][keyboard]")
{
    // a binary fraction, so the delay is reached exactly
    constexpr float frame = 1.0f / 64.0f;
    KeyRepeat repeat;
    CHECK(repeat.update(true, frame) == 1);
    int fired = 0;
    // up to the frame before the delay ends
    for (int i = 0; i < 31; ++i) {
        fired += repeat.update(true, frame);
    }
    CHECK(fired == 0);
    CHECK(repeat.update(true, frame) == 1);
    // a second more: one every 50 ms
    fired = 0;
    for (int i = 0; i < 64; ++i) {
        fired += repeat.update(true, frame);
    }
    CHECK(fired >= 19);
    CHECK(fired <= 21);

    // let go and pressed again: at once, and then the delay again
    CHECK(repeat.update(false, frame) == 0);
    CHECK(repeat.update(true, frame) == 1);
    CHECK(repeat.update(true, frame) == 0);
}

TEST_CASE("the strokes are given one a frame, keys pressed in one and let go of in the next", "[imgui][keyboard]")
{
    KeyStrokeQueue queue;
    queue.push({ .text = "a" });
    queue.push({ .keys = { EditKey::Ctrl, EditKey::A } });
    queue.push({ .keys = { EditKey::Backspace } });

    auto input = queue.next();
    CHECK(input.text == "a");
    CHECK(input.pressed.empty());

    input = queue.next();
    CHECK(input.text.empty());
    CHECK(input.pressed == std::vector{ EditKey::Ctrl, EditKey::A });

    // let go of with the modifier last, and nothing else in that frame
    input = queue.next();
    CHECK(input.released == std::vector{ EditKey::A, EditKey::Ctrl });
    CHECK(input.pressed.empty());

    input = queue.next();
    CHECK(input.pressed == std::vector{ EditKey::Backspace });
    input = queue.next();
    CHECK(input.released == std::vector{ EditKey::Backspace });
    CHECK(queue.empty());
    CHECK(queue.next().empty());
}

TEST_CASE("cleared, the strokes not given are dropped, and a key down is still let go of", "[imgui][keyboard]")
{
    KeyStrokeQueue queue;
    queue.push({ .keys = { EditKey::Backspace } });
    queue.push({ .text = "a" });
    CHECK(queue.next().pressed == std::vector{ EditKey::Backspace });
    queue.clear();
    CHECK(queue.next().released == std::vector{ EditKey::Backspace });
    CHECK(queue.next().empty());
}

TEST_CASE("a caret can be before each character and at the end", "[imgui][keyboard]")
{
    const std::string text = std::string("a") + std::string(E_ACUTE) + "b";
    CHECK(utf8Boundaries(text) == std::vector<std::size_t>{ 0, 1, 3, 4 });
    CHECK(utf8Boundaries("") == std::vector<std::size_t>{ 0 });
}

TEST_CASE("a press on the text puts the caret at the nearest place", "[imgui][keyboard]")
{
    const std::vector<float> places = { 0.0f, 10.0f, 22.0f, 30.0f };
    CHECK(nearestBoundary(places, -5.0f) == 0);
    CHECK(nearestBoundary(places, 4.0f) == 0);
    CHECK(nearestBoundary(places, 6.0f) == 1);
    CHECK(nearestBoundary(places, 27.0f) == 3);
    CHECK(nearestBoundary(places, 100.0f) == 3);
    CHECK(nearestBoundary({}, 5.0f) == 0);
}

TEST_CASE("the text scrolls as little as shows the caret, and never past its end", "[imgui][keyboard]")
{
    // fits: no scroll
    CHECK(near(scrollToShow(0.0f, 50.0f, 100.0f, 80.0f), 0.0f));
    // the caret past the right edge: the view's right edge is at the caret
    CHECK(near(scrollToShow(0.0f, 150.0f, 100.0f, 200.0f), 50.0f));
    // the caret before the left edge: the view starts at the caret
    CHECK(near(scrollToShow(80.0f, 20.0f, 100.0f, 200.0f), 20.0f));
    // the caret in view: the scroll stays
    CHECK(near(scrollToShow(30.0f, 60.0f, 100.0f, 200.0f), 30.0f));
    // the text got shorter: no room is left after its end
    CHECK(near(scrollToShow(90.0f, 120.0f, 100.0f, 120.0f), 20.0f));
}
