#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Text.h"
#include "imgui/controls/TextInputs.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    const ImRect BOX(ImVec2(100.0f, 100.0f), ImVec2(492.0f, 148.0f));

    /**
     * An ImGui context with the canvas font and no renderer, whose frames draw one text input in a window over the
     * display, with the controls' frame around them, and what the text input gave in each frame.
     */
    struct TestContext
    {
        explicit TestContext(TextOptions options = {})
            : options(options)
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(FONT_SIZE);
            get().text() = {};
        }

        ~TestContext()
        {
            get().text() = {};
            ImGui::DestroyContext();
        }

        /**
         * A frame that draws the text input, pressed or not, and keeps what it writes as the text, or one that does not.
         */
        void frame(const bool pressed = false, const bool drawn = true)
        {
            ImGui::NewFrame();
            get().beginFrame(ImGui::GetIO().DeltaTime);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
            id = ImGui::GetID("input");
            if (drawn) {
                const auto edit = textEdit(id, BOX, pressed, text, options);
                pulse = edit.pulse;
                if (edit.write) {
                    text = *edit.write;
                    writes.push_back(*edit.write);
                }
            }
            ImGui::End();
            get().endFrame();
            ImGui::EndFrame();
        }

        /**
         * A press that starts the edit, and the frame ImGui's field takes it in.
         */
        void start()
        {
            frame(true);
            frame();
        }

        void type(const char* characters)
        {
            ImGui::GetIO().AddInputCharactersUTF8(characters);
            frame();
        }

        void pressKey(const ImGuiKey key)
        {
            for (const bool down : { true, false }) {
                ImGui::GetIO().AddKeyEvent(key, down);
                frame();
            }
        }

        TextOptions options;
        ImGuiID id = 0;
        std::string text = "old";
        std::vector<std::string> writes;
        bool pulse = false;
    };
}

TEST_CASE("Controls text inputs: a press starts the edit with its pulse, and the text is written as Enter leaves the field")
{
    TestContext context;
    context.frame(true);
    CHECK(context.pulse);
    CHECK(get().isTextEdited(context.id));
    context.frame();
    CHECK(ImGui::GetActiveID() != 0);
    context.type(" and new");
    CHECK(context.writes.empty());
    context.pressKey(ImGuiKey_Enter);
    CHECK(context.writes == std::vector<std::string>{ "old and new" });
    CHECK_FALSE(get().isTextEdited(context.id));
}

TEST_CASE("Controls text inputs: a field left with the text it started with, or by Escape, writes nothing")
{
    TestContext context;
    context.start();
    context.pressKey(ImGuiKey_Enter);
    context.start();
    context.type("typed");
    context.pressKey(ImGuiKey_Escape);
    CHECK(context.writes.empty());
    CHECK(context.text == "old");
    CHECK_FALSE(get().isTextEdited(context.id));
}

TEST_CASE("Controls text inputs: a live one writes each change as it is typed, and keeps it through Escape")
{
    TestContext context({ .live = true });
    context.start();
    context.type("e");
    context.type("r");
    CHECK(context.writes == std::vector<std::string>{ "olde", "older" });
    context.pressKey(ImGuiKey_Escape);
    CHECK(context.writes.size() == 2);
    CHECK(context.text == "older");
    CHECK_FALSE(get().isTextEdited(context.id));
}

TEST_CASE("Controls text inputs: one that takes a number keeps only its characters")
{
    const auto typed = [](const TextChars chars) {
        TestContext context({ .chars = chars });
        context.text.clear();
        context.start();
        context.type("-1a2.5");
        context.pressKey(ImGuiKey_Enter);
        return context.text;
    };
    CHECK(typed(TextChars::Integer) == "-125");
    CHECK(typed(TextChars::Decimal) == "-12.5");
}

TEST_CASE("Controls text inputs: a most of characters cuts what is typed past it, counted in characters of UTF-8")
{
    TestContext context({ .maxLength = 5 });
    context.start();
    // three characters of two bytes each, escaped as the source is read in the system's code page
    context.type("\xC3\xA9\xC3\xA8\xC3\xA0");
    context.pressKey(ImGuiKey_Enter);
    CHECK(context.text == "old\xC3\xA9\xC3\xA8");
    CHECK(utf8Length(context.text) == 5);
}

TEST_CASE("Controls text inputs: Back ends the edit with nothing written, as the next frame begins")
{
    TestContext context;
    context.start();
    context.type("typed");
    CHECK(get().back());
    CHECK_FALSE(get().isTextEdited(context.id));
    CHECK_FALSE(get().back());
    context.frame();
    CHECK(ImGui::GetActiveID() == 0);
    CHECK(context.writes.empty());
    CHECK(context.text == "old");
}

TEST_CASE("Controls text inputs: a reset keeps the edit, and a frame that does not draw the field ends it")
{
    TestContext context;
    context.start();
    get().reset();
    context.frame();
    CHECK(get().isTextEdited(context.id));
    context.type("typed");
    context.frame(false, false);
    CHECK_FALSE(get().isTextEdited(context.id));
    context.frame();
    CHECK(context.writes.empty());
}
