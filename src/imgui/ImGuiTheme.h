#pragma once

#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>
#include <nlohmann/json_fwd.hpp>

#include "../render/Color.h"

// declared only, as their headers bring in Windows.h with its min and max, which the controls' code does not take
namespace f4cf::vrui
{
    struct UIPanelStyle;
}

namespace f4cf::imgui
{
    struct KeyboardStyle;

    /**
     * A mod's look in one value: the colors by role, the sizes, the font sizes and the spacing scale, which the controls,
     * ImGui's style of the main context, the keyboard and a panel's edge are drawn in. What it does not set is the shape
     * of a drawn part: a mark, an icon, an arrow. Sizes are in the canvas's pixels, the panel's in vrui units.
     *
     * Its defaults are the framework's own look, its green, as vrui::F4VR_PANEL_STYLE: a mod that sets no theme gets
     * them. A mod with a design sets every field from it, so a default that changes later does not change that mod.
     *
     *     controls::get().setTheme(myDesignTheme());
     *     panel->setStyle(controls::get().theme().panelStyle());
     */
    struct Theme
    {
        // the house color: text, icons, a selected or chosen fill, an outline under the ray
        render::Color accent = render::Color::rgba(10, 250, 120);

        // secondary text: headings, units, counts, a cell that is not chosen
        render::Color muted = render::Color::rgba(159, 224, 187);

        // the fill of a control's frame, as a slider's track
        render::Color track = render::Color::rgba(10, 250, 120, 26);

        // a row or a control under the ray, and while the trigger is held on it
        render::Color hover = render::Color::rgba(10, 250, 120, 31);
        render::Color pressed = render::Color::rgba(10, 250, 120, 51);

        // the outline while pressed, and a primary button under the ray
        render::Color glow = render::Color::rgba(196, 255, 220);

        // a heading's rule and a section's line
        render::Color rule = render::Color::rgba(10, 250, 120, 102);

        // a frame's outline at rest
        render::Color frameBorder = render::Color::rgba(10, 250, 120, 140);

        // text and icons on a solid accent fill
        render::Color onAccent = render::Color::rgba(4, 23, 12);

        // the warning tone and the text on its fill
        render::Color warning = render::Color::rgba(255, 191, 51);
        render::Color onWarning = render::Color::rgba(31, 20, 0);

        // the error tone and the text on its fill
        render::Color error = render::Color::rgba(255, 149, 133);
        render::Color onError = render::Color::rgba(36, 8, 5);

        // the background of a list or a dialog that opens over the panel
        render::Color popup = render::Color::rgba(11, 18, 14, 245);

        // what dims the panel behind a dialog
        render::Color scrim = render::Color::rgba(5, 8, 6, 140);

        // the background of a panel, and of the keyboard
        render::Color glass = render::Color::rgba(15, 15, 15, 166);

        // the fill of a navigation pane, so it reads as a fixed region
        render::Color plate = render::Color::rgba(10, 250, 120, 15);

        // a button the player must not miss
        render::Color important = render::Color::rgba(95, 212, 255);

        // every pressable control and row is this high
        float controlHeight = 48.0f;

        // the corner of a control's frame, and the width of its outline
        float frameRounding = 4.0f;
        float frameBorderSize = 2.0f;

        // the corner of a list or a dialog that opens over the panel
        float popupRounding = 8.0f;

        // the font sizes of a title, of the body text, which the canvas font is drawn at, and of a detail, as a tag's
        // text; the body size is clamped to MIN_FONT_SIZE_PIXELS..MAX_FONT_SIZE_PIXELS
        float titleFontSize = 26.0f;
        float bodyFontSize = 21.0f;
        float detailFontSize = 18.0f;

        // ImGui's scroll bar, and a slider's grab
        float scrollbarSize = 16.0f;
        float grabWidth = 20.0f;
        float grabRounding = 2.0f;

        // how much of its alpha a disabled row keeps
        float disabledOpacity = 0.4f;

        // the spacing scale: the gaps and paddings of ImGui's style are steps of it
        float space1 = 4.0f;
        float space2 = 8.0f;
        float space3 = 12.0f;
        float space4 = 16.0f;
        float space6 = 24.0f;
        float space8 = 32.0f;
        float space12 = 48.0f;

        // a panel's edge, corner and padding, in vrui units
        float panelBorderUnits = 0.05f;
        float panelCornerUnits = 0.2f;
        float panelPaddingUnits = 0.15f;

        void applyTo(ImGuiStyle& style) const;
        vrui::UIPanelStyle panelStyle() const;
        KeyboardStyle keyboardStyle(const KeyboardStyle& base) const;
    };

    /**
     * A field of Theme by its name as in the struct: a color or a number, by its member, of which the other is null. A
     * number set by name is in its range: no size is negative, a control and a font are at least a pixel, and an
     * opacity is at most 1, so a value ImGui cannot draw with is refused before it is drawn.
     */
    struct ThemeField
    {
        std::string_view name;
        render::Color Theme::* color = nullptr;
        float Theme::* number = nullptr;
        float least = 0.0f;
        float most = (std::numeric_limits<float>::max)();
    };

    std::span<const ThemeField> themeFields();

    nlohmann::json themeJson(const Theme& theme);
    std::expected<Theme, std::string> themeWithFields(const Theme& theme, const nlohmann::json& fields);
}
