#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "Controls.h"

// The text input of the controls: ImGui's field in a box, typed on the framework's keyboard.
namespace f4cf::imgui::controls
{
    /**
     * The characters a text input takes: any, or those of a whole number (digits and a minus), or of any number (a
     * point as well).
     */
    enum class TextChars : std::uint8_t
    {
        Any,
        Integer,
        Decimal,
    };

    /**
     * What a text input takes, and when its text is written.
     */
    struct TextOptions
    {
        TextChars chars = TextChars::Any;

        // the most characters of UTF-8 it takes, with their count at the right of its box; 0 for no most
        int maxLength = 0;

        // each change is written as it is typed, as a search's query, and not only as the field is left
        bool live = false;
    };

    // the room between the box of a text input and its text
    constexpr float TEXT_INPUT_PADDING = 16.0f;

    /**
     * How a text input that is edited looks: in a control's frame with an accent outline, or bare in a box its caller
     * draws, and the room between its box and its text.
     */
    struct TextLook
    {
        bool framed = true;
        float padding = TEXT_INPUT_PADDING;
    };

    /**
     * What an edit of a text input gave in this frame: a text to write, and whether the hand gets its pulse.
     */
    struct TextEdit
    {
        std::optional<std::string> write;
        bool pulse = false;
    };

    TextEdit textEdit(ImGuiID id, const ImRect& box, bool pressed, std::string_view text, const TextOptions& options = {}, const TextLook& look = {});
    void drawTextInput(const ImRect& box, std::string_view text, Tone tone);
}
