#include "ImGuiTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>

#include <nlohmann/json.hpp>

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

    using imgui::Theme;
    using imgui::ThemeField;

    constexpr ThemeField colorField(const std::string_view name, render::Color Theme::* color)
    {
        return { .name = name, .color = color };
    }

    constexpr ThemeField numberField(const std::string_view name, float Theme::* number)
    {
        return { .name = name, .number = number };
    }

    // every field of Theme, in its order
    constexpr std::array THEME_FIELDS{
        colorField("accent", &Theme::accent),
        colorField("muted", &Theme::muted),
        colorField("track", &Theme::track),
        colorField("hover", &Theme::hover),
        colorField("pressed", &Theme::pressed),
        colorField("glow", &Theme::glow),
        colorField("rule", &Theme::rule),
        colorField("frameBorder", &Theme::frameBorder),
        colorField("onAccent", &Theme::onAccent),
        colorField("warning", &Theme::warning),
        colorField("onWarning", &Theme::onWarning),
        colorField("error", &Theme::error),
        colorField("onError", &Theme::onError),
        colorField("popup", &Theme::popup),
        colorField("scrim", &Theme::scrim),
        colorField("glass", &Theme::glass),
        colorField("plate", &Theme::plate),
        colorField("important", &Theme::important),
        numberField("controlHeight", &Theme::controlHeight),
        numberField("frameRounding", &Theme::frameRounding),
        numberField("frameBorderSize", &Theme::frameBorderSize),
        numberField("popupRounding", &Theme::popupRounding),
        numberField("titleFontSize", &Theme::titleFontSize),
        numberField("bodyFontSize", &Theme::bodyFontSize),
        numberField("detailFontSize", &Theme::detailFontSize),
        numberField("scrollbarSize", &Theme::scrollbarSize),
        numberField("grabWidth", &Theme::grabWidth),
        numberField("grabRounding", &Theme::grabRounding),
        numberField("disabledOpacity", &Theme::disabledOpacity),
        numberField("space1", &Theme::space1),
        numberField("space2", &Theme::space2),
        numberField("space3", &Theme::space3),
        numberField("space4", &Theme::space4),
        numberField("space6", &Theme::space6),
        numberField("space8", &Theme::space8),
        numberField("space12", &Theme::space12),
        numberField("panelBorderUnits", &Theme::panelBorderUnits),
        numberField("panelCornerUnits", &Theme::panelCornerUnits),
        numberField("panelPaddingUnits", &Theme::panelPaddingUnits),
    };

    constexpr std::size_t sizeOfFields()
    {
        std::size_t size = 0;
        for (const auto& field : THEME_FIELDS) {
            size += field.color ? sizeof(render::Color) : sizeof(float);
        }
        return size;
    }

    // a field of Theme the table lacks stops the build here
    static_assert(sizeOfFields() == sizeof(Theme), "a field of imgui::Theme is not in THEME_FIELDS");

    /**
     * The field of the table by its name, or null.
     */
    const ThemeField* findField(const std::string_view name)
    {
        const auto it = std::ranges::find(THEME_FIELDS, name, &ThemeField::name);
        return it == THEME_FIELDS.end() ? nullptr : &*it;
    }

    /**
     * A color from its JSON, [r, g, b, a] of integers from 0 to 255, or none in another shape.
     */
    std::optional<render::Color> colorOf(const nlohmann::json& value)
    {
        if (!value.is_array() || value.size() != 4) {
            return std::nullopt;
        }
        std::array<int, 4> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (!value[i].is_number_integer() || value[i].get<std::int64_t>() < 0 || value[i].get<std::int64_t>() > 255) {
                return std::nullopt;
            }
            bytes[i] = value[i].get<int>();
        }
        return render::Color::rgba(bytes[0], bytes[1], bytes[2], bytes[3]);
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

    /**
     * Every field of Theme, in its order, by its name and member: what reads or sets a theme field by field, as a test's
     * probe theme. A field added to Theme and not to the table stops the build.
     */
    std::span<const ThemeField> themeFields()
    {
        return THEME_FIELDS;
    }

    /**
     * The theme as JSON, each field by its name: a color as [r, g, b, a] from 0 to 255, a number as the shortest text
     * that reads back as its float, so 0.05 is not 0.0500000007.
     */
    nlohmann::json themeJson(const Theme& theme)
    {
        auto fields = nlohmann::json::object();
        for (const auto& field : THEME_FIELDS) {
            if (field.color) {
                fields[std::string(field.name)] = (theme.*field.color).bytes();
            } else {
                fields[std::string(field.name)] = std::stod(std::format("{}", theme.*field.number));
            }
        }
        return fields;
    }

    /**
     * The theme with the given fields set, an object of names and values in themeJson's shapes. A name that is no field,
     * a value of the wrong shape, or no object of fields is an error that names it, and no field is set.
     */
    std::expected<Theme, std::string> themeWithFields(const Theme& theme, const nlohmann::json& fields)
    {
        if (!fields.is_object() || fields.empty()) {
            return std::unexpected("the fields to set must be an object of at least one field and its value");
        }
        auto result = theme;
        for (const auto& [name, value] : fields.items()) {
            const auto* field = findField(name);
            if (!field) {
                return std::unexpected(std::format("the theme has no field '{}'", name));
            }
            if (field->color) {
                const auto color = colorOf(value);
                if (!color) {
                    return std::unexpected(std::format("field '{}' must be [r, g, b, a], each an integer from 0 to 255", name));
                }
                result.*field->color = *color;
            } else {
                if (!value.is_number() || !std::isfinite(value.get<double>())) {
                    return std::unexpected(std::format("field '{}' must be a number", name));
                }
                result.*field->number = value.get<float>();
            }
        }
        return result;
    }
}
