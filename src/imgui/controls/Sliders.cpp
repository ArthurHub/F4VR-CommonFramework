#include "Sliders.h"

#include <algorithm>
#include <cmath>

#include "../../common/CommonUtils.h"
#include "Frames.h"
#include "Marks.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // the sizes of a slider's parts
    constexpr float FILL_HEIGHT = 8.0f;
    constexpr float GRAB_WIDTH = 20.0f;
    constexpr float GRAB_HEIGHT = 40.0f;
    constexpr float GRAB_ROUNDING = 2.0f;
    constexpr float STEP_ARROW_WIDTH = 24.0f;

    // the gap between an arrow and the track, which presses the arrow, and between the track and the box of the value
    constexpr float ARROW_GAP = 4.0f;
    constexpr float VALUE_GAP = 8.0f;

    // the triangle of a step arrow is 10 pixels, of the 14 of a stepper's
    constexpr float STEP_ARROW_SCALE = 10.0f / ARROW_SIZE;

    // the steps a slider with no step of its own has from its least to its most
    constexpr float STEPS_WITH_NO_STEP = 100.0f;

    /**
     * Where the parts of a slider that ends at the right are: its track between its step arrows, the box of its value
     * after them, and the two ends the middle of its grab moves between, so the grab is whole inside the track at both.
     */
    struct Boxes
    {
        ImRect previous;
        ImRect track;
        ImRect next;
        ImRect valueBox;
        float grabLeast = 0.0f;
        float grabMost = 0.0f;

        Boxes(const float right, const float top, const SliderLayout& layout)
            : previous(controlBox(right - layout.valueWidth - VALUE_GAP - layout.trackWidth, top, STEP_ARROW_WIDTH)),
              track(controlBox(previous.Max.x + ARROW_GAP, top, layout.trackWidth - (STEP_ARROW_WIDTH + ARROW_GAP) * 2.0f)),
              next(controlBox(track.Max.x + ARROW_GAP, top, STEP_ARROW_WIDTH)),
              valueBox(controlBox(right - layout.valueWidth, top, layout.valueWidth)),
              grabLeast(track.Min.x + get().theme().frameBorderSize + GRAB_WIDTH * 0.5f),
              grabMost(track.Max.x - get().theme().frameBorderSize - GRAB_WIDTH * 0.5f)
        {}

        /**
         * The side of the step arrow at a place of the row, as -1 or 1, and 0 for none. An arrow is pressed a little
         * beside it too, and in the gap between it and the track.
         */
        int arrowAt(const float x) const
        {
            if (x >= previous.Min.x - ARROW_GAP && x < track.Min.x) {
                return -1;
            }
            return x >= track.Max.x && x < next.Max.x + ARROW_GAP ? 1 : 0;
        }

        /**
         * The middle of the grab at a value of the slider. A value outside the range is at its end.
         */
        float grabAt(const SliderRange& range, const float value) const
        {
            const float fraction = range.max > range.min ? std::clamp((value - range.min) / (range.max - range.min), 0.0f, 1.0f) : 0.0f;
            return std::lerp(grabLeast, grabMost, fraction);
        }

        /**
         * The value of the slider with the middle of its grab at a place, in its range and not on a step.
         */
        float valueAt(const SliderRange& range, const float grab) const
        {
            return std::lerp(range.min, range.max, std::clamp((grab - grabLeast) / (grabMost - grabLeast), 0.0f, 1.0f));
        }
    };

    /**
     * A value of a slider on the step that is nearest to it, counted from the least, and in the range. A slider with no
     * step has any value of its range.
     */
    float onStep(const SliderRange& range, const float value)
    {
        double stepped = value;
        if (range.step > 0.0f) {
            stepped = range.min + std::round((static_cast<double>(value) - range.min) / range.step) * range.step;
        }
        return std::clamp(static_cast<float>(stepped), range.min, range.max);
    }

    /**
     * The edit of a slider a number of steps from its value: a change is to be written, and gives the hand its pulse.
     */
    SliderEdit stepBy(const SliderRange& range, const float value, const int steps)
    {
        const float step = range.step > 0.0f ? range.step : (range.max - range.min) / STEPS_WITH_NO_STEP;
        const float stepped = onStep(range, value + static_cast<float>(steps) * step);
        const bool write = f4cf::common::fNotEqual(stepped, value);
        return { .value = stepped, .write = write, .pulse = write };
    }
}

