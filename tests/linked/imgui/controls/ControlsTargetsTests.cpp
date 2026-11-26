#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Buttons.h"
#include "imgui/controls/Choices.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Dialogs.h"
#include "imgui/controls/Targets.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    // the canvas's window in the display, and the canvas's top left a little before it, as the layer has them
    constexpr ImVec2 WINDOW_POS(100.0f, 50.0f);
    constexpr ImVec2 WINDOW_SIZE(400.0f, 300.0f);
    constexpr ImVec2 CANVAS_ORIGIN(98.0f, 48.0f);

    /**
     * Keeps the targets while a test runs, and stops as it ends: the recorder is the one of the process.
     */
    struct Recording
    {
        Recording()
        {
            targets().setRecording(true);
        }

        Recording(const Recording&) = delete;
        Recording& operator=(const Recording&) = delete;

        ~Recording()
        {
            targets().setRecording(false);
        }
    };

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw a content in one window of a canvas, as
     * the layer does: the controls' frame begins, then the canvas, from its top left.
     */
    struct TargetsContext
    {
        TargetsContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(get().theme().bodyFontSize);
            get().reset();
            get().closePopups();
        }

        TargetsContext(const TargetsContext&) = delete;
        TargetsContext& operator=(const TargetsContext&) = delete;

        ~TargetsContext()
        {
            get().reset();
            ImGui::DestroyContext();
        }

        /**
         * One frame with the content in the canvas's window.
         */
        void frame(const std::function<void()>& content) const
        {
            auto& io = ImGui::GetIO();
            io.AddMousePosEvent(-100.0f, -100.0f);
            get().updateImGuiStyle();
            ImGui::NewFrame();
            get().beginFrame(io.DeltaTime);
            ImGui::SetNextWindowPos(WINDOW_POS);
            ImGui::SetNextWindowSize(WINDOW_SIZE);
            ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoDecoration);
            targets().beginCanvas(CANVAS_ORIGIN);
            content();
            ImGui::End();
            get().endFrame();
            ImGui::Render();
            for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
                if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
                    tex->SetStatus(ImTextureStatus_OK);
                }
            }
        }
    };

    const Target* findTarget(const std::string_view kind, const std::string_view key)
    {
        const auto& kept = targets().targets();
        const auto found = std::ranges::find_if(kept, [kind, key](const Target& target) {
            return target.kind == kind && target.key == key;
        });
        return found != kept.end() ? &*found : nullptr;
    }

    bool sameRect(const ImRect& left, const ImRect& right)
    {
        return common::fEqual(left.Min.x, right.Min.x) && common::fEqual(left.Min.y, right.Min.y) && common::fEqual(left.Max.x, right.Max.x) &&
               common::fEqual(left.Max.y, right.Max.y);
    }

    /**
     * A rect of the display in the canvas's pixels.
     */
    ImRect inCanvas(ImRect rect)
    {
        rect.Translate(ImVec2(-CANVAS_ORIGIN.x, -CANVAS_ORIGIN.y));
        return rect;
    }
}

TEST_CASE("Controls targets: nothing is kept while not recording")
{
    const TargetsContext context;
    context.frame([] {
        button("Save", ImRect(150.0f, 100.0f, 250.0f, 148.0f), ButtonLook::Primary);
        targets().add("row", "a", ImRect(150.0f, 200.0f, 250.0f, 248.0f));
        targets().addScroll("page");
    });
    CHECK(targets().targets().empty());
    CHECK(targets().scrolls().empty());
}

