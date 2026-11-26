#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Footers.h"
#include "imgui/controls/Targets.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float WINDOW_WIDTH = 800.0f;
    constexpr ImVec2 FOOTER_POS(0.0f, 100.0f);
    constexpr float HEIGHT = 72.0f;
    constexpr float BUTTON_WIDTH = 160.0f;
    constexpr ImVec2 AWAY(-100.0f, -100.0f);

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw a footer in a window as wide as the test's,
     * with the controls' frame around them, and record its targets.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(WINDOW_WIDTH, 300.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(get().theme().bodyFontSize);
            get().applyImGuiStyle();
            targets().setRecording(true);
        }

        TestContext(const TestContext&) = delete;
        TestContext& operator=(const TestContext&) = delete;

        ~TestContext()
        {
            targets().setRecording(false);
            ImGui::DestroyContext();
        }

        /**
         * A frame of the footer, with the mouse at a point and its button down or up, and the cursor after it.
         */
        FooterDrawn frame(const std::span<const FooterButton> buttons, const ImVec2& mouse = AWAY, const bool down = false)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            const auto drawn = footer(FOOTER_POS, buttons, HEIGHT, BUTTON_WIDTH);
            after = ImGui::GetCursorScreenPos();
            ImGui::End();
            ImGui::PopStyleVar(2);
            get().endFrame();
            ImGui::EndFrame();
            return drawn;
        }

        /**
         * A press and release of the trigger at a point, and what the frame of the release gave.
         */
        FooterDrawn press(const std::span<const FooterButton> buttons, const ImVec2& at)
        {
            frame(buttons, at);
            frame(buttons, at, true);
            return frame(buttons, at);
        }

        // where the cursor was after the last frame's footer
        ImVec2 after;
    };

    std::vector<Target> buttonTargets()
    {
        std::vector<Target> found;
        std::ranges::copy_if(targets().targets(), std::back_inserter(found), [](const Target& kept) {
            return kept.kind == target::BUTTON;
        });
        return found;
    }

    constexpr std::array BUTTONS{ FooterButton{ .label = "Discard" }, FooterButton{ .label = "Save", .look = ButtonLook::Primary } };
}

TEST_CASE("Controls footers: the buttons at the right in their order, in the middle of the footer, and the room at their left")
{
    TestContext context;
    const auto drawn = context.frame(BUTTONS);
    const auto& theme = get().theme();

    const auto buttons = buttonTargets();
    REQUIRE(buttons.size() == 2);
    CHECK(buttons[0].key == "Discard");
    CHECK(buttons[1].key == "Save");
    CHECK(common::fEqual(buttons[1].rect.Max.x, WINDOW_WIDTH - theme.space2));
    CHECK(common::fEqual(buttons[1].rect.Min.x - buttons[0].rect.Max.x, theme.space3));
    CHECK(common::fEqual(buttons[0].rect.GetWidth(), BUTTON_WIDTH));
    CHECK(common::fEqual(buttons[0].rect.GetCenter().y, FOOTER_POS.y + HEIGHT * 0.5f));

    CHECK(common::fEqual(drawn.left.Min.x, FOOTER_POS.x + theme.space4));
    CHECK(common::fEqual(drawn.left.Max.x, buttons[0].rect.Min.x - theme.space3));
    CHECK(common::fEqual(drawn.left.Min.y, FOOTER_POS.y));
    CHECK(common::fEqual(drawn.left.GetHeight(), HEIGHT));
    CHECK(drawn.pressed == -1);

    // it takes its room
    CHECK(common::fEqual(context.after.y, FOOTER_POS.y + HEIGHT));
}

TEST_CASE("Controls footers: a press gives the button's place, and a disabled button is not pressed")
{
    TestContext context;
    context.frame(BUTTONS);
    const auto buttons = buttonTargets();
    REQUIRE(buttons.size() == 2);
    const auto discard = buttons[0].rect.GetCenter();
    const auto save = buttons[1].rect.GetCenter();

    CHECK(context.press(BUTTONS, save).pressed == 1);
    CHECK(context.press(BUTTONS, discard).pressed == 0);

    const std::array disabled{ FooterButton{ .label = "Discard" }, FooterButton{ .label = "Save", .look = ButtonLook::Primary, .disabled = true } };
    CHECK(context.press(disabled, save).pressed == -1);
    CHECK(context.press(disabled, discard).pressed == 0);
}

TEST_CASE("Controls footers: with no button, the room at the left reaches the right edge")
{
    TestContext context;
    const auto drawn = context.frame({});
    CHECK(buttonTargets().empty());
    CHECK(common::fEqual(drawn.left.Max.x, WINDOW_WIDTH - get().theme().space3));
}
