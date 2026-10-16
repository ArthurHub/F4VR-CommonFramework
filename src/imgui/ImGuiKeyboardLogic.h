#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
}
