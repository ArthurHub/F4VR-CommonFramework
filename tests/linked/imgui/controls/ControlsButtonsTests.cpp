#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Buttons.h"
#include "imgui/controls/Dialogs.h"
#include "imgui/controls/Frames.h"
#include "imgui/controls/Text.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    /**
     * An ImGui context with the canvas font and no renderer, whose frames each draw one window over the display, with
     * the mouse where a test puts it.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            // no imgui.ini written where the tests run
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(FONT_SIZE);
        }

        ~TestContext()
        {
            ImGui::DestroyContext();
        }

        /**
         * One frame, with the mouse at a point and its button down or up, that draws the content in the window.
         */
        void frame(const ImVec2& mouse, const bool down, const std::function<void()>& content)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            content();
            ImGui::End();
            ImGui::EndFrame();
        }
    };

    /**
     * Clicks a button in a box, on the frames of a pointing, a pull and a release, and returns on which of them it
     * said it was pressed.
     */
    std::array<bool, 3> click(TestContext& context, const ImRect& box, const bool disabled)
    {
        std::array<bool, 3> pressed{};
        const auto at = box.GetCenter();
        const std::array downs{ false, true, false };
        for (std::size_t i = 0; i < downs.size(); ++i) {
            context.frame(at, downs[i], [&] {
                ImGui::BeginDisabled(disabled);
                pressed[i] = button("Save", box, ButtonLook::Primary);
                ImGui::EndDisabled();
            });
        }
        return pressed;
    }
}

TEST_CASE("Controls buttons: a click presses a button once, as the trigger is released")
{
    TestContext context;
    const auto pressed = click(context, controlBox(100.0f, 100.0f, 120.0f), false);
    CHECK_FALSE(pressed[0]);
    CHECK_FALSE(pressed[1]);
    CHECK(pressed[2]);
}

TEST_CASE("Controls buttons: a disabled button is not pressed")
{
    TestContext context;
    const auto pressed = click(context, controlBox(100.0f, 100.0f, 120.0f), true);
    CHECK_FALSE(pressed[0]);
    CHECK_FALSE(pressed[1]);
    CHECK_FALSE(pressed[2]);
}

TEST_CASE("Controls buttons: a dialog's buttons are from its right, each as wide as its label or the least")
{
    TestContext context;
    const std::array buttons{ DialogButton{ .label = "Save", .look = ButtonLook::Primary },
        DialogButton{ .label = "A much longer label than the least", .look = ButtonLook::Secondary },
        DialogButton{ .label = "Cancel" } };
    std::array<ImRect, 3> boxes{};
    float right = 0.0f;
    float longWidth = 0.0f;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        longWidth = textWidth(buttons[1].label);
        CHECK(dialogButtons(buttons, boxes) == -1);
    });

    CHECK(common::fEqual(boxes[0].Max.x, right));
    CHECK(common::fEqual(boxes[0].GetWidth(), DIALOG_BUTTON_MIN_WIDTH));
    CHECK(common::fEqual(boxes[1].Max.x, boxes[0].Min.x - 8.0f));
    CHECK(boxes[1].GetWidth() > longWidth);
    CHECK(boxes[1].GetWidth() > DIALOG_BUTTON_MIN_WIDTH);
    CHECK(common::fEqual(boxes[2].Max.x, boxes[1].Min.x - 8.0f));
    CHECK(common::fEqual(boxes[2].GetWidth(), DIALOG_BUTTON_MIN_WIDTH));
    CHECK(common::fEqual(boxes[0].GetHeight(), get().style().controlHeight));
}

TEST_CASE("Controls buttons: a switcher ends at the right with its two cells")
{
    TestContext context;
    float left = 0.0f;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        left = drawSwitcher(500.0f, 100.0f, true, Tone::Normal, "No", "Yes");
    });
    CHECK(common::fEqual(left, 500.0f - SWITCHER_CELL_WIDTH * 2.0f));
}
