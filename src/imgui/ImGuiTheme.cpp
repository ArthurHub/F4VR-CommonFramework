#include "ImGuiTheme.h"

#include <algorithm>

#include "../vrui/UIPanelStyle.h"
#include "ImGuiKeyboard.h"
#include "ImGuiSettings.h"

namespace
{
    using namespace f4cf;

    ImVec4 vec4(const render::Color& color)
    {
        return { color.r, color.g, color.b, color.a };
    }
}

namespace f4cf::imgui
{
    /**
     * Sets ImGui's sizes and colors from the theme: the fields of the design system's ImGui style tables, and the body
     * font size. Only those fields: the rest stay as they are, StyleColorsDark's on a new context, so a field a mod set
     * by hand outside of them outlives a new theme.
     * The font size takes effect as ImGui's next frame begins, which is when the framework applies it.
     */
    void Theme::applyTo(ImGuiStyle& style) const
    {
        style.FontSizeBase = std::clamp(bodyFontSize, MIN_FONT_SIZE_PIXELS, MAX_FONT_SIZE_PIXELS);

        style.WindowPadding = ImVec2(space4, space4);
        style.FramePadding = ImVec2(space4, space3);
        style.ItemSpacing = ImVec2(space3, space2);
        style.ItemInnerSpacing = ImVec2(space2, space2);
        style.TouchExtraPadding = ImVec2(space1, space1);
        style.IndentSpacing = space8;
        style.ScrollbarSize = scrollbarSize;
        style.GrabMinSize = grabWidth;
        style.FrameBorderSize = frameBorderSize;
        style.PopupBorderSize = frameBorderSize;
        style.WindowBorderSize = frameBorderSize;
        style.ChildBorderSize = 0.0f;
        style.FrameRounding = frameRounding;
        style.ScrollbarRounding = frameRounding;
        style.WindowRounding = popupRounding;
        style.ChildRounding = popupRounding;
        style.PopupRounding = popupRounding;
        style.GrabRounding = grabRounding;
        style.SeparatorTextBorderSize = frameBorderSize;
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(space2, space2);
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
        style.DisabledAlpha = disabledOpacity;

        constexpr ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
        auto& colors = style.Colors;
        colors[ImGuiCol_Text] = vec4(accent);
        colors[ImGuiCol_TextDisabled] = vec4(muted);
        colors[ImGuiCol_WindowBg] = none;
        colors[ImGuiCol_ChildBg] = none;
        colors[ImGuiCol_BorderShadow] = none;
        colors[ImGuiCol_Button] = none;
        colors[ImGuiCol_PopupBg] = vec4(popup);
        colors[ImGuiCol_Border] = vec4(frameBorder);
        colors[ImGuiCol_FrameBg] = vec4(track);
        colors[ImGuiCol_ScrollbarBg] = vec4(track);
        colors[ImGuiCol_FrameBgHovered] = vec4(hover);
        colors[ImGuiCol_ButtonHovered] = vec4(hover);
        colors[ImGuiCol_HeaderHovered] = vec4(hover);
        colors[ImGuiCol_FrameBgActive] = vec4(pressed);
        colors[ImGuiCol_ButtonActive] = vec4(pressed);
        colors[ImGuiCol_HeaderActive] = vec4(pressed);
        colors[ImGuiCol_TextSelectedBg] = vec4(pressed);
        colors[ImGuiCol_Header] = vec4(accent);
        colors[ImGuiCol_CheckMark] = vec4(onAccent);
        colors[ImGuiCol_SliderGrab] = vec4(accent);
        colors[ImGuiCol_ScrollbarGrabHovered] = vec4(accent);
        colors[ImGuiCol_NavCursor] = vec4(accent);
        colors[ImGuiCol_InputTextCursor] = vec4(accent);
        colors[ImGuiCol_SliderGrabActive] = vec4(glow);
        colors[ImGuiCol_ScrollbarGrabActive] = vec4(glow);
        colors[ImGuiCol_ScrollbarGrab] = vec4(frameBorder);
        colors[ImGuiCol_Separator] = vec4(rule);
        colors[ImGuiCol_ModalWindowDimBg] = vec4(scrim);
    }

    /**
     * A panel's look in the theme: its content and edge in the accent, its background the glass, and the theme's edge,
     * corner and padding.
     */
    vrui::UIPanelStyle Theme::panelStyle() const
    {
        return { .color = accent,
            .background = glass,
            .borderColor = accent,
            .borderThicknessUnits = panelBorderUnits,
            .cornerRadiusUnits = panelCornerUnits,
            .padding = vrui::UIPadding::all(panelPaddingUnits) };
    }

    /**
     * A keyboard style in the theme's colors: the glass behind it, the accent for its edge, a held key and the caret,
     * the text on the accent, and the accent at alpha 90 for a selection. Its key grays, size and tilt stay as the given
     * one has them.
     */
    KeyboardStyle Theme::keyboardStyle(const KeyboardStyle& base) const
    {
        auto style = base;
        style.background = glass;
        style.border = accent;
        style.accent = accent;
        style.accentText = onAccent;
        style.selection = render::Color{ accent.r, accent.g, accent.b, 90.0f / 255.0f };
        return style;
    }
}
