#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cfloat>

#include <imgui.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiFonts.h"

using namespace f4cf;
using Catch::Matchers::WithinAbs;

namespace
{
    // the size and the supersample the framework draws its canvases with by default
    constexpr float FONT_SIZE = 24.0f;
    constexpr float SUPERSAMPLE = 1.5f;

    /**
     * An ImGui context with the canvas font loaded and its atlas built, as a canvas has it, with no renderer.
     */
    struct TestFonts
    {
        TestFonts()
        {
            ImGui::CreateContext();
            imgui::internal::loadCanvasFont(FONT_SIZE, SUPERSAMPLE);
            ImGui::GetIO().Fonts->Build();
        }

        ~TestFonts()
        {
            ImGui::DestroyContext();
        }
    };

    float widthOf(const ImFont* font, const char* text)
    {
        return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text).x;
    }
}

TEST_CASE("ImGui fonts: the canvas font has a regular, a bold and an italic face")
{
    const TestFonts fonts;
    const ImFont* regular = imgui::canvasFont(imgui::FontStyle::Regular);
    const ImFont* bold = imgui::canvasFont(imgui::FontStyle::Bold);
    const ImFont* italic = imgui::canvasFont(imgui::FontStyle::Italic);
    REQUIRE(regular);
    REQUIRE(bold);
    REQUIRE(italic);

    // the regular face is ImGui's default font
    CHECK(regular == ImGui::GetIO().Fonts->Fonts[0]);
    CHECK(bold != regular);
    CHECK(italic != regular);
    CHECK(widthOf(bold, "Weight") > widthOf(regular, "Weight"));
}

TEST_CASE("ImGui fonts: a word in bold or italic is as large as the words around it, on their line")
{
    const TestFonts fonts;
    const ImFont* regular = imgui::canvasFont(imgui::FontStyle::Regular);
    const ImFontGlyph* regularCapital = regular->FindGlyph('H');

    for (const auto style : { imgui::FontStyle::Bold, imgui::FontStyle::Italic }) {
        const ImFont* face = imgui::canvasFont(style);
        CHECK(common::fEqual(face->FontSize, regular->FontSize));
        CHECK(common::fEqual(face->Ascent, regular->Ascent));
        CHECK(common::fEqual(face->Descent, regular->Descent));

        // a capital's top and bottom, within a pixel of the rounding of its raster
        const ImFontGlyph* capital = face->FindGlyph('H');
        CHECK_THAT(capital->Y0, WithinAbs(regularCapital->Y0, 1.0));
        CHECK_THAT(capital->Y1, WithinAbs(regularCapital->Y1, 1.0));
    }
}
