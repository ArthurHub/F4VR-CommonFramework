#include <catch2/catch_test_macros.hpp>

#include <functional>

#include <imgui.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Sliders.h"
#include "imgui/controls/Text.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    // a slider from 0 to 100 in steps of 1 that ends at 500, with the default layout: its track is from 136 to 360, the
    // grab moves from 148 to 348, and its next arrow is from 364 to 388
    constexpr float RIGHT = 500.0f;
    constexpr float TOP = 0.0f;
    constexpr float MIDDLE = 24.0f;
    constexpr SliderRange RANGE{ .min = 0.0f, .max = 100.0f, .step = 1.0f };
    constexpr ImGuiID ID = 1;
    constexpr float NEXT_ARROW = 376.0f;

    /**
     * An ImGui context with the canvas font and no renderer, whose frames move the controls' state as the framework's
     * layer does, with the mouse and the thumbstick where a test puts them.
     */
    struct TestSliders
    {
        TestSliders()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(FONT_SIZE);
            get().applyImGuiStyle();
            get().reset();
            get().setThumbstickSide(0.0f);
        }

        ~TestSliders()
        {
            get().reset();
            get().setThumbstickSide(0.0f);
            ImGui::DestroyContext();
        }

        /**
         * One frame of a length, with the mouse at a place and the thumbstick pushed to a side, in which a row edits the
         * slider. Returns the edit.
         */
        SliderEdit frame(const float seconds, const float mouseX, const SliderRow& row, const float value, const float side = 0.0f)
        {
            auto& io = ImGui::GetIO();
            io.DeltaTime = seconds;
            io.MousePos = ImVec2(mouseX, MIDDLE);
            get().setThumbstickSide(side);
            get().beginFrame(seconds);
            const auto edit = sliderEdit(ID, RIGHT, TOP, row, value, RANGE);
            get().endFrame();
            return edit;
        }
    };

    constexpr SliderRow PULLED{ .pointed = true, .started = true, .held = true };
    constexpr SliderRow HELD{ .pointed = true, .started = false, .held = true };
    constexpr SliderRow POINTED{ .pointed = true };
    constexpr SliderRow AWAY{};
}

TEST_CASE("Controls sliders: a held press steps at once, again after a delay, and faster once held long")
{
    StepRepeat repeat;
    CHECK(repeat.step(0.0f));
    CHECK_FALSE(repeat.step(0.25f));
    // 0.5 of the 0.4 delay
    CHECK(repeat.step(0.25f));
    // the interval of 0.12 after 0.4
    CHECK_FALSE(repeat.step(0.01f));
    CHECK(repeat.step(0.05f));

    // the steps of a stretch of frames of 0.01 seconds
    const auto stepsIn = [&repeat](const int frames) {
        int steps = 0;
        for (int frame = 0; frame < frames; ++frame) {
            steps += repeat.step(0.01f) ? 1 : 0;
        }
        return steps;
    };
    // 0.6 seconds at the interval of 0.12, to 1.16 held
    const int slow = stepsIn(60);
    CHECK(slow >= 4);
    CHECK(slow <= 6);
    // past 1.5 held, 0.3 seconds at the fast interval of 0.03
    stepsIn(50);
    const int fast = stepsIn(30);
    CHECK(fast >= 9);
    CHECK(fast <= 11);
}

TEST_CASE("Controls sliders: a drag follows the ray and is written as the trigger is let go")
{
    TestSliders sliders;
    // 300 is (300 - 148) / 200 of the way: 76
    auto edit = sliders.frame(0.01f, 300.0f, PULLED, 50.0f);
    CHECK(edit.pulse);
    CHECK_FALSE(edit.write);
    CHECK(common::fEqual(edit.value, 76.0f));
    CHECK(get().isDragging());

    // beside the track, at its end
    edit = sliders.frame(0.01f, 420.0f, HELD, 50.0f);
    CHECK_FALSE(edit.write);
    CHECK(common::fEqual(edit.value, 100.0f));

    edit = sliders.frame(0.01f, 420.0f, AWAY, 50.0f);
    CHECK(edit.write);
    CHECK(common::fEqual(edit.value, 100.0f));
    CHECK_FALSE(get().isDragging());
}

TEST_CASE("Controls sliders: a pull on the grab does not move it")
{
    TestSliders sliders;
    // the grab of 50 is at 248
    const auto edit = sliders.frame(0.01f, 253.0f, PULLED, 50.0f);
    CHECK(common::fEqual(edit.value, 50.0f));
}

