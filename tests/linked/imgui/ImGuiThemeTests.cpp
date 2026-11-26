#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "common/CommonUtils.h"
#include "imgui/ImGuiKeyboard.h"
#include "imgui/ImGuiSettings.h"
#include "imgui/ImGuiTheme.h"
#include "vrui/UIPanelStyle.h"

using namespace f4cf;
using namespace f4cf::imgui;

namespace
{
    bool sameColor(const render::Color& left, const render::Color& right)
    {
        return left.bytes() == right.bytes();
    }

    bool sameColor(const ImVec4& left, const render::Color& right)
    {
        return sameColor(render::Color{ left.x, left.y, left.z, left.w }, right);
    }

    bool sameColor(const ImVec4& left, const ImVec4& right)
    {
        return common::fEqual(left.x, right.x) && common::fEqual(left.y, right.y) && common::fEqual(left.z, right.z) && common::fEqual(left.w, right.w);
    }

    bool samePair(const ImVec2& value, const float x, const float y)
    {
        return common::fEqual(value.x, x) && common::fEqual(value.y, y);
    }

    /**
     * Whether two panel styles are the same look: their colors by their bytes, their sizes within fEqual.
     */
    bool samePanelStyle(const vrui::UIPanelStyle& left, const vrui::UIPanelStyle& right)
    {
        return sameColor(left.color, right.color) && sameColor(left.background, right.background) && sameColor(left.borderColor, right.borderColor) &&
               common::fEqual(left.borderThicknessUnits, right.borderThicknessUnits) && common::fEqual(left.cornerRadiusUnits, right.cornerRadiusUnits) &&
               common::fEqual(left.padding.top, right.padding.top) && common::fEqual(left.padding.right, right.padding.right) &&
               common::fEqual(left.padding.bottom, right.padding.bottom) && common::fEqual(left.padding.left, right.padding.left);
    }

    /**
     * Whether two keyboard styles are the same: their colors by their bytes, their sizes within fEqual.
     */
    bool sameKeyboardStyle(const KeyboardStyle& left, const KeyboardStyle& right)
    {
        return common::fEqual(left.widthShare, right.widthShare) && common::fEqual(left.tiltDegrees, right.tiltDegrees) && sameColor(left.background, right.background) &&
               sameColor(left.border, right.border) && sameColor(left.key, right.key) && sameColor(left.keyHovered, right.keyHovered) && sameColor(left.text, right.text) &&
               sameColor(left.functionKey, right.functionKey) && sameColor(left.functionKeyHovered, right.functionKeyHovered) && sameColor(left.accent, right.accent) &&
               sameColor(left.accentText, right.accentText) && sameColor(left.field, right.field) && sameColor(left.selection, right.selection);
    }

    /**
     * A theme whose every color and size is unlike its default and unlike the others, so a field read from the
     * wrong one shows.
     */
    Theme probeTheme()
    {
        Theme theme;
        theme.accent = render::Color::rgba(201, 1, 2);
        theme.muted = render::Color::rgba(202, 3, 4);
        theme.track = render::Color::rgba(203, 5, 6, 30);
        theme.hover = render::Color::rgba(204, 7, 8, 40);
        theme.pressed = render::Color::rgba(205, 9, 10, 50);
        theme.glow = render::Color::rgba(206, 11, 12);
        theme.rule = render::Color::rgba(207, 13, 14, 100);
        theme.frameBorder = render::Color::rgba(208, 15, 16, 150);
        theme.onAccent = render::Color::rgba(209, 17, 18);
        theme.popup = render::Color::rgba(210, 19, 20, 240);
        theme.scrim = render::Color::rgba(211, 21, 22, 130);
        theme.glass = render::Color::rgba(212, 23, 24, 200);
        theme.bodyFontSize = 23.0f;
        theme.frameRounding = 3.0f;
        theme.frameBorderSize = 1.5f;
        theme.popupRounding = 9.0f;
        theme.scrollbarSize = 14.0f;
        theme.grabWidth = 19.0f;
        theme.grabRounding = 2.5f;
        theme.disabledOpacity = 0.35f;
        theme.space1 = 5.0f;
        theme.space2 = 9.0f;
        theme.space3 = 13.0f;
        theme.space4 = 17.0f;
        theme.space8 = 33.0f;
        theme.panelBorderUnits = 0.07f;
        theme.panelCornerUnits = 0.4f;
        theme.panelPaddingUnits = 0.25f;
        return theme;
    }

