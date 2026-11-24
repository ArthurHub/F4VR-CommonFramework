#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The keyboard's logic, apart from the game: plain std only, so it is unit tested.
namespace f4cf::imgui::internal
{
    /**
     * The character SteamVR's keyboard text starts with, before the field's: a no-break space, which is not seen.
     * The keyboard sends no event when its last character is deleted, so with nothing before the field's text,
     * the field would keep its last character.
     */
    inline constexpr std::string_view KEYBOARD_TEXT_FLOOR = "\xC2\xA0";

    /**
     * The field's text in the keyboard's: what follows the floor it starts with, or all of it when the player
     * deleted the floor too.
     */
    inline std::string_view withoutFloor(const std::string_view keyboard)
    {
        return keyboard.starts_with(KEYBOARD_TEXT_FLOOR) ? keyboard.substr(KEYBOARD_TEXT_FLOOR.size()) : keyboard;
    }

    namespace detail
    {
        inline bool isUtf8Continuation(const std::string_view text, const std::size_t at)
        {
            return at < text.size() && (static_cast<std::uint8_t>(text[at]) & 0xC0) == 0x80;
        }
    }

    /**
     * The length in bytes of the start two UTF-8 texts share, ending on a whole character: a character that
     * starts the same in both and then differs is not shared.
     */
    inline std::size_t sharedUtf8Prefix(const std::string_view a, const std::string_view b)
    {
        std::size_t shared = 0;
        while (shared < a.size() && shared < b.size() && a[shared] == b[shared]) {
            ++shared;
        }
        // a continuation byte after the shared bytes, on either side, means they end inside a character
        while (shared > 0 && (detail::isUtf8Continuation(a, shared) || detail::isUtf8Continuation(b, shared))) {
            --shared;
        }
        return shared;
    }

    /**
     * The length in bytes of the end two UTF-8 texts share, starting on a whole character, and at most the given
     * length.
     */
    inline std::size_t sharedUtf8Suffix(const std::string_view a, const std::string_view b, const std::size_t maxLength)
    {
        const std::size_t limit = (std::min)({ maxLength, a.size(), b.size() });
        std::size_t shared = 0;
        while (shared < limit && a[a.size() - 1 - shared] == b[b.size() - 1 - shared]) {
            ++shared;
        }
        // a continuation byte where the shared bytes start, on either side, means they start inside a character
        while (shared > 0 && (detail::isUtf8Continuation(a, a.size() - shared) || detail::isUtf8Continuation(b, b.size() - shared))) {
            --shared;
        }
        return shared;
    }

    /**
     * The one edit that turns a field's text into the keyboard's: the bytes of the field from `from` up to `to`
     * are replaced with `insert`. It is the part between the start and the end the two texts share, so a
     * character changed in the middle is replaced there.
     */
    struct TextEdit
    {
        std::size_t from = 0;
        std::size_t to = 0;
        std::string_view insert;

        bool empty() const
        {
            return from == to && insert.empty();
        }
    };

    inline TextEdit textEdit(const std::string_view field, const std::string_view keyboard)
    {
        const std::size_t prefix = sharedUtf8Prefix(field, keyboard);
        // the end shared is after the start shared, in both: in "aa" to "a" only one "a" is shared
        const std::size_t suffix = sharedUtf8Suffix(field, keyboard, (std::min)(field.size(), keyboard.size()) - prefix);
        return { .from = prefix, .to = field.size() - suffix, .insert = keyboard.substr(prefix, keyboard.size() - suffix - prefix) };
    }

    // --- the framework's VR keyboard --------------------------------------------------------------------

    /**
     * A page of the keyboard: the letters with the digits above them, the symbols, and the accented letters.
     */
    enum class KeyboardPage : std::uint8_t
    {
        Letters,
        Symbols,
        Accents,
    };

