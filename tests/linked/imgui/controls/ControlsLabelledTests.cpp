#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Labelled.h"
#include "imgui/controls/Targets.h"
#include "vrcf/VRControllersHaptic.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr ImVec2 AWAY(-100.0f, -100.0f);

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw labelled rows in a window over the
     * display, with the controls' frame around them, and record their targets.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(900.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(get().theme().bodyFontSize);
            get().applyImGuiStyle();
            get().text() = {};
            targets().setRecording(true);
        }

        TestContext(const TestContext&) = delete;
        TestContext& operator=(const TestContext&) = delete;

        ~TestContext()
        {
            targets().setRecording(false);
            get().text() = {};
            ImGui::DestroyContext();
        }

        /**
         * A frame of the rows, with the mouse at a point and its button down or up. Keeps whether the last row was
         * ImGui's last item under the ray, and whether a press asked for its pulse.
         */
        void frame(const std::function<void()>& rows, const ImVec2& mouse = AWAY, const bool down = false)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            rows();
            itemHovered = ImGui::IsItemHovered();
            ImGui::End();
            get().endFrame();
            pulsed = get().pulsed();
            ImGui::EndFrame();
        }

        /**
         * A pull and a release of the trigger at a point.
         */
        void press(const std::function<void()>& rows, const ImVec2& at)
        {
            frame(rows, at);
            frame(rows, at, true);
            frame(rows, at);
        }

        bool itemHovered = false;
        bool pulsed = false;
    };

    /**
     * The row of a label in the last frame.
     */
    ImRect rowOf(const std::string_view label)
    {
        for (const auto& kept : targets().targets()) {
            if (kept.kind == target::ROW && kept.key == label) {
                return kept.rect;
            }
        }
        return {};
    }

    /**
     * Where the trigger is pulled on a row: at its left, on its label, away from its control.
     */
    ImVec2 onLabel(const std::string_view label)
    {
        const auto row = rowOf(label);
        return { row.Min.x + 40.0f, row.GetCenter().y };
    }
}

TEST_CASE("Controls labelled: a switcher's press turns it once, with its pulse, and the row is the last item and a target")
{
    TestContext context;
    bool on = false;
    int changes = 0;
    const auto rows = [&] {
        changes += labelledSwitcher("Enabled", on) ? 1 : 0;
    };
    context.frame(rows);
    REQUIRE(rowOf("Enabled").GetWidth() > CONTROL_COLUMN_WIDTH);
    const auto at = onLabel("Enabled");

    context.frame(rows, at);
    context.frame(rows, at);
    CHECK(context.itemHovered);
    context.frame(rows, at, true);
    CHECK_FALSE(on);
    context.frame(rows, at);
    CHECK(on);
    CHECK(changes == 1);
    CHECK(context.pulsed);
    context.frame(rows, at);
    CHECK(changes == 1);
    CHECK_FALSE(context.pulsed);
}

TEST_CASE("Controls labelled: a disabled row changes nothing and asks for no pulse")
{
    TestContext context;
    bool on = false;
    float value = 2.0f;
    std::size_t chosen = 0;
    constexpr std::array<std::string_view, 3> OPTIONS{ "A", "B", "C" };
    const Labelled disabled{ .disabled = true };
    bool changed = false;
    const auto rows = [&] {
        changed = labelledSwitcher("Switcher", on, disabled) || changed;
        changed = labelledSlider("Slider", value, { .min = 0.0f, .max = 10.0f, .step = 1.0f }, {}, disabled) || changed;
        changed = labelledStepper("Stepper", chosen, OPTIONS, disabled) || changed;
        changed = labelledButton("Button", "Run", disabled) || changed;
    };
    context.frame(rows);
    for (const auto* label : { "Switcher", "Slider", "Stepper", "Button" }) {
        context.press(rows, onLabel(label));
        CHECK_FALSE(context.pulsed);
    }
    CHECK_FALSE(changed);
    CHECK_FALSE(on);
    CHECK(common::fEqual(value, 2.0f));
    CHECK(chosen == 0);
}