    /**
     * Whether two themes are the same field by field: their colors by their bytes, their numbers within fEqual.
     */
    bool sameTheme(const Theme& left, const Theme& right)
    {
        for (const auto& field : themeFields()) {
            const bool same = field.color ? sameColor(left.*field.color, right.*field.color) : common::fEqual(left.*field.number, right.*field.number);
            if (!same) {
                UNSCOPED_INFO("differs in " << field.name);
                return false;
            }
        }
        return true;
    }
}

TEST_CASE("Theme: the default theme is the framework's house look")
{
    const Theme theme;
    CHECK(sameColor(theme.accent, vrui::F4VR_PANEL_STYLE.color));
    CHECK(samePanelStyle(theme.panelStyle(), vrui::F4VR_PANEL_STYLE));
}

TEST_CASE("Theme: the default keyboard is the default theme's")
{
    CHECK(sameKeyboardStyle(Theme{}.keyboardStyle(KeyboardStyle{}), KeyboardStyle{}));
}

TEST_CASE("Theme: a panel's style has the theme's colors and sizes")
{
    const auto theme = probeTheme();
    const auto style = theme.panelStyle();
    CHECK(sameColor(style.color, theme.accent));
    CHECK(sameColor(style.background, theme.glass));
    CHECK(sameColor(style.borderColor, theme.accent));
    CHECK(common::fEqual(style.borderThicknessUnits, theme.panelBorderUnits));
    CHECK(common::fEqual(style.cornerRadiusUnits, theme.panelCornerUnits));
    CHECK(samePanelStyle(style,
        { .color = theme.accent,
            .background = theme.glass,
            .borderColor = theme.accent,
            .borderThicknessUnits = theme.panelBorderUnits,
            .cornerRadiusUnits = theme.panelCornerUnits,
            .padding = vrui::UIPadding::all(theme.panelPaddingUnits) }));
}

TEST_CASE("Theme: a keyboard style takes the theme's colors and keeps the rest")
{
    const auto theme = probeTheme();
    KeyboardStyle base;
    base.widthShare = 0.5f;
    base.tiltDegrees = 30.0f;
    base.key = render::Color::rgba(1, 2, 3);
    base.text = render::Color::rgba(4, 5, 6);
    const auto style = theme.keyboardStyle(base);

    CHECK(sameColor(style.background, theme.glass));
    CHECK(sameColor(style.border, theme.accent));
    CHECK(sameColor(style.accent, theme.accent));
    CHECK(sameColor(style.accentText, theme.onAccent));
    CHECK(sameColor(style.selection, render::Color::rgba(201, 1, 2, 90)));

    auto expected = base;
    expected.background = style.background;
    expected.border = style.border;
    expected.accent = style.accent;
    expected.accentText = style.accentText;
    expected.selection = style.selection;
    CHECK(sameKeyboardStyle(style, expected));
}

TEST_CASE("Theme: ImGui's style has the theme's sizes")
{
    const auto theme = probeTheme();
    ImGuiStyle style;
    theme.applyTo(style);

    CHECK(common::fEqual(style.FontSizeBase, theme.bodyFontSize));
    CHECK(samePair(style.WindowPadding, theme.space4, theme.space4));
    CHECK(samePair(style.FramePadding, theme.space4, theme.space3));
    CHECK(samePair(style.ItemSpacing, theme.space3, theme.space2));
    CHECK(samePair(style.ItemInnerSpacing, theme.space2, theme.space2));
    CHECK(samePair(style.TouchExtraPadding, theme.space1, theme.space1));
    CHECK(samePair(style.SeparatorTextPadding, theme.space2, theme.space2));
    CHECK(samePair(style.SeparatorTextAlign, 0.0f, 0.5f));
    CHECK(samePair(style.SelectableTextAlign, 0.0f, 0.5f));
    CHECK(common::fEqual(style.IndentSpacing, theme.space8));
    CHECK(common::fEqual(style.ScrollbarSize, theme.scrollbarSize));
    CHECK(common::fEqual(style.GrabMinSize, theme.grabWidth));
    CHECK(common::fEqual(style.FrameBorderSize, theme.frameBorderSize));
    CHECK(common::fEqual(style.PopupBorderSize, theme.frameBorderSize));
    CHECK(common::fEqual(style.WindowBorderSize, theme.frameBorderSize));
    CHECK(common::fEqual(style.SeparatorTextBorderSize, theme.frameBorderSize));
    CHECK(common::fEqual(style.ChildBorderSize, 0.0f));
    CHECK(common::fEqual(style.FrameRounding, theme.frameRounding));
    CHECK(common::fEqual(style.ScrollbarRounding, theme.frameRounding));
    CHECK(common::fEqual(style.WindowRounding, theme.popupRounding));
    CHECK(common::fEqual(style.ChildRounding, theme.popupRounding));
    CHECK(common::fEqual(style.PopupRounding, theme.popupRounding));
    CHECK(common::fEqual(style.GrabRounding, theme.grabRounding));
    CHECK(common::fEqual(style.DisabledAlpha, theme.disabledOpacity));
}

