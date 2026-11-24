#include "ImGuiKeyboardLogic.h"

#include <array>
#include <cmath>
#include <numeric>
#include <ranges>

namespace f4cf::imgui::internal
{
    namespace
    {
        constexpr KeyboardKey letter(const std::string_view label, const std::string_view shifted)
        {
            return { .label = label, .shifted = shifted, .letter = true };
        }

        constexpr KeyboardKey character(const std::string_view label, const std::string_view shifted)
        {
            return { .label = label, .shifted = shifted };
        }

        constexpr KeyboardKey symbol(const std::string_view label)
        {
            return { .label = label, .shifted = label };
        }

        constexpr KeyboardKey action(const std::string_view label, const float units, const KeyAction action)
        {
            return { .label = label, .shifted = label, .units = units, .action = action };
        }

        constexpr KeyboardKey toPage(const std::string_view label, const KeyboardPage page)
        {
            return { .label = label, .shifted = label, .units = 1.25f, .action = KeyAction::ToPage, .page = page };
        }

        // The keys every page has. The pages follow a US PC keyboard, as SteamVR's does: rows 15 units wide, staggered
        // by the widths of the keys at their ends; a row with fewer keys is centered. Clear is where Tab is, which a
        // one-line field has no use for, and Done at the bottom right, as SteamVR's key that hides its keyboard. Over
        // the keys, the line of the field's text, with the keys that move the caret at its right.
        constexpr KeyboardKey LINE = action("", 13.0f, KeyAction::Line);
        constexpr KeyboardKey LEFT = action("Left", 1.0f, KeyAction::Left);
        constexpr KeyboardKey RIGHT = action("Right", 1.0f, KeyAction::Right);
        constexpr KeyboardKey BACKSPACE = action("Backspace", 2.0f, KeyAction::Backspace);
        constexpr KeyboardKey CLEAR = action("Clear", 1.5f, KeyAction::Clear);
        constexpr KeyboardKey CAPS = action("Caps", 1.75f, KeyAction::Caps);
        constexpr KeyboardKey ENTER = action("Enter", 2.25f, KeyAction::NewLine);
        constexpr KeyboardKey SHIFT_LEFT = action("Shift", 2.25f, KeyAction::Shift);
        constexpr KeyboardKey SHIFT_RIGHT = action("Shift", 2.75f, KeyAction::Shift);
        constexpr KeyboardKey SPACE = action("Space", 10.0f, KeyAction::Space);
        constexpr KeyboardKey DONE = action("Done", 2.5f, KeyAction::Done);
        constexpr KeyboardKey TO_LETTERS = toPage("abc", KeyboardPage::Letters);
        constexpr KeyboardKey TO_SYMBOLS = toPage("?123", KeyboardPage::Symbols);
        constexpr KeyboardKey TO_ACCENTS = toPage("\xC3\xA1\xC3\xA9", KeyboardPage::Accents);

        // the letters, and what a US keyboard types with Shift
        constexpr std::array LETTERS_0 = { character("`", "~"),
            character("1", "!"),
            character("2", "@"),
            character("3", "#"),
            character("4", "$"),
            character("5", "%"),
            character("6", "^"),
            character("7", "&"),
            character("8", "*"),
            character("9", "("),
            character("0", ")"),
            character("-", "_"),
            character("=", "+"),
            BACKSPACE };
        constexpr std::array LETTERS_1 = { CLEAR,
            letter("q", "Q"),
            letter("w", "W"),
            letter("e", "E"),
            letter("r", "R"),
            letter("t", "T"),
            letter("y", "Y"),
            letter("u", "U"),
            letter("i", "I"),
            letter("o", "O"),
            letter("p", "P"),
            character("[", "{"),
            character("]", "}"),
            KeyboardKey{ .label = "\\", .shifted = "|", .units = 1.5f } };
        constexpr std::array LETTERS_2 = { CAPS,
            letter("a", "A"),
            letter("s", "S"),
            letter("d", "D"),
            letter("f", "F"),
            letter("g", "G"),
            letter("h", "H"),
            letter("j", "J"),
            letter("k", "K"),
            letter("l", "L"),
            character(";", ":"),
            character("'", "\""),
            ENTER };
        constexpr std::array LETTERS_3 = { SHIFT_LEFT,
            letter("z", "Z"),
            letter("x", "X"),
            letter("c", "C"),
            letter("v", "V"),
            letter("b", "B"),
            letter("n", "N"),
            letter("m", "M"),
            character(",", "<"),
            character(".", ">"),
            character("/", "?"),
            SHIFT_RIGHT };
        constexpr std::array LETTERS_4 = { TO_SYMBOLS, TO_ACCENTS, SPACE, DONE };