    /**
     * What a key does when pressed.
     */
    enum class KeyAction : std::uint8_t
    {
        // types its label, or its shifted label
        Character,
        Space,
        Backspace,
        // deletes the field's whole text
        Clear,
        // moves the caret a character
        Left,
        Right,
        // the next character is shifted
        Shift,
        // every letter is shifted until it is pressed again
        Caps,
        // shows another page
        ToPage,
        // leaves the field with its text
        Done,
        // leaves the field with the text it had before it was edited
        Close,
    };

    /**
     * A key of a page: its label and what it types shifted, as UTF-8, its width in key units, and what it does.
     */
    struct KeyboardKey
    {
        std::string_view label;
        std::string_view shifted;
        float units = 1.0f;
        KeyAction action = KeyAction::Character;
        // the page a ToPage key shows
        KeyboardPage page = KeyboardPage::Letters;
        // Caps shifts it as Shift does; Shift alone shifts the others
        bool letter = false;

        // Backspace and the caret keys repeat while held
        bool repeats() const
        {
            return action == KeyAction::Backspace || action == KeyAction::Left || action == KeyAction::Right;
        }
    };

    std::span<const std::span<const KeyboardKey>> keyboardRows(KeyboardPage page);

    /**
     * Where a key is on the keyboard, in the pixels of the area the keys are laid out in, from its top left.
     */
    struct KeyRect
    {
        const KeyboardKey* key = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    std::vector<KeyRect> layoutKeys(KeyboardPage page, float width, float height, float gap);
    const KeyRect* keyAt(std::span<const KeyRect> keys, float x, float y, float gap);

    /**
     * A key ImGui is given as a key event, for the edits a field makes itself. Ctrl is a modifier, held with the
     * key after it.
     */
    enum class EditKey : std::uint8_t
    {
        Backspace,
        Left,
        Right,
        A,
        Enter,
        Escape,
        Ctrl,
    };

    /**
     * What one press gives the field: text, typed as characters, or keys pressed together and then let go.
     */
    struct KeyStroke
    {
        std::string text;
        std::vector<EditKey> keys;

        bool operator==(const KeyStroke&) const = default;
    };

    /**
     * The keyboard's page, Shift and Caps, and what a press of a key types with them.
     */
    class KeyboardTyping
    {
    public:
        std::vector<KeyStroke> press(const KeyboardKey& key);
        bool isOn(const KeyboardKey& key) const;
        std::string_view labelOf(const KeyboardKey& key) const;
        void reset();

        KeyboardPage page() const
        {
            return _page;
        }

        bool shift() const
        {
            return _shift;
        }

        bool caps() const
        {
            return _caps;
        }

    private:
        KeyboardPage _page = KeyboardPage::Letters;
        bool _shift = false;
        bool _caps = false;
    };

    /**
     * The repeat of a held key, for one hand.
     */
    class KeyRepeat
    {
    public:
        // the first repeat waits this long, so a press is one character
        static constexpr float DELAY_SECONDS = 0.5f;
        static constexpr float INTERVAL_SECONDS = 0.05f;

        int update(bool held, float deltaSeconds);

    private:
        bool _held = false;
        float _untilNext = 0.0f;
    };

    /**
     * What ImGui is given in one frame: characters, keys pressed and keys let go.
     */
    struct KeyFrameInput
    {
        std::string text;
        std::vector<EditKey> pressed;
        std::vector<EditKey> released;

        bool empty() const
        {
            return text.empty() && pressed.empty() && released.empty();
        }
    };

    /**
     * The strokes the keyboard has typed, given to ImGui one a frame.
     */
    class KeyStrokeQueue
    {
    public:
        void push(KeyStroke stroke);
        KeyFrameInput next();
        void clear();

        bool empty() const
        {
            return _strokes.empty() && _held.empty();
        }

    private:
        std::deque<KeyStroke> _strokes;
        std::vector<EditKey> _held;
    };

    std::vector<std::size_t> utf8Boundaries(std::string_view text);
    std::size_t nearestBoundary(std::span<const float> boundaryX, float x);
    float scrollToShow(float scroll, float caretX, float viewWidth, float textWidth);
}