TEST_CASE("Controls targets: a rect is kept from the canvas's top left, the part its window shows, with its path")
{
    const TargetsContext context;
    const Recording recording;
    ImRect clip;
    const ImRect inside(150.0f, 100.0f, 250.0f, 148.0f);
    const ImRect overEdge(450.0f, 120.0f, 650.0f, 168.0f);
    context.frame([&] {
        clip = ImGui::GetCurrentWindow()->ClipRect;
        targets().add("row", "inside", inside, { "Page", "Sub" });
        targets().add("row", "overEdge", overEdge);
        // past the window's bottom, as a row scrolled away
        targets().add("row", "away", ImRect(150.0f, 500.0f, 250.0f, 548.0f));
    });

    const auto* kept = findTarget("row", "inside");
    REQUIRE(kept);
    CHECK(sameRect(kept->rect, inCanvas(inside)));
    CHECK(kept->path == std::vector<std::string>{ "Page", "Sub" });

    const auto* cut = findTarget("row", "overEdge");
    REQUIRE(cut);
    CHECK(common::fEqual(cut->rect.Max.x, clip.Max.x - CANVAS_ORIGIN.x));
    CHECK(common::fEqual(cut->rect.Min.x, overEdge.Min.x - CANVAS_ORIGIN.x));
    CHECK(cut->path.empty());

    CHECK_FALSE(findTarget("row", "away"));
}

TEST_CASE("Controls targets: a frame drops the last one's targets and scrolls")
{
    const TargetsContext context;
    const Recording recording;
    context.frame([] {
        targets().add("row", "a", ImRect(150.0f, 100.0f, 250.0f, 148.0f));
        targets().addScroll("page");
        targets().addScroll("list", 12.0f, 40.0f);
    });
    REQUIRE(targets().targets().size() == 1);
    REQUIRE(targets().scrolls().size() == 2);
    CHECK(targets().scrolls()[0].area == "page");
    CHECK(targets().scrolls()[1].area == "list");
    CHECK(common::fEqual(targets().scrolls()[1].y, 12.0f));
    CHECK(common::fEqual(targets().scrolls()[1].max, 40.0f));

    context.frame([] {
    });
    CHECK(targets().targets().empty());
    CHECK(targets().scrolls().empty());
}

TEST_CASE("Controls targets: a button, a dialog's buttons and an open list's options record themselves")
{
    const TargetsContext context;
    const Recording recording;
    const ImRect saveBox(150.0f, 100.0f, 250.0f, 148.0f);
    const ImRect dropdown(120.0f, 160.0f, 420.0f, 208.0f);
    constexpr std::array<std::string_view, 3> options{ "Low", "Medium", "High" };
    const std::array<DialogButton, 2> buttons{ DialogButton{ .label = "Keep", .look = ButtonLook::Primary }, DialogButton{ .label = "Cancel", .look = ButtonLook::Quiet } };
    std::array<ImRect, 2> buttonBoxes{};
    ListDrawn list;
    const auto content = [&] {
        button("Save", saveBox, ButtonLook::Primary);
        const ImGuiID listId = ImGui::GetID("quality");
        if (!ImGui::IsPopupOpen(listId, ImGuiPopupFlags_None)) {
            ImGui::OpenPopupEx(listId);
        }
        list = drawList(listId, dropdown, options, 1, {}, { .action = "Reset" });
        ImGui::SetCursorScreenPos(ImVec2(WINDOW_POS.x + 8.0f, 240.0f));
        dialogButtons(buttons, buttonBoxes);
    };
    context.frame(content);
    context.frame(content);

    const auto* save = findTarget(target::BUTTON, "Save");
    REQUIRE(save);
    CHECK(sameRect(save->rect, inCanvas(saveBox)));
    for (std::size_t place = 0; place < buttons.size(); ++place) {
        const auto* kept = findTarget(target::BUTTON, buttons[place].label);
        REQUIRE(kept);
        CHECK(sameRect(kept->rect, inCanvas(buttonBoxes[place])));
    }

    REQUIRE(list.shown);
    REQUIRE(list.boxes.size() == options.size() + 1);
    for (std::size_t i = 0; i < options.size(); ++i) {
        const auto* option = findTarget(target::OPTION, options[i]);
        REQUIRE(option);
        CHECK(sameRect(option->rect, inCanvas(list.boxes[i])));
    }
    const auto* action = findTarget(target::OPTION, "Reset");
    REQUIRE(action);
    CHECK(sameRect(action->rect, inCanvas(list.boxes.back())));
    REQUIRE(targets().scrolls().size() == 1);
    CHECK(targets().scrolls()[0].area == target::LIST_AREA);
    get().closePopups();
}
