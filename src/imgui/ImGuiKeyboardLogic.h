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
    namespace detail
    {
        inline bool isUtf8Continuation(const std::string_view text, const std::size_t at)
        {
            return at < text.size() && (static_cast<std::uint8_t>(text[at]) & 0xC0) == 0x80;
        }
    }

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
        // starts a new line of a multiline field; a one-line field has none
        NewLine,
        // leaves the field with its text
        Done,
        // the line that shows the field's text, along the top: a press on it puts the caret there
        Line,
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
     * The field the keyboard types into, as far as its keys care: a multiline field takes new lines, and with
     * ctrlEnterForNewLine Ctrl+Enter starts one and Enter leaves the field, as ImGui's flag of that name has it.
     */
    struct KeyboardField
    {
        bool multiline = false;
        bool ctrlEnterForNewLine = false;
    };

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
        std::vector<KeyStroke> done() const;
        bool isOn(const KeyboardKey& key) const;
        bool isEnabled(const KeyboardKey& key) const;
        std::string_view labelOf(const KeyboardKey& key) const;
        void reset();

        void setField(const KeyboardField& field)
        {
            _field = field;
        }

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
        KeyboardField _field;
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
