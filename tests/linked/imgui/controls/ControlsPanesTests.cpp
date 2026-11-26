#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Panes.h"
#include "imgui/controls/Targets.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float NAV_WIDTH = 300.0f;
    constexpr float PANE_HEIGHT = 300.0f;
    constexpr float HEADER_HEIGHT = 40.0f;

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw a content in a window over the display,
     * with the controls' frame around them.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(900.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(get().theme().bodyFontSize);
            get().applyImGuiStyle();
        }

        TestContext(const TestContext&) = delete;
        TestContext& operator=(const TestContext&) = delete;

        ~TestContext()
        {
            ImGui::DestroyContext();
        }

        void frame(const std::function<void()>& content) const
        {
            ImGui::NewFrame();
            get().beginFrame(ImGui::GetIO().DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            content();
            ImGui::End();
            get().endFrame();
            ImGui::EndFrame();
        }
    };

    /**
     * What the rows of a navigation pane had: the width left for them, whether a scroll bar showed, and their scroll.
     */
    struct NavRows
    {
        float available = 0.0f;
        bool scrollbar = false;
        float scrollY = 0.0f;
    };

    /**
     * A navigation pane and the content's pane beside it, with a number of rows in each, the nav rows' scroll set by
     * `scrollRows` after they are drawn, and what the nav rows and the content had.
     */
    void drawPanes(const int rows, NavRows& nav, float& contentScroll, const bool showTop = false, const std::optional<float> scrollTo = {},
        const std::optional<float> scrollRows = {})
    {
        if (beginNavPane("nav", NAV_WIDTH, PANE_HEIGHT)) {
            ImGui::Dummy(ImVec2(navRowWidth(NAV_WIDTH), HEADER_HEIGHT));
            if (beginNavRows("rows", NAV_WIDTH, showTop)) {
                nav.scrollY = ImGui::GetScrollY();
                for (int i = 0; i < rows; ++i) {
                    ImGui::Dummy(ImVec2(navRowWidth(NAV_WIDTH), get().theme().controlHeight));
                }
                nav.available = ImGui::GetContentRegionAvail().x;
                nav.scrollbar = ImGui::GetCurrentWindow()->ScrollbarY;
                if (scrollRows) {
                    ImGui::SetScrollY(*scrollRows);
                }
            }
            endNavRows();
        }
        endNavPane();
        ImGui::SameLine(0.0f, get().theme().space4);
        if (beginContentPane("content", PANE_HEIGHT, scrollTo)) {
            contentScroll = ImGui::GetScrollY();
            for (int i = 0; i < rows; ++i) {
                ImGui::Dummy(ImVec2(100.0f, get().theme().controlHeight));
            }
        }
        endContentPane();
    }
}

TEST_CASE("Controls panes: a navigation pane's rows have navRowWidth, with and without a scroll bar")
{
    const TestContext context;
    NavRows few;
    NavRows many;
    float content = 0.0f;
    context.frame([&] {
        drawPanes(2, few, content);
    });
    context.frame([&] {
        drawPanes(2, few, content);
    });
    CHECK_FALSE(few.scrollbar);
    CHECK(few.available >= navRowWidth(NAV_WIDTH));

    context.frame([&] {
        drawPanes(20, many, content);
    });
    context.frame([&] {
        drawPanes(20, many, content);
    });
    CHECK(many.scrollbar);
    CHECK(many.available >= navRowWidth(NAV_WIDTH));

    const auto& theme = get().theme();
    CHECK(common::fEqual(navRowWidth(NAV_WIDTH), NAV_WIDTH - theme.space2 - theme.space1 - theme.scrollbarSize - theme.space1));
}

TEST_CASE("Controls panes: showTop scrolls the rows to their top, and scrollTo sets the content's scroll")
{
    const TestContext context;
    NavRows nav;
    float content = 0.0f;
    context.frame([&] {
        drawPanes(20, nav, content, false, std::nullopt, 120.0f);
    });
    context.frame([&] {
        drawPanes(20, nav, content);
    });
    REQUIRE(nav.scrollY > 0.0f);
    context.frame([&] {
        drawPanes(20, nav, content, true);
    });
    CHECK(common::fEqual(nav.scrollY, 0.0f));

    context.frame([&] {
        drawPanes(20, nav, content, false, 90.0f);
    });
    CHECK(common::fEqual(content, 90.0f));
    context.frame([&] {
        drawPanes(20, nav, content);
    });
    CHECK(common::fEqual(content, 90.0f));
    context.frame([&] {
        drawPanes(20, nav, content, false, 0.0f);
    });
    CHECK(common::fEqual(content, 0.0f));
}

TEST_CASE("Controls panes: each pane records its scroll under its area")
{
    const TestContext context;
    targets().setRecording(true);
    NavRows nav;
    float content = 0.0f;
    context.frame([&] {
        drawPanes(20, nav, content);
    });
    const auto scrolled = [](const std::string_view area) {
        return std::ranges::any_of(targets().scrolls(), [area](const ScrollTarget& scroll) {
            return scroll.area == area;
        });
    };
    CHECK(scrolled(target::TREE_AREA));
    CHECK(scrolled(target::PAGE_AREA));

    context.frame([&] {
        if (beginNavPane("nav", NAV_WIDTH, PANE_HEIGHT)) {
            beginNavRows("rows", NAV_WIDTH);
            endNavRows("categories");
        }
        endNavPane();
    });
    CHECK(scrolled("categories"));
    CHECK_FALSE(scrolled(target::TREE_AREA));
    targets().setRecording(false);
}
