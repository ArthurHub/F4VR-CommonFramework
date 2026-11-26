#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/SearchFields.h"
#include "imgui/controls/Targets.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr ImVec2 POS(100.0f, 100.0f);
    constexpr float WIDTH = 360.0f;

    // a point on the field's text, and on Clear at its right
    constexpr ImVec2 ON_FIELD(POS.x + 100.0f, POS.y + SEARCH_FIELD_HEIGHT * 0.5f);
    constexpr ImVec2 ON_CLEAR(POS.x + WIDTH - 20.0f, POS.y + SEARCH_FIELD_HEIGHT * 0.5f);
    constexpr ImVec2 AWAY(-100.0f, -100.0f);

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw one search field in a window over the
     * display, with the controls' frame around them, and keep its query as a caller does.
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
            imgui::internal::loadCanvasFont(get().theme().bodyFontSize);
            get().applyImGuiStyle();
            get().text() = {};
        }

        TestContext(const TestContext&) = delete;
        TestContext& operator=(const TestContext&) = delete;

        ~TestContext()
        {
            get().text() = {};
            ImGui::DestroyContext();
        }

        /**
         * A frame with the mouse at a point, its button down or up, which draws the field and keeps what it writes.
         */
        void frame(const ImVec2& mouse = AWAY, const bool down = false)
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            id = ImGui::GetID("search");
            drawn = searchField("search", POS, WIDTH, query, SearchField{ .placeholder = "Search 3 mods", .count = 2 });
            if (drawn.write) {
                query = *drawn.write;
                writes.push_back(*drawn.write);
            }
            if (drawn.cleared) {
                query.clear();
            }
            ImGui::End();
            get().endFrame();
            ImGui::EndFrame();
        }

        /**
         * The ray at a point, and what the frame gave once ImGui has taken the move.
         */
        SearchDrawn point(const ImVec2& at)
        {
            frame(at);
            frame(at);
            return drawn;
        }

        /**
         * A press and release of the trigger at a point, and what the frame of the release gave.
         */
        SearchDrawn press(const ImVec2& at)
        {
            frame(at);
            frame(at, true);
            frame(at);
            return drawn;
        }

        void type(const char* characters)
        {
            ImGui::GetIO().AddInputCharactersUTF8(characters);
            frame();
        }

        ImGuiID id = 0;
        std::string query;
        std::vector<std::string> writes;
        SearchDrawn drawn;
    };

    bool hasTarget(const std::string_view key)
    {
        return std::ranges::any_of(targets().targets(), [key](const Target& kept) {
            return kept.kind == target::SEARCH && kept.key == key;
        });
    }
}

TEST_CASE("Controls search fields: a press edits the field with its pulse, and the query is written as it is typed")
{
    TestContext context;
    const auto pressed = context.press(ON_FIELD);
    CHECK(pressed.pulse);
    CHECK(get().isTextEdited(context.id));
    context.frame();
    context.type("a");
    context.type("b");
    CHECK(context.writes == std::vector<std::string>{ "a", "ab" });

    // Escape, as a physical keyboard's, keeps the query
    for (const bool down : { true, false }) {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, down);
        context.frame();
    }
    CHECK(context.query == "ab");
    CHECK_FALSE(get().isTextEdited(context.id));
}

TEST_CASE("Controls search fields: Back ends the edit and keeps the query")
{
    TestContext context;
    context.press(ON_FIELD);
    context.frame();
    context.type("ab");
    REQUIRE(get().back());
    context.frame();
    CHECK_FALSE(get().isTextEdited(context.id));
    CHECK(context.query == "ab");
}

TEST_CASE("Controls search fields: Clear empties the query with its pulse, and ends an edit")
{
    TestContext context;
    context.query = "old";
    const auto onClear = context.point(ON_CLEAR);
    CHECK(onClear.clearPointed);
    CHECK_FALSE(onClear.pointed);
    const auto cleared = context.press(ON_CLEAR);
    CHECK(cleared.cleared);
    CHECK(cleared.pulse);
    CHECK(context.query.empty());

    context.query = "old";
    context.press(ON_FIELD);
    context.frame();
    REQUIRE(get().isTextEdited(context.id));
    CHECK(context.press(ON_CLEAR).cleared);
    CHECK(context.query.empty());
    CHECK_FALSE(get().isTextEdited(context.id));
}

TEST_CASE("Controls search fields: the ray on the field is told, and with no query there is no Clear")
{
    TestContext context;
    CHECK(context.point(ON_FIELD).pointed);
    const auto atRight = context.point(ON_CLEAR);
    CHECK(atRight.pointed);
    CHECK_FALSE(atRight.clearPointed);
    CHECK_FALSE(context.point(AWAY).pointed);
    CHECK_FALSE(context.press(ON_CLEAR).cleared);
}

TEST_CASE("Controls search fields: the field is a target while it is not edited, and Clear while there is a query")
{
    TestContext context;
    targets().setRecording(true);
    context.frame();
    CHECK(hasTarget("Field"));
    CHECK_FALSE(hasTarget("Clear"));

    context.query = "old";
    context.frame();
    CHECK(hasTarget("Field"));
    CHECK(hasTarget("Clear"));

    context.press(ON_FIELD);
    context.frame();
    CHECK_FALSE(hasTarget("Field"));
    CHECK(hasTarget("Clear"));
    targets().setRecording(false);
}