        // the symbols of the keyboard, each a key of its own, and the marks and signs of Latin-1 a US keyboard does not have
        constexpr std::array SYMBOLS_0 = { symbol("~"),
            symbol("!"),
            symbol("@"),
            symbol("#"),
            symbol("$"),
            symbol("%"),
            symbol("^"),
            symbol("&"),
            symbol("*"),
            symbol("("),
            symbol(")"),
            symbol("_"),
            symbol("+"),
            BACKSPACE };
        constexpr std::array SYMBOLS_1 = { CLEAR,
            symbol("`"),
            symbol("-"),
            symbol("="),
            symbol("["),
            symbol("]"),
            symbol("{"),
            symbol("}"),
            symbol("\\"),
            symbol("|"),
            symbol("<"),
            symbol(">"),
            symbol("/"),
            KeyboardKey{ .label = "?", .shifted = "?", .units = 1.5f } };
        constexpr std::array SYMBOLS_2 = { KeyboardKey{ .label = ";", .shifted = ";", .units = 1.75f },
            symbol(":"),
            symbol("'"),
            symbol("\""),
            symbol(","),
            symbol("."),
            symbol("\xE2\x82\xAC"),
            symbol("\xC2\xA3"),
            symbol("\xC2\xA5"),
            symbol("\xC2\xB0"),
            symbol("\xC2\xA7"),
            symbol("\xC2\xB6"),
            ENTER };
        constexpr std::array SYMBOLS_3 = { KeyboardKey{ .label = "\xC2\xBF", .shifted = "\xC2\xBF", .units = 2.25f },
            symbol("\xC2\xA1"),
            symbol("\xC2\xAB"),
            symbol("\xC2\xBB"),
            symbol("\xC2\xA9"),
            symbol("\xC2\xAE"),
            symbol("\xC2\xB1"),
            symbol("\xC3\x97"),
            symbol("\xC3\xB7"),
            symbol("\xC2\xB5"),
            symbol("\xC2\xA2"),
            symbol("\xC2\xB2"),
            KeyboardKey{ .label = "\xC2\xB3", .shifted = "\xC2\xB3", .units = 1.75f } };
        constexpr std::array SYMBOLS_4 = { TO_LETTERS, TO_ACCENTS, SPACE, DONE };