namespace f4cf::imgui::controls
{
    /**
     * Whether a press that is held steps in this frame: as it begins, and again after a delay and then faster.
     */
    bool StepRepeat::step(const float seconds)
    {
        if (!begun) {
            begun = true;
            nextStepSeconds = STEP_REPEAT_DELAY;
            return true;
        }
        heldSeconds += seconds;
        if (heldSeconds < nextStepSeconds) {
            return false;
        }
        nextStepSeconds += heldSeconds < STEP_FAST_AFTER ? STEP_REPEAT_INTERVAL : STEP_FAST_INTERVAL;
        return true;
    }

    /**
     * The text of a value in the box of a slider's value: with its unit in the current size where it fits, else in the
     * detail size, else the number alone, so the caller can say the unit elsewhere, in the size it fits in. A number is
     * never shortened.
     */
    ValueText fitValueText(std::string withUnit, std::string numberOnly, const SliderLayout& layout)
    {
        const float room = layout.valueWidth - SLIDER_VALUE_PADDING * 2.0f;
        const float detailSize = get().theme().detailFontSize;
        const auto widthAt = [](const std::string& text, const float size) {
            ImGui::PushFont(nullptr, size);
            const float width = textWidth(text);
            ImGui::PopFont();
            return width;
        };
        if (textWidth(withUnit) <= room) {
            return { .text = std::move(withUnit), .fontSize = ImGui::GetFontSize(), .withUnit = true };
        }
        if (widthAt(withUnit, detailSize) <= room) {
            return { .text = std::move(withUnit), .fontSize = detailSize, .withUnit = true };
        }
        const bool same = numberOnly == withUnit;
        const float size = textWidth(numberOnly) <= room ? ImGui::GetFontSize() : detailSize;
        return { .text = std::move(numberOnly), .fontSize = size, .withUnit = same };
    }

    /**
     * The value a slider that ends at the right shows in this frame: the one the player gives it, and its own where they
     * give none. A pull of the trigger on the track takes hold of the grab, which follows the ray until the trigger is let
     * go, also beside the track. A pull on the grab itself does not move it. The value of a drag is to be written as the
     * trigger is let go, and not before: a store can write a file for each. After Back the drag gives nothing.
     * A pull on a step arrow steps the slider, and again while it is held, until the ray leaves the arrow. The thumbstick
     * steps the slider whose row the ray is on, once it is there for a short time. Each step is to be written.
     * The hand gets its pulse as the pull takes hold of the slider, or a step changes it.
     */
    SliderEdit sliderEdit(const ImGuiID id, const float right, const float top, const SliderRow& row, const float value, const SliderRange& range, const SliderLayout& layout)
    {
        auto& state = get().sliders();
        const auto& io = ImGui::GetIO();
        const Boxes boxes(right, top, layout);
        SliderEdit edit{ .value = value };
        if (row.started && boxes.track.Contains(io.MousePos)) {
            const float grab = boxes.grabAt(range, value);
            state.dragged = id;
            state.dragValue = value;
            state.grabOffset = std::abs(io.MousePos.x - grab) <= GRAB_WIDTH * 0.5f ? io.MousePos.x - grab : 0.0f;
            state.dragCancelled = false;
            edit.pulse = true;
        }
        const int arrow = boxes.arrowAt(io.MousePos.x);
        if (row.started && arrow != 0) {
            state.arrowHeld = id;
            state.arrowSide = arrow;
            state.arrowRepeat = {};
        }
        // by the frame before, as the steps of this frame are
        const bool wasPointed = row.pointed && state.pointed == id;
        if (row.pointed) {
            state.pointedSeen = true;
            if (state.pointed == id) {
                state.pointedSeconds += io.DeltaTime;
            } else {
                // a push that is held as the ray comes here is not for this slider
                state.pointed = id;
                state.pointedSeconds = 0.0f;
                state.pushIgnored = state.pushSide != 0;
            }
        }
        if (state.dragged == id) {
            if (state.dragCancelled) {
                state.dragHeld = row.held;
                return edit;
            }
            if (!row.held) {
                edit.write = common::fNotEqual(state.dragValue, value);
                edit.value = state.dragValue;
                return edit;
            }
            state.dragHeld = true;
            state.dragValue = onStep(range, boxes.valueAt(range, io.MousePos.x - state.grabOffset));
            edit.value = state.dragValue;
            return edit;
        }
        if (state.arrowHeld == id && (!row.held || arrow != state.arrowSide)) {
            state.arrowHeld = 0;
        }
        if (state.arrowHeld == id) {
            if (state.arrowRepeat.step(io.DeltaTime)) {
                const auto stepped = stepBy(range, value, state.arrowSide);
                return { .value = stepped.value, .write = stepped.write, .pulse = edit.pulse || stepped.pulse };
            }
            return edit;
        }
        if (wasPointed && state.steps != 0) {
            const auto stepped = stepBy(range, value, state.steps);
            return { .value = stepped.value, .write = stepped.write, .pulse = edit.pulse || stepped.pulse };
        }
        return edit;
    }