TEST_CASE("Theme: ImGui's colors are the theme's, and the rest as StyleColorsDark leaves them")
{
    const auto theme = probeTheme();
    const render::Color none{ 0.0f, 0.0f, 0.0f, 0.0f };
    const std::vector<std::pair<ImGuiCol, render::Color>> table{
        { ImGuiCol_Text, theme.accent },
        { ImGuiCol_TextDisabled, theme.muted },
        { ImGuiCol_WindowBg, none },
        { ImGuiCol_ChildBg, none },
        { ImGuiCol_BorderShadow, none },
        { ImGuiCol_Button, none },
        { ImGuiCol_PopupBg, theme.popup },
        { ImGuiCol_Border, theme.frameBorder },
        { ImGuiCol_FrameBg, theme.track },
        { ImGuiCol_ScrollbarBg, theme.track },
        { ImGuiCol_FrameBgHovered, theme.hover },
        { ImGuiCol_ButtonHovered, theme.hover },
        { ImGuiCol_HeaderHovered, theme.hover },
        { ImGuiCol_FrameBgActive, theme.pressed },
        { ImGuiCol_ButtonActive, theme.pressed },
        { ImGuiCol_HeaderActive, theme.pressed },
        { ImGuiCol_TextSelectedBg, theme.pressed },
        { ImGuiCol_Header, theme.accent },
        { ImGuiCol_CheckMark, theme.onAccent },
        { ImGuiCol_SliderGrab, theme.accent },
        { ImGuiCol_ScrollbarGrabHovered, theme.accent },
        { ImGuiCol_NavCursor, theme.accent },
        { ImGuiCol_InputTextCursor, theme.accent },
        { ImGuiCol_SliderGrabActive, theme.glow },
        { ImGuiCol_ScrollbarGrabActive, theme.glow },
        { ImGuiCol_ScrollbarGrab, theme.frameBorder },
        { ImGuiCol_Separator, theme.rule },
        { ImGuiCol_ModalWindowDimBg, theme.scrim },
    };

    ImGuiStyle dark;
    ImGui::StyleColorsDark(&dark);
    auto style = dark;
    theme.applyTo(style);

    std::set<int> inTable;
    for (const auto& [col, color] : table) {
        INFO(ImGui::GetStyleColorName(col));
        CHECK(sameColor(style.Colors[col], color));
        inTable.insert(col);
    }
    for (int col = 0; col < ImGuiCol_COUNT; ++col) {
        if (!inTable.contains(col)) {
            INFO(ImGui::GetStyleColorName(col));
            CHECK(sameColor(style.Colors[col], dark.Colors[col]));
        }
    }
    CHECK(samePair(style.CellPadding, dark.CellPadding.x, dark.CellPadding.y));
    CHECK(common::fEqual(style.TabRounding, dark.TabRounding));
}

