#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>

#include <imgui.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"
#include "imgui/controls/Controls.h"
#include "imgui/controls/Text.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr float FONT_SIZE = 21.0f;

    /**
     * An ImGui frame in a window, with the canvas font and no renderer, in which the controls draw.
     */
    struct TestFrame
    {
        TestFrame()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            // no imgui.ini written where the tests run
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            io.DeltaTime = 1.0f / 90.0f;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(FONT_SIZE);
            get().applyImGuiStyle();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoDecoration);
        }

        ~TestFrame()
        {
            ImGui::End();
            ImGui::EndFrame();
            ImGui::DestroyContext();
        }
    };
}

TEST_CASE("Controls text: pieces wrap between their words")
{
    const TestFrame frame;
    const std::array pieces{ TextPiece{ .text = "alpha beta ", .color = IM_COL32_WHITE }, TextPiece{ .text = "gamma", .color = IM_COL32_WHITE } };
    const float oneWord = textWidth("alpha");
    // a little more than the words, as the width of a line is their widths added up
    const float all = textWidth("alpha beta gamma") + 0.5f;

    CHECK(countTextLines(pieces, all) == 1);
    CHECK(countTextLines(pieces, textWidth("alpha beta") + 0.5f) == 2);
    // each word on its own line
    CHECK(countTextLines(pieces, oneWord) == 3);
}

TEST_CASE("Controls text: a word wider than the width is a line by itself")
{
    const TestFrame frame;
    const std::array pieces{ TextPiece{ .text = "a extraordinarily b", .color = IM_COL32_WHITE } };
    CHECK(countTextLines(pieces, textWidth("a b")) == 3);
}

TEST_CASE("Controls text: a mark is a word with the room of a space after it")
{
    const TestFrame frame;
    const std::array pieces{ TextPiece{ .mark = Mark::Warning }, TextPiece{ .text = "word", .color = IM_COL32_WHITE } };
    CHECK(countTextLines(pieces, MARK_WIDTH + 8.0f + textWidth("word") + 0.5f) == 1);
    CHECK(countTextLines(pieces, MARK_WIDTH + 8.0f + textWidth("word") - 1.0f) == 2);
}

TEST_CASE("Controls text: a piece in its own size wraps by that size")
{
    const TestFrame frame;
    const std::array tenPixels{ TextPiece{ .text = "alpha beta", .color = IM_COL32_WHITE, .size = 10.0f } };
    // the text at 10 px fits where the current size takes two lines
    CHECK(countTextLines(tenPixels, textWidth("alpha beta") * 0.6f) == 1);
}

TEST_CASE("Controls text: a line that does not fit is cut to its room")
{
    const TestFrame frame;
    const std::array pieces{ TextPiece{ .text = "alpha beta gamma", .color = IM_COL32_WHITE } };
    const float width = textWidth("alpha beta gamma");

    // ImGui rounds a whole line's width up to a pixel, and the pieces add up their words' widths
    CHECK_THAT(drawTextLine(0.0f, width + 10.0f, 0.0f, 48.0f, pieces), WithinAbs(width, 1.0f));
    CHECK_THAT(drawTextLine(0.0f, width * 0.5f, 0.0f, 48.0f, pieces), WithinAbs(width * 0.5f, 0.01f));
    CHECK(common::fEqual(drawTextLine(0.0f, 100.0f, 0.0f, 48.0f, {}), 0.0f));
}

TEST_CASE("Controls text: wrapped text is a line of the font size for each line, and one for no words")
{
    const TestFrame frame;
    const std::array pieces{ TextPiece{ .text = "alpha beta gamma", .color = IM_COL32_WHITE } };

    CHECK(common::fEqual(drawWrappedText(pieces, TextAlign::Left, ImVec2(0.0f, 0.0f), textWidth("alpha")), FONT_SIZE * 3.0f));
    CHECK(common::fEqual(drawWrappedText(pieces, TextAlign::Center, ImVec2(0.0f, 0.0f), 700.0f), FONT_SIZE));
    CHECK(common::fEqual(drawWrappedText({}, TextAlign::Left, ImVec2(0.0f, 0.0f), 700.0f), FONT_SIZE));
}
