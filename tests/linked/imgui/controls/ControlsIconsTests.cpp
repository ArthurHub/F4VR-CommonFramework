#include <catch2/catch_test_macros.hpp>

#include <cfloat>
#include <format>
#include <functional>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Dialogs.h"
#include "imgui/controls/Icons.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr ImVec2 DISPLAY(800.0f, 600.0f);

    // ImGui's soft edge past a shape, and the miter of a sharp join, as the eye's tips, where two curves meet
    constexpr float FRINGE = 2.0f;

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw a content in a window over the display.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = DISPLAY;
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
            ImGui::SetNextWindowSize(DISPLAY);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            content();
            ImGui::End();
            get().endFrame();
            ImGui::EndFrame();
        }
    };

    /**
     * The box of the vertices an icon adds to the window's draw list.
     */
    ImRect iconVertices(const Icon icon, const ImVec2& center, const float size)
    {
        auto* drawList = ImGui::GetWindowDrawList();
        const int first = drawList->VtxBuffer.Size;
        drawIcon(icon, center, IM_COL32(255, 255, 255, 255), size);
        ImRect drawn(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (int i = first; i < drawList->VtxBuffer.Size; ++i) {
            drawn.Add(drawList->VtxBuffer[i].pos);
        }
        return drawn;
    }

    /**
     * Where a row's popup of a size opens for a row, in a window over the display.
     */
    ImRect rowPopup(const TestContext& context, const ImRect& row, const ImVec2& size)
    {
        ImRect popup;
        const auto draw = [&] {
            const auto id = ImGui::GetID("popup");
            if (!ImGui::IsPopupOpen(id, ImGuiPopupFlags_None)) {
                ImGui::OpenPopupEx(id);
            }
            if (beginRowPopup(id, row, size.x, size.y)) {
                popup = ImGui::GetCurrentWindow()->Rect();
                ImGui::EndPopup();
            }
        };
        context.frame(draw);
        context.frame(draw);
        context.frame([] {
            ImGui::ClosePopupsExceptModals();
        });
        return popup;
    }
}

TEST_CASE("Controls icons: each draws inside its box, at its design box and at another size")
{
    const TestContext context;
    context.frame([] {
        const ImVec2 center(200.0f, 200.0f);
        for (int value = static_cast<int>(Icon::Search); value <= static_cast<int>(Icon::Trash); ++value) {
            const auto icon = static_cast<Icon>(value);
            for (const float size : { iconDesignBox(icon), 36.0f }) {
                INFO(std::format("icon {} at {}", value, size));
                const auto drawn = iconVertices(icon, center, size);
                REQUIRE(drawn.Min.x < drawn.Max.x);
                const float half = size * 0.5f + FRINGE;
                CHECK(drawn.Min.x >= center.x - half);
                CHECK(drawn.Min.y >= center.y - half);
                CHECK(drawn.Max.x <= center.x + half);
                CHECK(drawn.Max.y <= center.y + half);
                // and fills most of it, so a size is the size it is drawn at
                CHECK(drawn.GetWidth() >= size * 0.4f);
            }
        }
    });
}

TEST_CASE("Controls icons: none draws nothing, and a button's icon and the others have their design boxes")
{
    const TestContext context;
    context.frame([] {
        auto* drawList = ImGui::GetWindowDrawList();
        const int before = drawList->VtxBuffer.Size;
        drawIcon(Icon::None, ImVec2(100.0f, 100.0f), IM_COL32(255, 255, 255, 255));
        CHECK(drawList->VtxBuffer.Size == before);
    });
    CHECK(common::fEqual(iconDesignBox(Icon::Cross), ICON_SIZE));
    CHECK(common::fEqual(iconDesignBox(Icon::Vibrate), ICON_SIZE));
    CHECK(common::fEqual(iconDesignBox(Icon::Play), 20.0f));
}

TEST_CASE("Controls row popups: under a row with room, over one without, centered on it and inside its window")
{
    const TestContext context;
    const ImVec2 size(300.0f, 200.0f);

    const ImRect high(ImVec2(200.0f, 100.0f), ImVec2(600.0f, 148.0f));
    const auto under = rowPopup(context, high, size);
    CHECK(common::fEqual(under.Min.y, high.Max.y));
    CHECK(common::fEqual(under.GetHeight(), size.y));
    CHECK(common::fEqual(under.GetWidth(), size.x));
    CHECK(common::fEqual(under.GetCenter().x, high.GetCenter().x));

    const ImRect low(ImVec2(200.0f, 500.0f), ImVec2(600.0f, 548.0f));
    const auto over = rowPopup(context, low, size);
    CHECK(common::fEqual(over.Max.y, low.Min.y));

    // a row at a side: the popup stays inside the window
    const auto atLeft = rowPopup(context, ImRect(ImVec2(0.0f, 100.0f), ImVec2(100.0f, 148.0f)), size);
    CHECK(atLeft.Min.x >= 0.0f);
    const auto atRight = rowPopup(context, ImRect(ImVec2(700.0f, 100.0f), ImVec2(800.0f, 148.0f)), size);
    CHECK(atRight.Max.x <= DISPLAY.x);
}
