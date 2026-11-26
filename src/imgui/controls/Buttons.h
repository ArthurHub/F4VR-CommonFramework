#pragma once

#include <cstdint>
#include <optional>

#include <imgui.h>
#include <imgui_internal.h>

#include "Controls.h"

// The buttons and the switcher of the controls.
namespace f4cf::imgui::controls
{
    /**
     * How a button looks.
     */
    enum class ButtonLook : std::uint8_t
    {
        // the main action, as Save: a solid accent fill
        Primary,
        // another action, as Discard: an outline
        Secondary,
        // a way out, as Cancel: no outline until the ray is on it
        Quiet,
        // the confirming button of a dangerous confirmation: a solid error fill
        Danger,
    };

    // a cell of a switcher, which has two
    constexpr float SWITCHER_CELL_WIDTH = 88.0f;

    bool button(const char* label, const ImRect& box, ButtonLook look);
    bool iconButton(const char* id, const ImRect& box, bool& pointed);
    float drawSwitcher(float right, float top, std::optional<bool> on, Tone tone, const char* offLabel = "Off", const char* onLabel = "On");
}
