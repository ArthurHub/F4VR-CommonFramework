#pragma once

#include <cstddef>
#include <cstdint>

// Plain std only, so game-free code that finds or lays out text uses these types without ImGui.
namespace f4cf::imgui::controls
{
    /**
     * A range of the bytes of a text, as the words a search matched, which the controls underline.
     */
    struct TextRange
    {
        std::size_t begin = 0;
        std::size_t end = 0;

        bool operator==(const TextRange&) const = default;
    };

    /**
     * Where a line of text is set in its room.
     */
    enum class TextAlign : std::uint8_t
    {
        Left,
        Center,
        Right,
    };
}
