#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Tree.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    // where the rows start in the test's window, and their width
    const ImVec2 ROWS_AT(100.0f, 100.0f);
    constexpr float ROW_WIDTH = 300.0f;

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
    std::array<TreeRowDrawn, 3> click(TestContext& context, const std::function<TreeRowDrawn()>& add)
    {
        std::array<TreeRowDrawn, 3> rows{};
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
     * How many vertices a row adds to the window's draw list, drawn at `ROWS_AT` with the mouse away from it.
     */
    int verticesOf(TestContext& context, const TreeRow& row)
    {
        int count = 0;
        context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
            auto* drawList = ImGui::GetWindowDrawList();
            const int before = drawList->VtxBuffer.Size;
            ImGui::SetCursorScreenPos(ROWS_AT);
            treeRow(row, ROW_WIDTH);
            count = drawList->VtxBuffer.Size - before;
        });
        return count;
    }
}

TEST_CASE("Controls tree: a row is pointed, and pressed as the trigger is let go on it, and is the last item")
{
    TestContext context;
    ImRect item;
    const auto rows = click(context, [&item] {
        const auto drawn = treeRow({ .name = "Display", .arrow = TreeArrow::Closed }, ROW_WIDTH);
        item = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        return drawn;
    });
    CHECK(rows[0].pointed);
    CHECK_FALSE(rows[0].pressed);
    CHECK_FALSE(rows[1].pressed);
    CHECK(rows[2].pressed);
    CHECK(common::fEqual(item.Min.x, ROWS_AT.x));
    CHECK(common::fEqual(item.GetWidth(), ROW_WIDTH));
    CHECK(common::fEqual(item.GetHeight(), get().style().controlHeight));
}

TEST_CASE("Controls tree: a disabled row is pointed and never pressed")
{
    TestContext context;
    const auto rows = click(context, [] {
        return treeRow({ .name = "Display", .disabled = true }, ROW_WIDTH);
    });
    CHECK(rows[0].pointed);
    CHECK_FALSE(rows[2].pressed);
}

TEST_CASE("Controls tree: a press on a group row is the caller's to open or close it")
{
    TestContext context;
    const auto rows = click(context, [] {
        return treeGroupRow("Weapons", 12, false, false, ROW_WIDTH);
    });
    CHECK(rows[0].pointed);
    CHECK(rows[2].pressed);
}

TEST_CASE("Controls tree: a selected row with show is scrolled to where it is not shown")
{
    TestContext context;
    float scroll = 0.0f;
    for (int i = 0; i < 3; ++i) {
        context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
            ImGui::BeginChild("Rows", ImVec2(ROW_WIDTH, 100.0f));
            scroll = ImGui::GetScrollY();
            ImGui::Dummy(ImVec2(10.0f, 500.0f));
            treeRow({ .name = "Display", .selected = true, .show = true }, ROW_WIDTH);
            ImGui::EndChild();
        });
    }
    CHECK(scroll > 0.0f);
}

TEST_CASE("Controls tree: a row scrolled out of its window draws nothing")
{
    TestContext context;
    int count = -1;
    context.frame(ImVec2(-100.0f, -100.0f), false, [&] {
        ImGui::BeginChild("Rows", ImVec2(ROW_WIDTH, 100.0f));
        ImGui::Dummy(ImVec2(10.0f, 500.0f));
        auto* drawList = ImGui::GetWindowDrawList();
        const int before = drawList->VtxBuffer.Size;
        treeRow({ .name = "Display", .arrow = TreeArrow::Open, .selected = true }, ROW_WIDTH);
        count = drawList->VtxBuffer.Size - before;
        ImGui::EndChild();
    });
    CHECK(count == 0);
}

TEST_CASE("Controls tree: a tag, a count and the dot are drawn after the name")
{
    TestContext context;
    const int plain = verticesOf(context, { .name = "Display" });
    CHECK(plain > 0);
    CHECK(verticesOf(context, { .name = "Display", .tag = "MCM" }) > plain);
    CHECK(verticesOf(context, { .name = "Display", .count = 3 }) > plain);
    CHECK(verticesOf(context, { .name = "Display", .dot = true }) > plain);
}
