#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>

#include "Marks.h"
#include "TextRange.h"

// The rows of a list that scrolls, and the headings of its sections, plain or folding.
namespace f4cf::imgui::controls
{
    float headingTextInset();

    /**
     * What a row tells of the pointer in the frame that is drawn.
     */
    struct Row
    {
        // false for a row scrolled out of the list: the caller draws nothing of it
        bool shown = false;

        // the ray is on the row
        bool pointed = false;

        // the trigger was pulled on the row in this frame, and is held on it
        bool started = false;
        bool held = false;

        // the trigger was let go on the row in this frame, after a pull on it
        bool pressed = false;
    };

    /**
     * The mark before a count: the error mark, the warning mark, or the dot in the warning color.
     */
    enum class CountMark : std::uint8_t
    {
        Error,
        Warning,
        Dot,
    };

    /**
     * A count with its mark, as `2 invalid` in the error color after the error mark.
     */
    struct MarkedCount
    {
        CountMark mark = CountMark::Error;
        std::string text;
        ImU32 color = 0;
    };

    float rowWidth();
    Row addRow(bool pressable);
    Row addFoldingRow(const char* id, bool& folded);
    float drawMarkedCounts(float right, float top, std::span<const MarkedCount> counts);
    void drawFoldingHeading(std::string_view text, const ImVec2& pos, float width, bool folded, std::span<const MarkedCount> counts = {}, Mark mark = Mark::None,
        std::span<const TextRange> found = {});
    void sectionHeading(std::string_view text, float right, std::span<const TextRange> found = {}, float height = 0.0f);
}