        // The accented letters of Latin-1 and of the languages of Europe beside it, all in the framework's Roboto, with
        // their capitals. Escaped, as the source is read in the system's code page.
        constexpr std::array ACCENTS_0 = { letter("\xC3\xA1", "\xC3\x81"),
            letter("\xC3\xA0", "\xC3\x80"),
            letter("\xC3\xA2", "\xC3\x82"),
            letter("\xC3\xA4", "\xC3\x84"),
            letter("\xC3\xA3", "\xC3\x83"),
            letter("\xC3\xA5", "\xC3\x85"),
            letter("\xC3\xA6", "\xC3\x86"),
            letter("\xC3\xA7", "\xC3\x87"),
            letter("\xC3\xB1", "\xC3\x91"),
            letter("\xC3\x9F", "\xC3\x9F"),
            letter("\xC5\x99", "\xC5\x98"),
            letter("\xC5\x91", "\xC5\x90"),
            letter("\xC5\xB1", "\xC5\xB0"),
            BACKSPACE };
        constexpr std::array ACCENTS_1 = { CLEAR,
            letter("\xC3\xA9", "\xC3\x89"),
            letter("\xC3\xA8", "\xC3\x88"),
            letter("\xC3\xAA", "\xC3\x8A"),
            letter("\xC3\xAB", "\xC3\x8B"),
            letter("\xC3\xAD", "\xC3\x8D"),
            letter("\xC3\xAC", "\xC3\x8C"),
            letter("\xC3\xAE", "\xC3\x8E"),
            letter("\xC3\xAF", "\xC3\x8F"),
            letter("\xC3\xBD", "\xC3\x9D"),
            letter("\xC3\xBF", "\xC5\xB8"),
            letter("\xC4\x9F", "\xC4\x9E"),
            letter("\xC5\x9F", "\xC5\x9E"),
            KeyboardKey{ .label = "\xC5\xBE", .shifted = "\xC5\xBD", .units = 1.5f, .letter = true } };
        constexpr std::array ACCENTS_2 = { CAPS,
            letter("\xC3\xB3", "\xC3\x93"),
            letter("\xC3\xB2", "\xC3\x92"),
            letter("\xC3\xB4", "\xC3\x94"),
            letter("\xC3\xB6", "\xC3\x96"),
            letter("\xC3\xB5", "\xC3\x95"),
            letter("\xC3\xB8", "\xC3\x98"),
            letter("\xC5\x93", "\xC5\x92"),
            letter("\xC3\xBA", "\xC3\x9A"),
            letter("\xC3\xB9", "\xC3\x99"),
            letter("\xC3\xBB", "\xC3\x9B"),
            letter("\xC3\xBC", "\xC3\x9C"),
            ENTER };
        constexpr std::array ACCENTS_3 = { SHIFT_LEFT,
            letter("\xC4\x85", "\xC4\x84"),
            letter("\xC4\x99", "\xC4\x98"),
            letter("\xC4\x87", "\xC4\x86"),
            letter("\xC5\x82", "\xC5\x81"),
            letter("\xC5\x84", "\xC5\x83"),
            letter("\xC5\x9B", "\xC5\x9A"),
            letter("\xC5\xBA", "\xC5\xB9"),
            letter("\xC5\xBC", "\xC5\xBB"),
            letter("\xC4\x8D", "\xC4\x8C"),
            letter("\xC5\xA1", "\xC5\xA0"),
            SHIFT_RIGHT };
        constexpr std::array ACCENTS_4 = { TO_LETTERS, TO_SYMBOLS, SPACE, DONE };

        constexpr std::array LINE_ROW = { LINE, LEFT, RIGHT };

        constexpr std::array<std::span<const KeyboardKey>, 6> LETTERS = { LINE_ROW, LETTERS_0, LETTERS_1, LETTERS_2, LETTERS_3, LETTERS_4 };
        constexpr std::array<std::span<const KeyboardKey>, 6> SYMBOLS = { LINE_ROW, SYMBOLS_0, SYMBOLS_1, SYMBOLS_2, SYMBOLS_3, SYMBOLS_4 };
        constexpr std::array<std::span<const KeyboardKey>, 6> ACCENTS = { LINE_ROW, ACCENTS_0, ACCENTS_1, ACCENTS_2, ACCENTS_3, ACCENTS_4 };

        float rowUnits(const std::span<const KeyboardKey> row)
        {
            return std::accumulate(row.begin(), row.end(), 0.0f, [](const float sum, const KeyboardKey& key) {
                return sum + key.units;
            });
        }
    }

    /**
     * The rows of a page, from the top: the line of the field's text with the caret keys, then the keys.
     */
    std::span<const std::span<const KeyboardKey>> keyboardRows(const KeyboardPage page)
    {
        switch (page) {
        case KeyboardPage::Symbols:
            return SYMBOLS;
        case KeyboardPage::Accents:
            return ACCENTS;
        case KeyboardPage::Letters:
            break;
        }
        return LETTERS;
    }

