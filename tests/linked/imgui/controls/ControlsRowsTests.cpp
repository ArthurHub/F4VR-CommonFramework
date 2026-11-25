#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <span>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Rows.h"
#include "imgui/controls/Text.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    // where the rows start in the test's window
    const ImVec2 ROWS_AT(100.0f, 100.0f);

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
     * Points at a row at `ROWS_AT`, pulls the trigger on it and lets it go, one frame each, and returns what the row
     * told in each frame. A frame with the row comes first, as ImGui finds the window under the mouse from the frame
     * before.
     */
    std::array<Row, 3> click(TestContext& context, const std::function<Row()>& add)
    {
        std::array<Row, 3> rows{};
        const ImVec2 at(ROWS_AT.x + 20.0f, ROWS_AT.y + 10.0f);
        context.frame(at, false, [&] {
            ImGui::SetCursorScreenPos(ROWS_AT);
            add();
        });
        const std::array downs{ false, true, false };
        for (std::size_t i = 0; i < downs.size(); ++i) {
            context.frame(at, downs[i], [&] {
                ImGui::SetCursorScreenPos(ROWS_AT);
                rows[i] = add();
            });
        }
        return rows;
    }

    /**
     * The width of a row at the top of a child window with a scroll bar, whose content is as high as given, after
     * two frames, as the scroll bar shows from the frame after the content outgrew the window.
     */
    float rowWidthIn(TestContext& context, const float contentHeight)
    {
        float width = 0.0f;
        for (int i = 0; i < 2; ++i) {
            context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
                ImGui::BeginChild("List", ImVec2(400.0f, 200.0f));
                width = rowWidth();
                ImGui::Dummy(ImVec2(10.0f, contentHeight));
                ImGui::EndChild();
            });
        }
        return width;
    }
}

TEST_CASE("Controls rows: a row is as wide whether or not its window's scroll bar shows, and a set-in row ends with the others")
{
    TestContext context;
    const float plain = rowWidthIn(context, 50.0f);
    const float scrolled = rowWidthIn(context, 1000.0f);
    CHECK(common::fEqual(plain, scrolled));

    float setIn = 0.0f;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        ImGui::BeginChild("List", ImVec2(400.0f, 200.0f));
        ImGui::Indent(16.0f);
        setIn = rowWidth();
        ImGui::Unindent(16.0f);
        ImGui::EndChild();
    });
    CHECK(common::fEqual(setIn, plain - 16.0f));
}

TEST_CASE("Controls rows: a pressable row is started and held as the trigger is pulled on it, and pressed as it is let go")
{
    TestContext context;
    const auto rows = click(context, [] {
        return addRow(true);
    });
    CHECK(rows[0].shown);
    CHECK(rows[0].pointed);
    CHECK_FALSE(rows[0].started);
    CHECK_FALSE(rows[0].pressed);
    CHECK(rows[1].started);
    CHECK(rows[1].held);
    CHECK_FALSE(rows[1].pressed);
    CHECK(rows[2].pressed);
    CHECK_FALSE(rows[2].held);
}

TEST_CASE("Controls rows: a row that is not pressable is pointed and never pressed")
{
    TestContext context;
    const auto rows = click(context, [] {
        return addRow(false);
    });
    CHECK(rows[0].pointed);
    CHECK_FALSE(rows[1].started);
    CHECK_FALSE(rows[2].pressed);
}

TEST_CASE("Controls rows: a row scrolled out of its window is not shown")
{
    TestContext context;
    Row row;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        ImGui::BeginChild("List", ImVec2(400.0f, 100.0f));
        ImGui::Dummy(ImVec2(10.0f, 500.0f));
        row = addRow(true);
        ImGui::EndChild();
    });
    CHECK_FALSE(row.shown);
    CHECK_FALSE(row.pointed);
}

TEST_CASE("Controls rows: a press on a folding row folds it, and another opens it")
{
    TestContext context;
    bool folded = false;
    const auto add = [&folded] {
        return addFoldingRow("Section", folded);
    };
    click(context, add);
    CHECK(folded);
    click(context, add);
    CHECK_FALSE(folded);
}

TEST_CASE("Controls rows: marked counts end at their right edge, each mark and text after the one before")
{
    TestContext context;
    float none = 0.0f;
    float one = 0.0f;
    float two = 0.0f;
    float oneWidth = 0.0f;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        const std::vector<MarkedCount> counts{ { .mark = CountMark::Dot, .text = "2 unsaved", .color = get().colors().warning },
            { .mark = CountMark::Error, .text = "1 invalid", .color = get().colors().error } };
        none = drawMarkedCounts(500.0f, 100.0f, {});
        one = drawMarkedCounts(500.0f, 100.0f, std::span(counts).first(1));
        two = drawMarkedCounts(500.0f, 100.0f, counts);
        oneWidth = DOT_WIDTH + 8.0f + textWidth("2 unsaved");
    });
    CHECK(common::fEqual(none, 500.0f));
    CHECK(common::fEqual(one, std::floor(500.0f - oneWidth)));
    CHECK(two < one);
}

TEST_CASE("Controls rows: a section heading spans to its right edge, as high as its text and its padding")
{
    TestContext context;
    ImRect heading;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        ImGui::SetCursorScreenPos(ROWS_AT);
        sectionHeading("Display", 500.0f);
        heading = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    });
    CHECK(common::fEqual(heading.Min.x, ROWS_AT.x));
    CHECK(common::fEqual(heading.Max.x, 500.0f));
    CHECK(common::fEqual(heading.GetHeight(), FONT_SIZE + 16.0f));
}

TEST_CASE("Controls rows: a section heading given a height is that high")
{
    TestContext context;
    float height = 0.0f;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        ImGui::SetCursorScreenPos(ROWS_AT);
        sectionHeading("Display", 500.0f, {}, 48.0f);
        height = ImGui::GetItemRectSize().y;
    });
    CHECK(common::fEqual(height, 48.0f));
}

TEST_CASE("Controls rows: a folding heading draws its arrow, text and rule, and underlines the ranges found")
{
    TestContext context;
    int plain = 0;
    int found = 0;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        auto* drawList = ImGui::GetWindowDrawList();
        int before = drawList->VtxBuffer.Size;
        drawFoldingHeading("Display", ROWS_AT, 400.0f, true);
        plain = drawList->VtxBuffer.Size - before;
        before = drawList->VtxBuffer.Size;
        const std::array ranges{ TextRange{ .begin = 0, .end = 3 } };
        drawFoldingHeading("Display", ROWS_AT, 400.0f, true, {}, Mark::None, ranges);
        found = drawList->VtxBuffer.Size - before;
    });
    CHECK(plain > 0);
    CHECK(found > plain);
}
