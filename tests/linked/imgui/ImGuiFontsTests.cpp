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
    // the size the framework draws its canvases with by default
    constexpr float FONT_SIZE = 24.0f;

    /**
     * An ImGui context with the canvas font loaded, as a canvas has it, with no renderer: the flag of a
     * renderer that takes ImGui's texture requests, so the font is rasterized at each size it is drawn at.
     */
    struct TestFonts
    {
        TestFonts()
        {
            ImGui::CreateContext();
            ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(FONT_SIZE);
        }

        ~TestFonts()
        {
            ImGui::DestroyContext();
        }
    };

    float widthOf(ImFont* font, const char* text)
    {
        return font->CalcTextSizeA(FONT_SIZE, FLT_MAX, 0.0f, text).x;
    }
}

TEST_CASE("ImGui fonts: the canvas font has a regular, a bold and an italic face")
{
    const TestFonts fonts;
    ImFont* regular = imgui::canvasFont(imgui::FontStyle::Regular);
    ImFont* bold = imgui::canvasFont(imgui::FontStyle::Bold);
    ImFont* italic = imgui::canvasFont(imgui::FontStyle::Italic);
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
    // copied, as baking another face may move the regular one's
    ImFontBaked* regular = imgui::canvasFont(imgui::FontStyle::Regular)->GetFontBaked(FONT_SIZE);
    const float regularAscent = regular->Ascent;
    const float regularDescent = regular->Descent;
    const ImFontGlyph regularCapital = *regular->FindGlyph('H');

    for (const auto style : { imgui::FontStyle::Bold, imgui::FontStyle::Italic }) {
        ImFontBaked* face = imgui::canvasFont(style)->GetFontBaked(FONT_SIZE);
        CHECK(common::fEqual(face->Ascent, regularAscent));
        CHECK(common::fEqual(face->Descent, regularDescent));

        // a capital's top and bottom, within a pixel of the rounding of its raster
        const ImFontGlyph* capital = face->FindGlyph('H');
        CHECK_THAT(capital->Y0, WithinAbs(regularCapital.Y0, 1.0));
        CHECK_THAT(capital->Y1, WithinAbs(regularCapital.Y1, 1.0));
    }
}