TEST_CASE("Theme: as JSON, every field of the table in its shape")
{
    const auto theme = probeTheme();
    const auto json = themeJson(theme);
    CHECK(json.size() == themeFields().size());
    for (const auto& field : themeFields()) {
        const std::string name(field.name);
        INFO(name);
        REQUIRE(json.contains(name));
        if (field.color) {
            CHECK(json[name] == nlohmann::json((theme.*field.color).bytes()));
        } else {
            REQUIRE(json[name].is_number());
            CHECK(common::fEqual(json[name].get<float>(), theme.*field.number));
        }
    }
    // a number is the shortest text of its float
    CHECK(themeJson(Theme{})["panelBorderUnits"].dump() == "0.05");
}

TEST_CASE("Theme: a theme's JSON set on another theme gives that theme")
{
    const auto theme = probeTheme();
    const auto set = themeWithFields(Theme{}, themeJson(theme));
    REQUIRE(set.has_value());
    CHECK(sameTheme(*set, theme));
}

TEST_CASE("Theme: setting fields changes those fields and no other")
{
    const auto base = probeTheme();
    const auto set = themeWithFields(base, { { "accent", { 255, 180, 60, 128 } }, { "space2", 6 }, { "panelCornerUnits", 0.3 } });
    REQUIRE(set.has_value());
    CHECK(sameColor(set->accent, render::Color::rgba(255, 180, 60, 128)));
    CHECK(common::fEqual(set->space2, 6.0f));
    CHECK(common::fEqual(set->panelCornerUnits, 0.3f));

    auto expected = base;
    expected.accent = set->accent;
    expected.space2 = set->space2;
    expected.panelCornerUnits = set->panelCornerUnits;
    CHECK(sameTheme(*set, expected));
}

TEST_CASE("Theme: an unknown field or a value of the wrong shape sets nothing and is named")
{
    const std::vector<std::pair<nlohmann::json, std::string>> wrong{
        { { { "accent", { 1, 2, 3, 4 } }, { "nope", 1 } }, "'nope'" },
        { { { "accent", { 1, 2, 3 } } }, "'accent'" },
        { { { "accent", { 1, 2, 3, 256 } } }, "'accent'" },
        { { { "accent", { 1, 2, -1, 4 } } }, "'accent'" },
        { { { "accent", { 1, 2, 3.5, 4 } } }, "'accent'" },
        { { { "accent", "#ffffff" } }, "'accent'" },
        { { { "space2", "6" } }, "'space2'" },
        { { { "space2", true } }, "'space2'" },
        { { { "space2", nlohmann::json::array({ 6 }) } }, "'space2'" },
        { { { "space2", 6 }, { "glass", 3 } }, "'glass'" },
        { { { "space2", -1 } }, "'space2'" },
        { { { "space2", 1e39 } }, "'space2'" },
        { { { "controlHeight", 0 } }, "'controlHeight'" },
        { { { "bodyFontSize", 0.5 } }, "'bodyFontSize'" },
        { { { "disabledOpacity", 1.5 } }, "'disabledOpacity'" },
        { nlohmann::json::object(), "object" },
        { nlohmann::json::array({ 1 }), "object" },
    };
    for (const auto& [fields, named] : wrong) {
        INFO(fields.dump());
        const auto set = themeWithFields(Theme{}, fields);
        REQUIRE_FALSE(set.has_value());
        CHECK(set.error().find(named) != std::string::npos);
    }
}

TEST_CASE("Theme: a number at the edge of its field's range is set")
{
    const auto set = themeWithFields(Theme{}, { { "frameRounding", 0 }, { "controlHeight", 1 }, { "disabledOpacity", 1 }, { "panelBorderUnits", 0 } });
    REQUIRE(set.has_value());
    CHECK(common::fEqual(set->frameRounding, 0.0f));
    CHECK(common::fEqual(set->controlHeight, 1.0f));
    CHECK(common::fEqual(set->disabledOpacity, 1.0f));
    CHECK(common::fEqual(set->panelBorderUnits, 0.0f));
}

TEST_CASE("Theme: the body font size is clamped")
{
    Theme theme;
    ImGuiStyle style;

    theme.bodyFontSize = 4.0f;
    theme.applyTo(style);
    CHECK(common::fEqual(style.FontSizeBase, MIN_FONT_SIZE_PIXELS));

    theme.bodyFontSize = 400.0f;
    theme.applyTo(style);
    CHECK(common::fEqual(style.FontSizeBase, MAX_FONT_SIZE_PIXELS));
}
