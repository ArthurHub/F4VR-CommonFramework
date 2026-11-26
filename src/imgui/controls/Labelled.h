#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>

#include "Controls.h"
#include "Icons.h"
#include "Marks.h"
#include "Sliders.h"
#include "TextInputs.h"
#include "TextRange.h"

// The controls with their label in a row: one call adds the row, edits the value and draws both, for a plain page.
namespace f4cf::imgui::controls
{
    // the room of a control at the right of its row
    constexpr float CONTROL_COLUMN_WIDTH = 392.0f;

    /**
     * How a labelled row looks: a mark after its label, the tone of its value, disabled, and the ranges of its label a
     * search found, which are underlined.
     */
    struct Labelled
    {
        Mark mark = Mark::None;
        Tone tone = Tone::Normal;
        bool disabled = false;
        std::span<const TextRange> found;
    };

    /**
     * How a labelled slider says its value: with this many decimals, and its unit after the number as it is given, as
     * "%" or " cm".
     */
    struct SliderText
    {
        int decimals = 0;
        std::string_view unit;
    };

    bool labelledSwitcher(std::string_view label, bool& on, const Labelled& row = {});
    bool labelledSlider(std::string_view label, float& value, const SliderRange& range, const SliderText& text = {}, const Labelled& row = {});
    bool labelledStepper(std::string_view label, std::size_t& chosen, std::span<const std::string_view> options, const Labelled& row = {});
    bool labelledDropdown(std::string_view label, std::size_t& chosen, std::span<const std::string_view> options, const Labelled& row = {});
    bool labelledText(std::string_view label, std::string& text, const TextOptions& options = {}, const Labelled& row = {});
    bool labelledButton(std::string_view label, std::string_view buttonText, const Labelled& row = {}, Icon icon = Icon::None);
    void labelledValue(std::string_view label, std::string_view value, const Labelled& row = {});
    float drawRowLabel(const ImVec2& pos, float right, std::string_view text, Mark mark = Mark::None, std::span<const TextRange> found = {});
}
