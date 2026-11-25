#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Choices.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    // a stepper that ends at 500, with the default layout: its button to the option before is from 108 to 156, its
    // option from 164 to 444, and its button to the option after from 452 to 500
    constexpr float RIGHT = 500.0f;
    constexpr float PREVIOUS = 130.0f;
    constexpr float OPTION = 300.0f;
    constexpr float NEXT = 476.0f;
    constexpr std::size_t COUNT = 3;

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw one window over the display with the
     * controls' frame around them, as the framework's layer begins and ends it, and the mouse where a test puts it.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(FONT_SIZE);
            get().reset();
            get().setThumbstickSide(0.0f);
        }

        ~TestContext()
        {
            get().reset();
            get().setThumbstickSide(0.0f);
            ImGui::DestroyContext();
        }

        void frame(const ImVec2& mouse, const bool down, const std::function<void()>& content, const float side = 0.0f)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            get().setThumbstickSide(side);
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            content();
            ImGui::End();
            get().endFrame();
            ImGui::EndFrame();
        }
    };

    /**
     * The stepper's pick in a frame with a press at a place of its row, and none for a frame with no press.
     */
    std::optional<std::size_t> press(TestContext& context, const float x, const std::optional<std::size_t> chosen, const bool noneStepsBackToLast = false)
    {
        std::optional<std::size_t> picked;
        context.frame(ImVec2(x, 24.0f), false, [&] {
            picked = stepperEdit(RIGHT, 0.0f, ChoiceRow{ .pointed = true, .held = false, .pressed = true }, chosen, COUNT, noneStepsBackToLast);
        });
        return picked;
    }

    constexpr std::array<std::string_view, 3> TEXTS{ "Low", "Medium", "High" };
}

TEST_CASE("Controls choices: a stepper's buttons go to the option before and after, and stop at the ends")
{
    TestContext context;
    CHECK(press(context, NEXT, 0) == 1u);
    CHECK(press(context, PREVIOUS, 1) == 0u);
    CHECK_FALSE(press(context, NEXT, 2).has_value());
    CHECK_FALSE(press(context, PREVIOUS, 0).has_value());
}

TEST_CASE("Controls choices: a press elsewhere on a stepper's row goes to the option after, from the last to the first")
{
    TestContext context;
    CHECK(press(context, OPTION, 1) == 2u);
    CHECK(press(context, OPTION, 2) == 0u);
}

TEST_CASE("Controls choices: with no option chosen, a stepper goes to the first, or back to the last where it is asked to")
{
    TestContext context;
    CHECK(press(context, NEXT, std::nullopt) == 0u);
    CHECK(press(context, OPTION, std::nullopt) == 0u);
    CHECK_FALSE(press(context, PREVIOUS, std::nullopt).has_value());
    CHECK(press(context, PREVIOUS, std::nullopt, true) == 2u);
}

TEST_CASE("Controls choices: the thumbstick steps the stepper under the ray once for each push")
{
    TestContext context;
    const auto pushed = [&context](const float side) {
        std::optional<std::size_t> picked;
        context.frame(
            ImVec2(OPTION, 24.0f),
            false,
            [&] {
                picked = stepperEdit(RIGHT, 0.0f, ChoiceRow{ .pointed = true }, 1, COUNT);
            },
            side);
        return picked;
    };
    CHECK(pushed(1.0f) == 2u);
    CHECK_FALSE(pushed(1.0f).has_value());
    CHECK_FALSE(pushed(0.0f).has_value());
    CHECK(pushed(-1.0f) == 0u);
}

TEST_CASE("Controls choices: a pick in a list returns its option and closes the list")
{
    TestContext context;
    const ImRect box(100.0f, 100.0f, 400.0f, 148.0f);
    ListDrawn drawn;
    const auto list = [&](const bool open) {
        return [&, open] {
            const auto id = ImGui::GetID("list");
            if (open) {
                ImGui::OpenPopupEx(id);
            }
            drawn = drawList(id, box, TEXTS, 0, {}, ListExtras{ .action = "Type your own" });
        };
    };

    context.frame(ImVec2(-100.0f, -100.0f), false, list(true));
    REQUIRE(drawn.shown);
    REQUIRE(drawn.boxes.size() == TEXTS.size() + 1);
    CHECK(drawn.boxes[1].Min.y > drawn.boxes[0].Max.y);
    CHECK(drawn.boxes[3].Min.y > drawn.boxes[2].Max.y);
    CHECK(get().isListOpen());
    // ImGui hides a popup in the frame it appears, so nothing is under the ray until the next
    context.frame(ImVec2(-100.0f, -100.0f), false, list(false));

    const auto at = drawn.boxes[1].GetCenter();
    context.frame(at, false, list(false));
    CHECK(drawn.pointed == 1u);
    context.frame(at, true, list(false));
    context.frame(at, false, list(false));
    CHECK(drawn.picked == 1u);
    context.frame(at, false, list(false));
    CHECK_FALSE(drawn.shown);
    CHECK_FALSE(get().isListOpen());
}

TEST_CASE("Controls choices: Back closes the list as the next frame begins, and not the popup it is over")
{
    TestContext context;
    const ImRect box(100.0f, 100.0f, 400.0f, 148.0f);
    ListDrawn drawn;
    bool editorOpen = false;
    const auto editor = [&](const bool open) {
        return [&, open] {
            const auto editorId = ImGui::GetID("editor");
            if (open) {
                ImGui::OpenPopupEx(editorId);
            }
            editorOpen = ImGui::BeginPopupEx(editorId, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
            if (!editorOpen) {
                drawn = {};
                return;
            }
            const auto listId = ImGui::GetID("list");
            if (open) {
                ImGui::OpenPopupEx(listId);
            }
            drawn = drawList(listId, box, TEXTS, 0);
            ImGui::EndPopup();
        };
    };

    context.frame(ImVec2(-100.0f, -100.0f), false, editor(true));
    REQUIRE(drawn.shown);
    CHECK(get().back());
    CHECK_FALSE(get().isListOpen());
    CHECK_FALSE(get().back());

    context.frame(ImVec2(-100.0f, -100.0f), false, editor(false));
    CHECK(editorOpen);
    CHECK_FALSE(drawn.shown);
}
