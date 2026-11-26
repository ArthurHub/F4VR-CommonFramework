#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Targets.h"
#include "imgui/controls/Text.h"
#include "imgui/controls/TitleBars.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float BAR_WIDTH = 800.0f;
    constexpr ImVec2 AWAY(-100.0f, -100.0f);

    constexpr std::array BUTTONS{ TitleButton{ .name = "Close", .icon = Icon::Cross },
        TitleButton{ .name = "Minimize", .icon = Icon::Minimize },
        TitleButton{ .name = "Help", .text = "?" } };

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw a title bar at the top of a window as wide
     * as the bar, with the controls' frame around them, and record its targets.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(BAR_WIDTH, 300.0f);
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
         * A frame of the title bar, with the mouse at a point and its button down or up.
         */
        TitleBarDrawn frame(const TitleBar& bar, const ImVec2& mouse = AWAY, const bool down = false) const
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            ImGui::PopStyleVar();
            const auto drawn = titleBar(bar);
            ImGui::End();
            get().endFrame();
            ImGui::EndFrame();
            return drawn;
        }

        /**
         * The width of a crumb whole: its text in the title's size, and `space2` on each side. Measured in a frame of its
         * own, which keeps the last frame's targets.
         */
        float crumbWidth(const std::string_view text) const
        {
            ImGui::NewFrame();
            ImGui::PushFont(nullptr, get().theme().titleFontSize);
            const float width = textWidth(text) + get().theme().space2 * 2.0f;
            ImGui::PopFont();
            ImGui::EndFrame();
            return width;
        }

        /**
         * A press and release of the trigger at a point, and what the frame of the release gave.
         */
        TitleBarDrawn press(const TitleBar& bar, const ImVec2& at) const
        {
            frame(bar, at);
            frame(bar, at, true);
            return frame(bar, at);
        }
    };

    std::vector<Target> targetsOf(const std::string_view kind)
    {
        std::vector<Target> found;
        std::ranges::copy_if(targets().targets(), std::back_inserter(found), [kind](const Target& target) {
            return target.kind == kind;
        });
        return found;
    }

    const Target* crumbTarget(const std::string_view text)
    {
        const auto& kept = targets().targets();
        const auto found = std::ranges::find_if(kept, [text](const Target& target) {
            return target.kind == target::CRUMB && target.key == text;
        });
        return found != kept.end() ? &*found : nullptr;
    }

    /**
     * The left of the leftmost button, which the crumbs end before.
     */
    float buttonsLeft()
    {
        float left = BAR_WIDTH;
        for (const auto& button : targetsOf(target::TITLE_BUTTON)) {
            left = (std::min)(left, button.rect.Min.x);
        }
        return left;
    }
}

TEST_CASE("Controls title bars: crumbs that fit are drawn whole, and the buttons from the right")
{
    const TestContext context;
    const std::array crumbs{ Crumb{ .text = "Menu" }, Crumb{ .text = "Mod" }, Crumb{ .text = "Page", .look = CrumbLook::Current } };
    context.frame({ .crumbs = crumbs, .buttons = BUTTONS });

    const auto buttons = targetsOf(target::TITLE_BUTTON);
    REQUIRE(buttons.size() == 3);
    CHECK(buttons[0].key == "Close");
    CHECK(buttons[0].rect.Min.x > buttons[1].rect.Max.x);
    CHECK(common::fEqual(buttons[0].rect.GetWidth(), get().theme().controlHeight));

    REQUIRE(targetsOf(target::CRUMB).size() == 3);
    for (const auto& crumb : crumbs) {
        const auto* kept = crumbTarget(crumb.text);
        REQUIRE(kept);
        CHECK(common::fEqual(kept->rect.GetWidth(), context.crumbWidth(crumb.text), 0.01f));
    }
    CHECK(crumbTarget("Page")->rect.Max.x <= buttonsLeft());
}

TEST_CASE("Controls title bars: the crumbs between the second and the last become one ellipsis, and the last fits")
{
    const TestContext context;
    const std::array crumbs{ Crumb{ .text = "Menu" },
        Crumb{ .text = "A mod of a long name" },
        Crumb{ .text = "A page of a long name" },
        Crumb{ .text = "Another page of a long name" },
        Crumb{ .text = "The last page of a long name", .look = CrumbLook::Current } };
    context.frame({ .crumbs = crumbs, .buttons = BUTTONS, .suffix = " (filtered)" });
    CHECK(crumbTarget("Menu"));
    CHECK(crumbTarget("A mod of a long name"));
    CHECK_FALSE(crumbTarget("A page of a long name"));
    CHECK_FALSE(crumbTarget("Another page of a long name"));
    const auto* last = crumbTarget("The last page of a long name");
    REQUIRE(last);
    CHECK(last->rect.Max.x <= buttonsLeft());
}

TEST_CASE("Controls title bars: the second shortens to its least width beside a long last, unless it keeps its width")
{
    const TestContext context;
    std::array crumbs{ Crumb{ .text = "Menu" },
        Crumb{ .text = "A mod of a very long name indeed, longer than the bar" },
        Crumb{ .text = "A page of a long name, which takes its room", .look = CrumbLook::Current } };
    context.frame({ .crumbs = crumbs, .buttons = BUTTONS });
    const auto* second = crumbTarget(crumbs[1].text);
    REQUIRE(second);
    CHECK(common::fEqual(second->rect.GetWidth(), get().theme().controlHeight * 2.0f));

    crumbs[1] = Crumb{ .text = "Developer", .shortens = false };
    context.frame({ .crumbs = crumbs, .buttons = BUTTONS });
    const auto* kept = crumbTarget("Developer");
    REQUIRE(kept);
    CHECK(common::fEqual(kept->rect.GetWidth(), context.crumbWidth("Developer"), 0.01f));
}

TEST_CASE("Controls title bars: a press on a link gives its place, and a button's press and the ray on it its place")
{
    const TestContext context;
    const std::array crumbs{ Crumb{ .text = "Menu" }, Crumb{ .text = "Mod" }, Crumb{ .text = "Page", .look = CrumbLook::Current } };
    const TitleBar bar{ .crumbs = crumbs, .buttons = BUTTONS };
    context.frame(bar);
    const auto mod = crumbTarget("Mod")->rect.GetCenter();
    const auto page = crumbTarget("Page")->rect.GetCenter();
    const auto minimize = targetsOf(target::TITLE_BUTTON)[1].rect.GetCenter();

    CHECK(context.press(bar, mod).pressedCrumb == 1u);
    // the current crumb is not pressed
    CHECK_FALSE(context.press(bar, page).pressedCrumb.has_value());
    const auto drawn = context.press(bar, minimize);
    CHECK(drawn.pressedButton == 1u);
    CHECK(drawn.pointedButton == 1u);
}

TEST_CASE("Controls title bars: a button with a highlight text is wider, and the crumbs end before it")
{
    const TestContext context;
    const std::array crumbs{ Crumb{ .text = "Menu", .look = CrumbLook::Selected } };
    const std::array buttons{ TitleButton{ .name = "Help", .text = "?", .highlightText = "Help" } };
    context.frame({ .crumbs = crumbs, .buttons = buttons });
    const auto help = targetsOf(target::TITLE_BUTTON);
    REQUIRE(help.size() == 1);
    CHECK(help[0].rect.GetWidth() > get().theme().controlHeight);
}
