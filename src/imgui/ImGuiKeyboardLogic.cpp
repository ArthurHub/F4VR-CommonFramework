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

        // The keys every page has. The rows of a page are 11.5 units wide or less, and narrower ones are centered.
        constexpr KeyboardKey BACKSPACE = action("Back", 1.5f, KeyAction::Backspace);
        constexpr KeyboardKey CLEAR = action("Clear", 1.5f, KeyAction::Clear);
        constexpr KeyboardKey SHIFT = action("Shift", 1.5f, KeyAction::Shift);
        constexpr KeyboardKey CLOSE = action("Close", 1.25f, KeyAction::Close);
        constexpr KeyboardKey SPACE = action("Space", 3.75f, KeyAction::Space);
        constexpr KeyboardKey LEFT = action("Left", 1.0f, KeyAction::Left);
        constexpr KeyboardKey RIGHT = action("Right", 1.0f, KeyAction::Right);
        constexpr KeyboardKey DONE = action("Done", 2.0f, KeyAction::Done);
        constexpr KeyboardKey TO_LETTERS = toPage("abc", KeyboardPage::Letters);
        constexpr KeyboardKey TO_SYMBOLS = toPage("?123", KeyboardPage::Symbols);
        constexpr KeyboardKey TO_ACCENTS = toPage("\xC3\xA1\xC3\xA9", KeyboardPage::Accents);

        // QWERTY, with the digits above it and what a US keyboard types with Shift
        constexpr std::array LETTERS_0 = { character("1", "!"),
            character("2", "@"),
            character("3", "#"),
            character("4", "$"),
            character("5", "%"),
            character("6", "^"),
            character("7", "&"),
            character("8", "*"),
            character("9", "("),
            character("0", ")"),
            BACKSPACE };
        constexpr std::array LETTERS_1 = { letter("q", "Q"),
            letter("w", "W"),
            letter("e", "E"),
            letter("r", "R"),
            letter("t", "T"),
            letter("y", "Y"),
            letter("u", "U"),
            letter("i", "I"),
            letter("o", "O"),
            letter("p", "P"),
            CLEAR };
        constexpr std::array LETTERS_2 = { action("Caps", 1.5f, KeyAction::Caps),
            letter("a", "A"),
            letter("s", "S"),
            letter("d", "D"),
            letter("f", "F"),
            letter("g", "G"),
            letter("h", "H"),
            letter("j", "J"),
            letter("k", "K"),
            letter("l", "L"),
            character("'", "\"") };
        constexpr std::array LETTERS_3 = { SHIFT,
            letter("z", "Z"),
            letter("x", "X"),
            letter("c", "C"),
            letter("v", "V"),
            letter("b", "B"),
            letter("n", "N"),
            letter("m", "M"),
            character(",", "<"),
            character(".", ">"),
            character("-", "_") };
        constexpr std::array LETTERS_4 = { CLOSE, TO_SYMBOLS, TO_ACCENTS, SPACE, LEFT, RIGHT, DONE };

        constexpr std::array
            SYMBOLS_0 = { symbol("1"), symbol("2"), symbol("3"), symbol("4"), symbol("5"), symbol("6"), symbol("7"), symbol("8"), symbol("9"), symbol("0"), BACKSPACE };
        constexpr std::array
            SYMBOLS_1 = { symbol("!"), symbol("@"), symbol("#"), symbol("$"), symbol("%"), symbol("^"), symbol("&"), symbol("*"), symbol("("), symbol(")"), CLEAR };
        constexpr std::array
            SYMBOLS_2 = { symbol("-"), symbol("_"), symbol("="), symbol("+"), symbol("["), symbol("]"), symbol("{"), symbol("}"), symbol("\\"), symbol("|"), symbol("~") };
        constexpr std::array
            SYMBOLS_3 = { symbol(";"), symbol(":"), symbol("'"), symbol("\""), symbol(","), symbol("."), symbol("<"), symbol(">"), symbol("/"), symbol("?"), symbol("`") };
        constexpr std::array SYMBOLS_4 = { CLOSE, TO_LETTERS, TO_ACCENTS, SPACE, LEFT, RIGHT, DONE };

        // The accented letters of Latin-1 and the common ones beside it, all in the framework's Roboto, with their
        // capitals, and the marks and signs of Latin-1 that a US keyboard does not have. Escaped, as the source is
        // read in the system's code page.
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
            BACKSPACE };
        constexpr std::array ACCENTS_1 = { letter("\xC3\xA9", "\xC3\x89"),
            letter("\xC3\xA8", "\xC3\x88"),
            letter("\xC3\xAA", "\xC3\x8A"),
            letter("\xC3\xAB", "\xC3\x8B"),
            letter("\xC3\xAD", "\xC3\x8D"),
            letter("\xC3\xAC", "\xC3\x8C"),
            letter("\xC3\xAE", "\xC3\x8E"),
            letter("\xC3\xAF", "\xC3\x8F"),
            letter("\xC3\xBD", "\xC3\x9D"),
            letter("\xC3\xBF", "\xC5\xB8"),
            CLEAR };
        constexpr std::array ACCENTS_2 = { SHIFT,
            letter("\xC3\xB3", "\xC3\x93"),
            letter("\xC3\xB2", "\xC3\x92"),
            letter("\xC3\xB4", "\xC3\x94"),
            letter("\xC3\xB6", "\xC3\x96"),
            letter("\xC3\xB5", "\xC3\x95"),
            letter("\xC3\xB8", "\xC3\x98"),
            letter("\xC5\x93", "\xC5\x92"),
            letter("\xC3\xBA", "\xC3\x9A"),
            letter("\xC3\xB9", "\xC3\x99"),
            letter("\xC3\xBB", "\xC3\x9B") };
        constexpr std::array ACCENTS_3 = { letter("\xC3\xBC", "\xC3\x9C"),
            symbol("\xC2\xBF"),
            symbol("\xC2\xA1"),
            symbol("\xC2\xAB"),
            symbol("\xC2\xBB"),
            symbol("\xE2\x82\xAC"),
            symbol("\xC2\xA3"),
            symbol("\xC2\xA5"),
            symbol("\xC2\xB0"),
            symbol("\xC2\xA7") };
        constexpr std::array ACCENTS_4 = { CLOSE, TO_LETTERS, TO_SYMBOLS, SPACE, LEFT, RIGHT, DONE };

        constexpr std::array<std::span<const KeyboardKey>, 5> LETTERS = { LETTERS_0, LETTERS_1, LETTERS_2, LETTERS_3, LETTERS_4 };
        constexpr std::array<std::span<const KeyboardKey>, 5> SYMBOLS = { SYMBOLS_0, SYMBOLS_1, SYMBOLS_2, SYMBOLS_3, SYMBOLS_4 };
        constexpr std::array<std::span<const KeyboardKey>, 5> ACCENTS = { ACCENTS_0, ACCENTS_1, ACCENTS_2, ACCENTS_3, ACCENTS_4 };

        float rowUnits(const std::span<const KeyboardKey> row)
        {
            return std::accumulate(row.begin(), row.end(), 0.0f, [](const float sum, const KeyboardKey& key) {
                return sum + key.units;
            });
        }
    }

    /**
     * The rows of a page, from the top.
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
     * space lets go of Shift. Clear selects the whole text and then deletes it; Done is Enter and Close is Escape,
     * which leave a single-line field with its text or the text it had.
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
        case KeyAction::Done:
            return { KeyStroke{ .keys = { EditKey::Enter } } };
        case KeyAction::Close:
            return { KeyStroke{ .keys = { EditKey::Escape } } };
        }
        return {};
    }

    /**
     * Whether a Shift or Caps key is on, to draw it pressed.
     */
    bool KeyboardTyping::isOn(const KeyboardKey& key) const
    {
        return (key.action == KeyAction::Shift && _shift) || (key.action == KeyAction::Caps && _caps);
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
     * Back to the letters, with Shift and Caps off, for the next field.
     */
    void KeyboardTyping::reset()
    {
        *this = {};
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