TEST_CASE("Controls labelled: a slider's drag writes as it is let go, on a step of the range")
{
    TestContext context;
    float value = 0.0f;
    int writes = 0;
    const auto rows = [&] {
        writes += labelledSlider("Distance", value, { .min = 0.0f, .max = 10.0f, .step = 1.0f }, { .decimals = 1, .unit = " m" }) ? 1 : 0;
    };
    context.frame(rows);
    const auto row = rowOf("Distance");
    // the track is in the column at the right of the row, between its arrows, before the value's box
    const float trackLeft = row.Max.x - CONTROL_COLUMN_WIDTH + 40.0f;
    const float trackRight = row.Max.x - 104.0f - get().theme().space2 - 40.0f;
    const float y = row.GetCenter().y;

    context.frame(rows, ImVec2(trackLeft, y));
    context.frame(rows, ImVec2(trackLeft, y), true);
    CHECK(context.pulsed);
    context.frame(rows, ImVec2(trackRight, y), true);
    CHECK(writes == 0);
    CHECK(common::fEqual(value, 0.0f));
    context.frame(rows, ImVec2(trackRight, y));
    CHECK(writes == 1);
    CHECK(value > 5.0f);
    CHECK(common::fEqual(value, std::round(value)));
}

TEST_CASE("Controls labelled: a stepper's press on its row goes to the next option, and from the last to the first")
{
    TestContext context;
    std::size_t chosen = 1;
    constexpr std::array<std::string_view, 2> OPTIONS{ "Low", "High" };
    int changes = 0;
    const auto rows = [&] {
        changes += labelledStepper("Quality", chosen, OPTIONS) ? 1 : 0;
    };
    context.frame(rows);
    context.press(rows, onLabel("Quality"));
    CHECK(chosen == 0);
    CHECK(changes == 1);
    CHECK(context.pulsed);
}

TEST_CASE("Controls labelled: a dropdown's press opens its list, and a press on another option picks it")
{
    TestContext context;
    std::size_t chosen = 0;
    constexpr std::array<std::string_view, 3> OPTIONS{ "Red", "Green", "Blue" };
    int changes = 0;
    const auto rows = [&] {
        changes += labelledDropdown("Color", chosen, OPTIONS) ? 1 : 0;
    };
    context.frame(rows);
    context.press(rows, onLabel("Color"));
    CHECK(context.pulsed);
    CHECK(get().isListOpen());

    context.frame(rows);
    ImRect blue;
    for (const auto& kept : targets().targets()) {
        if (kept.kind == target::OPTION && kept.key == "Blue") {
            blue = kept.rect;
        }
    }
    REQUIRE(blue.GetWidth() > 0.0f);
    context.press(rows, blue.GetCenter());
    CHECK(chosen == 2);
    CHECK(changes == 1);
}

TEST_CASE("Controls labelled: a text's press starts its edit, and the text is written once as Enter ends it")
{
    TestContext context;
    std::string text = "old";
    int changes = 0;
    const auto rows = [&] {
        changes += labelledText("Name", text) ? 1 : 0;
    };
    context.frame(rows);
    context.press(rows, onLabel("Name"));
    CHECK(context.pulsed);
    context.frame(rows);
    ImGui::GetIO().AddInputCharactersUTF8(" and new");
    context.frame(rows);
    CHECK(changes == 0);
    for (const bool down : { true, false }) {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, down);
        context.frame(rows);
    }
    CHECK(text == "old and new");
    CHECK(changes == 1);
}

TEST_CASE("Controls labelled: a button's press on its row returns true once, and a value is a row with no press")
{
    TestContext context;
    int presses = 0;
    const auto rows = [&] {
        presses += labelledButton("Reload", "Run", {}, Icon::Refresh) ? 1 : 0;
        labelledValue("Version", "1.2.3");
    };
    context.frame(rows);
    context.press(rows, onLabel("Reload"));
    CHECK(presses == 1);
    CHECK(context.pulsed);
    context.press(rows, onLabel("Version"));
    CHECK_FALSE(context.pulsed);
    CHECK(rowOf("Version").GetWidth() > 0.0f);
}

TEST_CASE("Controls labelled: the press pulse is the lightest pattern until a mod sets another or none")
{
    REQUIRE(get().pressPulse() == vrcf::HapticPattern::Tick);
    get().setPressPulse(std::nullopt);
    CHECK_FALSE(get().pressPulse().has_value());
    get().setPressPulse(vrcf::HapticPattern::Tick);
}