    /**
     * A slider that ends at the right: its track between its step arrows, with a fill from the least to the grab, and the
     * box of its value with its text, in its tone's color. While it is dragged the grab and the outline of the track glow.
     * A slider with no value has no fill, no grab and an empty box. The arrow of the end the value is at is disabled, and
     * so are both with no value. Returns its left edge.
     */
    float drawSlider(const ImGuiID id, const float right, const float top, const SliderRow& row, const std::optional<float> value, const SliderRange& range, const Tone tone,
        const ValueText& text, const SliderLayout& layout)
    {
        const auto& state = get().sliders();
        const auto& colors = get().colors();
        const Boxes boxes(right, top, layout);
        const auto& track = boxes.track;
        const bool dragged = state.dragged == id && state.dragHeld && !state.dragCancelled;
        const int pointedArrow = row.pointed && !dragged ? boxes.arrowAt(ImGui::GetIO().MousePos.x) : 0;
        const int heldArrow = state.arrowHeld == id ? state.arrowSide : 0;
        drawStepButton(boxes.previous, Arrow::Left, value && *value > range.min, pointedArrow < 0, heldArrow < 0, STEP_ARROW_SCALE);
        drawStepButton(boxes.next, Arrow::Right, value && *value < range.max, pointedArrow > 0, heldArrow > 0, STEP_ARROW_SCALE);
        drawFrame(track);
        if (dragged) {
            drawOutline(track.Min, track.Max, colors.glow);
        }
        drawFrame(boxes.valueBox, tone);
        if (!value) {
            return boxes.previous.Min.x;
        }

        const float grab = boxes.grabAt(range, *value);
        const float middle = track.GetCenter().y;
        const auto toneColor = get().tone(tone).color;
        const auto color = ImGui::GetColorU32(toneColor);
        auto* drawList = ImGui::GetWindowDrawList();
        if (grab > boxes.grabLeast) {
            drawList->AddRectFilled(ImVec2(boxes.grabLeast, middle - FILL_HEIGHT * 0.5f), ImVec2(grab, middle + FILL_HEIGHT * 0.5f), color, GRAB_ROUNDING);
        }
        drawList->AddRectFilled(ImVec2(grab - GRAB_WIDTH * 0.5f, middle - GRAB_HEIGHT * 0.5f),
            ImVec2(grab + GRAB_WIDTH * 0.5f, middle + GRAB_HEIGHT * 0.5f),
            dragged ? ImGui::GetColorU32(colors.glow) : color,
            GRAB_ROUNDING);

        ImGui::PushFont(nullptr, text.fontSize);
        drawTextIn(boxes.valueBox, text.text, toneColor, 1.0f, SLIDER_VALUE_PADDING);
        ImGui::PopFont();
        return boxes.previous.Min.x;
    }

    namespace internal
    {
        /**
         * The steps the thumbstick moves the slider under the ray by in this frame: one to the side it is pushed to, and
         * one again while it is held, after a delay and then faster. None until the ray is on the row of the slider for a
         * short time: a push in that time steps as the time is over. A push that began with the ray on no slider, or on
         * another, steps nothing until the thumbstick is let go.
         */
        int stepsOfPush(SliderState& state, const float push, const float seconds)
        {
            const int side = push >= SLIDER_STEP_PUSH ? 1 : push <= -SLIDER_STEP_PUSH ? -1 : 0;
            if (side != state.pushSide) {
                state.pushSide = side;
                state.pushRepeat = {};
                state.pushIgnored = side != 0 && !state.pointed;
            }
            if (side == 0 || state.pushIgnored) {
                return 0;
            }
            if (!state.pointed) {
                state.pushIgnored = true;
                return 0;
            }
            if (state.pointedSeconds < SLIDER_POINTED_DELAY) {
                return 0;
            }
            return state.pushRepeat.step(seconds) ? side : 0;
        }
    }
}