    /**
     * The keys of a page in an area of the given size, with the given gap between keys and rows. Every key unit
     * has one width, which fits the widest row, and a key of more units also spans the gaps it covers, so the
     * edges of the keys line up from row to row. A narrower row is centered.
     */
    std::vector<KeyRect> layoutKeys(const KeyboardPage page, const float width, const float height, const float gap)
    {
        const auto rows = keyboardRows(page);
        if (rows.empty()) {
            return {};
        }
        float widestUnits = 0.0f;
        for (const auto row : rows) {
            widestUnits = (std::max)(widestUnits, rowUnits(row));
        }
        const float unit = (std::max)(0.0f, (width - (widestUnits - 1.0f) * gap) / widestUnits);
        const float keyHeight = (std::max)(0.0f, (height - static_cast<float>(rows.size() - 1) * gap) / static_cast<float>(rows.size()));

        std::vector<KeyRect> keys;
        float y = 0.0f;
        for (const auto row : rows) {
            const float units = rowUnits(row);
            float x = (width - (units * unit + (units - 1.0f) * gap)) * 0.5f;
            for (const KeyboardKey& key : row) {
                const float keyWidth = key.units * unit + (key.units - 1.0f) * gap;
                keys.push_back({ .key = &key, .x = x, .y = y, .width = keyWidth, .height = keyHeight });
                x += keyWidth + gap;
            }
            y += keyHeight + gap;
        }
        return keys;
    }

    /**
     * The key at a point of the keys' area: a press in the gap beside a key is that key's, so no press between
     * two keys is lost. Null outside every key.
     */
    const KeyRect* keyAt(const std::span<const KeyRect> keys, const float x, const float y, const float gap)
    {
        const float half = gap * 0.5f;
        const auto found = std::ranges::find_if(keys, [&](const KeyRect& rect) {
            return x >= rect.x - half && x < rect.x + rect.width + half && y >= rect.y - half && y < rect.y + rect.height + half;
        });
        return found != keys.end() ? &*found : nullptr;
    }

    /**
     * What a press of the key gives the field, in order, and the page, Shift and Caps after it. A character or a
     * space lets go of Shift. Clear selects the whole text and then deletes it. Enter starts a new line of a
     * multiline field, and does nothing on a one-line field; Done leaves the field with its text. A press on the
     * line gives nothing: the keyboard puts the caret where it was.
     */
    std::vector<KeyStroke> KeyboardTyping::press(const KeyboardKey& key)
    {
        switch (key.action) {
        case KeyAction::Character: {
            const std::string_view typed = labelOf(key);
            _shift = false;
            return { KeyStroke{ .text = std::string(typed) } };
        }
        case KeyAction::Space:
            _shift = false;
            return { KeyStroke{ .text = " " } };
        case KeyAction::Backspace:
            return { KeyStroke{ .keys = { EditKey::Backspace } } };
        case KeyAction::Clear:
            return { KeyStroke{ .keys = { EditKey::Ctrl, EditKey::A } }, KeyStroke{ .keys = { EditKey::Backspace } } };
        case KeyAction::Left:
            return { KeyStroke{ .keys = { EditKey::Left } } };
        case KeyAction::Right:
            return { KeyStroke{ .keys = { EditKey::Right } } };
        case KeyAction::Shift:
            _shift = !_shift;
            return {};
        case KeyAction::Caps:
            _caps = !_caps;
            return {};
        case KeyAction::ToPage:
            _page = key.page;
            return {};
        case KeyAction::NewLine:
            if (!_field.multiline) {
                return {};
            }
            return { _field.ctrlEnterForNewLine ? KeyStroke{ .keys = { EditKey::Ctrl, EditKey::Enter } } : KeyStroke{ .keys = { EditKey::Enter } } };
        case KeyAction::Done:
            return done();
        case KeyAction::Line:
            return {};
        }
        return {};
    }

    /**
     * What leaves the field with its text, as Done does: Enter on a one-line field, and on a multiline one the
     * keys ImGui leaves it with, Ctrl+Enter, or Enter where Ctrl+Enter starts a new line.
     */
    std::vector<KeyStroke> KeyboardTyping::done() const
    {
        if (_field.multiline && !_field.ctrlEnterForNewLine) {
            return { KeyStroke{ .keys = { EditKey::Ctrl, EditKey::Enter } } };
        }
        return { KeyStroke{ .keys = { EditKey::Enter } } };
    }

    /**
     * Whether a Shift or Caps key is on, to draw it pressed.
     */
    bool KeyboardTyping::isOn(const KeyboardKey& key) const
    {
        return (key.action == KeyAction::Shift && _shift) || (key.action == KeyAction::Caps && _caps);
    }

