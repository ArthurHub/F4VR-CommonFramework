#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "common/CommonUtils.h"
#include "imgui/ImGuiKeyboard.h"
#include "imgui/controls/Controls.h"

using namespace f4cf;
using namespace f4cf::imgui::controls;

namespace
{
    /**
     * Sets the default theme and keyboard style back as a test ends, with no field overridden, so one test's look does
     * not reach the next.
     */
    struct DefaultTheme
    {
        ~DefaultTheme()
        {
            get().clearThemeOverride();
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

TEST_CASE("Controls: an override's fields are drawn from the next frame, and no other changes")
{
    const DefaultTheme restore;
    const TestContext context;
    get().setTheme(amberTheme());
    get().updateImGuiStyle();

    REQUIRE(get().overrideTheme({ { "accent", { 200, 10, 20, 255 } }, { "space2", 3 } }).has_value());
    CHECK(get().theme().accent.bytes() == render::Color::rgba(200, 10, 20).bytes());
    CHECK(common::fEqual(get().theme().space2, 3.0f));
    CHECK(get().colors().accent == IM_COL32(200, 10, 20, 255));
    CHECK(imgui::keyboard().style().accent.bytes() == render::Color::rgba(200, 10, 20).bytes());
    get().updateImGuiStyle();
    CHECK(common::fEqual(ImGui::GetStyle().ItemSpacing.y, 3.0f));
    CHECK(ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]) == IM_COL32(200, 10, 20, 255));

    // the rest is the mod's theme, and the fields add up over two calls
    CHECK(get().theme().glass.bytes() == amberTheme().glass.bytes());
    CHECK(common::fEqual(get().theme().space3, amberTheme().space3));
    REQUIRE(get().overrideTheme({ { "space3", 5 } }).has_value());
    CHECK(get().overriddenThemeFields() == std::vector<std::string_view>{ "accent", "space2", "space3" });
    CHECK(get().theme().accent.bytes() == render::Color::rgba(200, 10, 20).bytes());
}

TEST_CASE("Controls: an override that is not valid sets nothing")
{
    const DefaultTheme restore;
    get().setTheme(amberTheme());
    const auto overridden = get().overrideTheme({ { "space2", 3 }, { "accent", { 1, 2, 3 } } });
    REQUIRE_FALSE(overridden.has_value());
    CHECK(overridden.error().find("'accent'") != std::string::npos);
    CHECK(get().overriddenThemeFields().empty());
    CHECK(common::fEqual(get().theme().space2, amberTheme().space2));
    CHECK(get().colors().accent == IM_COL32(255, 180, 60, 255));
}

TEST_CASE("Controls: an override stays over a new theme of the mod, whose other fields are drawn")
{
    const DefaultTheme restore;
    get().setTheme({});
    REQUIRE(get().overrideTheme({ { "accent", { 200, 10, 20, 255 } } }).has_value());

    get().setTheme(amberTheme());
    CHECK(get().theme().accent.bytes() == render::Color::rgba(200, 10, 20).bytes());
    CHECK(get().theme().glass.bytes() == amberTheme().glass.bytes());
    CHECK(common::fEqual(get().theme().space2, amberTheme().space2));
    CHECK(get().colors().glass == IM_COL32(20, 15, 10, 200));
    CHECK(get().overriddenThemeFields() == std::vector<std::string_view>{ "accent" });
}

TEST_CASE("Controls: clearing the override draws the mod's theme as it set it")
{
    const DefaultTheme restore;
    const TestContext context;
    get().setTheme(amberTheme());
    REQUIRE(get().overrideTheme({ { "accent", { 200, 10, 20, 255 } }, { "space2", 3 } }).has_value());
    get().updateImGuiStyle();

    get().clearThemeOverride();
    CHECK(get().overriddenThemeFields().empty());
    CHECK(get().theme().accent.bytes() == amberTheme().accent.bytes());
    CHECK(get().colors().accent == IM_COL32(255, 180, 60, 255));
    CHECK(imgui::keyboard().style().accent.bytes() == amberTheme().accent.bytes());
    get().updateImGuiStyle();
    CHECK(common::fEqual(ImGui::GetStyle().ItemSpacing.y, 6.0f));

    // a field set again after a clear is drawn alone
    REQUIRE(get().overrideTheme({ { "space2", 4 } }).has_value());
    CHECK(get().overriddenThemeFields() == std::vector<std::string_view>{ "space2" });
    CHECK(get().theme().accent.bytes() == amberTheme().accent.bytes());
}