TEST_CASE("Controls sliders: Back ends a drag with nothing written")
{
    TestSliders sliders;
    sliders.frame(0.01f, 300.0f, PULLED, 50.0f);
    CHECK(get().back());
    CHECK_FALSE(get().back());
    CHECK_FALSE(get().isDragging());

    auto edit = sliders.frame(0.01f, 340.0f, HELD, 50.0f);
    CHECK_FALSE(edit.write);
    CHECK(common::fEqual(edit.value, 50.0f));
    edit = sliders.frame(0.01f, 340.0f, AWAY, 50.0f);
    CHECK_FALSE(edit.write);
    CHECK(common::fEqual(edit.value, 50.0f));
}

TEST_CASE("Controls sliders: a step arrow steps as it is pulled, and again while it is held")
{
    TestSliders sliders;
    auto edit = sliders.frame(0.01f, NEXT_ARROW, PULLED, 50.0f);
    CHECK(edit.write);
    CHECK(edit.pulse);
    CHECK(common::fEqual(edit.value, 51.0f));

    edit = sliders.frame(0.25f, NEXT_ARROW, HELD, 51.0f);
    CHECK_FALSE(edit.write);
    edit = sliders.frame(0.25f, NEXT_ARROW, HELD, 51.0f);
    CHECK(edit.write);
    CHECK(common::fEqual(edit.value, 52.0f));

    // off the arrow, it stops
    edit = sliders.frame(0.25f, 300.0f, HELD, 52.0f);
    CHECK_FALSE(edit.write);
}

TEST_CASE("Controls sliders: the thumbstick steps the slider under the ray once the ray is there a short time")
{
    TestSliders sliders;
    sliders.frame(0.25f, 300.0f, POINTED, 50.0f);
    sliders.frame(0.25f, 300.0f, POINTED, 50.0f);

    // pointed 0.25 seconds of the 0.3: not yet
    auto edit = sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 1.0f);
    CHECK_FALSE(edit.write);
    edit = sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 1.0f);
    CHECK(edit.write);
    CHECK(edit.pulse);
    CHECK(common::fEqual(edit.value, 51.0f));

    // to the left, at the least, it changes nothing
    sliders.frame(0.25f, 300.0f, POINTED, 0.0f, 0.0f);
    edit = sliders.frame(0.25f, 300.0f, POINTED, 0.0f, -1.0f);
    CHECK_FALSE(edit.write);
    CHECK(common::fEqual(edit.value, 0.0f));
}

TEST_CASE("Controls sliders: a push that began with the ray on no slider steps nothing until it is let go")
{
    TestSliders sliders;
    sliders.frame(0.25f, 300.0f, AWAY, 50.0f, 1.0f);
    sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 1.0f);
    sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 1.0f);
    auto edit = sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 1.0f);
    CHECK_FALSE(edit.write);

    sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 0.0f);
    edit = sliders.frame(0.25f, 300.0f, POINTED, 50.0f, 1.0f);
    CHECK(edit.write);
}

TEST_CASE("Controls sliders: a value's text fits its box with its unit, smaller, or as the number alone")
{
    const TestSliders sliders;
    ImGui::NewFrame();
    const std::string withUnit = "100 m";
    const std::string number = "100";
    const float bodyWidth = textWidth(withUnit);
    ImGui::PushFont(nullptr, get().theme().detailFontSize);
    const float detailWidth = textWidth(withUnit);
    ImGui::PopFont();
    REQUIRE(detailWidth < bodyWidth);
    const auto boxFor = [](const float room) {
        return SliderLayout{ .valueWidth = room + sliderValuePadding() * 2.0f };
    };

    auto text = fitValueText(withUnit, number, boxFor(bodyWidth + 1.0f));
    CHECK(text.text == withUnit);
    CHECK(text.withUnit);
    CHECK(common::fEqual(text.fontSize, FONT_SIZE));

    text = fitValueText(withUnit, number, boxFor((bodyWidth + detailWidth) * 0.5f));
    CHECK(text.text == withUnit);
    CHECK(common::fEqual(text.fontSize, get().theme().detailFontSize));

    text = fitValueText(withUnit, number, boxFor(detailWidth - 1.0f));
    CHECK(text.text == number);
    CHECK_FALSE(text.withUnit);
    ImGui::EndFrame();
}