    /**
     * Whether a press of the key does something: Enter only on a multiline field, which takes new lines.
     */
    bool KeyboardTyping::isEnabled(const KeyboardKey& key) const
    {
        return key.action != KeyAction::NewLine || _field.multiline;
    }

    /**
     * What the key shows, which a character key types: shifted while Shift is on, and a letter also while Caps is.
     */
    std::string_view KeyboardTyping::labelOf(const KeyboardKey& key) const
    {
        const bool shifted = key.letter ? _shift != _caps : _shift;
        return key.action == KeyAction::Character && shifted ? key.shifted : key.label;
    }

    /**
     * Back to the letters, with Shift and Caps off, for the next field. The field's kind is set apart.
     */
    void KeyboardTyping::reset()
    {
        _page = KeyboardPage::Letters;
        _shift = false;
        _caps = false;
    }

    /**
     * How many times a held key fires in this frame: once as it is pressed, and after DELAY_SECONDS once every
     * INTERVAL_SECONDS while it is held. A long frame fires the repeats it held.
     */
    int KeyRepeat::update(const bool held, const float deltaSeconds)
    {
        if (!held) {
            _held = false;
            return 0;
        }
        if (!_held) {
            _held = true;
            _untilNext = DELAY_SECONDS;
            return 1;
        }
        _untilNext -= deltaSeconds;
        int fired = 0;
        while (_untilNext <= 0.0f) {
            ++fired;
            _untilNext += INTERVAL_SECONDS;
        }
        return fired;
    }

    void KeyStrokeQueue::push(KeyStroke stroke)
    {
        _strokes.push_back(std::move(stroke));
    }

    /**
     * This frame's input. ImGui takes all of a frame's events in that frame, so a key pressed and let go in one
     * frame would not be seen as pressed: the keys of a stroke are pressed in one frame and let go in the next, and
     * no other stroke is given in either. Text is given in a frame of its own too, so the field takes the strokes
     * in the order they were typed.
     */
    KeyFrameInput KeyStrokeQueue::next()
    {
        KeyFrameInput input;
        if (!_held.empty()) {
            // let go of in the reverse order, so a modifier is let go of last
            input.released.assign(_held.rbegin(), _held.rend());
            _held.clear();
            return input;
        }
        if (_strokes.empty()) {
            return input;
        }
        KeyStroke stroke = std::move(_strokes.front());
        _strokes.pop_front();
        input.text = std::move(stroke.text);
        input.pressed = stroke.keys;
        _held = std::move(stroke.keys);
        return input;
    }

    /**
     * Drops the strokes not given yet. Keys that are down are still let go of in the next frame.
     */
    void KeyStrokeQueue::clear()
    {
        _strokes.clear();
    }

    /**
     * The byte offsets of a UTF-8 text where a character starts, and its end: every place a caret can be.
     */
    std::vector<std::size_t> utf8Boundaries(const std::string_view text)
    {
        std::vector<std::size_t> boundaries;
        for (std::size_t at = 0; at < text.size(); ++at) {
            if (!detail::isUtf8Continuation(text, at)) {
                boundaries.push_back(at);
            }
        }
        boundaries.push_back(text.size());
        return boundaries;
    }

    /**
     * The index of the place nearest to x of places given by their x, from the left: where a press on a text puts
     * the caret. 0 with none.
     */
    std::size_t nearestBoundary(const std::span<const float> boundaryX, const float x)
    {
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < boundaryX.size(); ++i) {
            if (std::abs(boundaryX[i] - x) < std::abs(boundaryX[nearest] - x)) {
                nearest = i;
            }
        }
        return nearest;
    }

    /**
     * The scroll of a one-line view of a text, given the last one, that shows the caret: it moves only as far as
     * the caret needs, and never past the end of the text, so a text that got shorter is not left scrolled off.
     */
    float scrollToShow(float scroll, const float caretX, const float viewWidth, const float textWidth)
    {
        scroll = std::clamp(scroll, 0.0f, (std::max)(0.0f, textWidth - viewWidth));
        if (caretX - scroll > viewWidth) {
            scroll = caretX - viewWidth;
        } else if (caretX < scroll) {
            scroll = caretX;
        }
        return scroll;
    }
}
