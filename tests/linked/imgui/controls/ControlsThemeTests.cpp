#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include "common/CommonUtils.h"
#include "imgui/ImGuiKeyboard.h"
#include "imgui/controls/Controls.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    /**
     * Sets the default theme and keyboard style back as a test ends, so one test's look does not reach the next.
     */
    struct DefaultTheme
    {
        ~DefaultTheme()
        {
            get().setTheme({});
            imgui::keyboard().setStyle({});
        }
    };

    /**
     * An ImGui context with no renderer and no frame, current while it lives.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
        }

        ~TestContext()
        {
            ImGui::DestroyContext();
        }
    };

    imgui::Theme amberTheme()
    {
        imgui::Theme theme;
        theme.accent = render::Color::rgba(255, 180, 60);
        theme.onAccent = render::Color::rgba(30, 20, 5);
        theme.glass = render::Color::rgba(20, 15, 10, 200);
        theme.plate = render::Color::rgba(255, 180, 60, 15);
        theme.important = render::Color::rgba(80, 200, 255);
        theme.space2 = 6.0f;
        return theme;
    }
}

TEST_CASE("Controls: setTheme packs the theme's own colors too")
{
    const DefaultTheme restore;
    get().setTheme(amberTheme());
    CHECK(get().colors().glass == IM_COL32(20, 15, 10, 200));
    CHECK(get().colors().plate == IM_COL32(255, 180, 60, 15));
    CHECK(get().colors().important == IM_COL32(80, 200, 255, 255));
}

TEST_CASE("Controls: setTheme gives the keyboard the theme's colors and keeps the rest of its style")
{
    const DefaultTheme restore;
    auto keyboardStyle = imgui::keyboard().style();
    keyboardStyle.widthShare = 0.5f;
    keyboardStyle.key = render::Color::rgba(1, 2, 3);
    imgui::keyboard().setStyle(keyboardStyle);

    get().setTheme(amberTheme());
    const auto& style = imgui::keyboard().style();
    CHECK(style.background.bytes() == amberTheme().glass.bytes());
    CHECK(style.accent.bytes() == amberTheme().accent.bytes());
    CHECK(style.accentText.bytes() == amberTheme().onAccent.bytes());
    CHECK(common::fEqual(style.widthShare, 0.5f));
    CHECK(style.key.bytes() == render::Color::rgba(1, 2, 3).bytes());
}

TEST_CASE("Controls: setTheme marks ImGui's style, which the next update sets once")
{
    const DefaultTheme restore;
    const TestContext context;
    auto& style = ImGui::GetStyle();

    get().setTheme(amberTheme());
    get().updateImGuiStyle();
    CHECK(common::fEqual(style.ItemSpacing.y, 6.0f));
    CHECK(ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]) == IM_COL32(255, 180, 60, 255));

    // set already: an update does not set it again over a change made since
    style.ItemSpacing.y = 1.0f;
    get().updateImGuiStyle();
    CHECK(common::fEqual(style.ItemSpacing.y, 1.0f));

    get().setTheme({});
    get().updateImGuiStyle();
    CHECK(common::fEqual(style.ItemSpacing.y, imgui::Theme{}.space2));
}

TEST_CASE("Controls: applyImGuiStyle sets the current context's style from the theme")
{
    const DefaultTheme restore;
    const TestContext context;
    get().setTheme(amberTheme());
    get().applyImGuiStyle();
    CHECK(common::fEqual(ImGui::GetStyle().ItemSpacing.y, 6.0f));
    CHECK(common::fEqual(ImGui::GetStyle().FontSizeBase, imgui::Theme{}.bodyFontSize));
}
