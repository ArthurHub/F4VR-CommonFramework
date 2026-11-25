#pragma once

#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "Controls.h"

// The slider of the controls: a track between two step arrows, and the box of its value.
namespace f4cf::imgui::controls
{
    /**
     * The values a slider has: from its least to its most, on its steps from the least. A step of 0 is any value of the
     * range, which an arrow and the thumbstick step by a hundredth of it.
     */
    struct SliderRange
    {
        float min = 0.0f;
        float max = 1.0f;
        float step = 0.0f;
    };

    /**
     * What the row a slider is in tells of the pointer in this frame. The row is the item the trigger presses; the
     * slider reads from the pointer's place whether a press is on its track or an arrow.
     */
    struct SliderRow
    {
        // the ray is on the row
        bool pointed = false;

        // the trigger was pulled on the row in this frame, and is held since a pull on it
        bool started = false;
        bool held = false;
    };

    /**
     * The widths of a slider: its track with its arrows, and the box of its value.
     */
    struct SliderLayout
    {
        float trackWidth = 280.0f;
        float valueWidth = 104.0f;
    };

    /**
     * What an edit of a slider gave in this frame: the value it shows, whether that is a change to write, and whether
     * the hand gets its pulse.
     */
    struct SliderEdit
    {
        float value = 0.0f;
        bool write = false;
        bool pulse = false;
    };

    /**
     * The text of a value as the box of a slider's value shows it, its font size, and whether it has its unit.
     */
    struct ValueText
    {
        std::string text;
        float fontSize = 0.0f;
        bool withUnit = true;
    };

    // the room between the box of a slider's value and its text
    constexpr float SLIDER_VALUE_PADDING = 12.0f;

    // The thumbstick steps a slider from this far off its center, of 1 for a full push. It is far, so a push that
    // scrolls the page and is a little to a side steps nothing.
    constexpr float SLIDER_STEP_PUSH = 0.6f;

    // the seconds the ray is on the row of a slider before the thumbstick steps it, so a ray that crosses it steps nothing
    constexpr float SLIDER_POINTED_DELAY = 0.3f;

    // a press that is held steps again after the delay, then at the interval, and at the fast one once it is held for
    // long: in seconds
    constexpr float STEP_REPEAT_DELAY = 0.4f;
    constexpr float STEP_REPEAT_INTERVAL = 0.12f;
    constexpr float STEP_FAST_AFTER = 1.5f;
    constexpr float STEP_FAST_INTERVAL = 0.03f;

    ValueText fitValueText(std::string withUnit, std::string numberOnly, const SliderLayout& layout = {});
    SliderEdit sliderEdit(ImGuiID id, float right, float top, const SliderRow& row, float value, const SliderRange& range, const SliderLayout& layout = {});
    float drawSlider(ImGuiID id, float right, float top, const SliderRow& row, std::optional<float> value, const SliderRange& range, Tone tone, const ValueText& text,
        const SliderLayout& layout = {});

    namespace internal
    {
        int stepsOfPush(SliderState& state, float push, float seconds);
    }
}
