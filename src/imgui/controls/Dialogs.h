#pragma once

#include <span>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "Buttons.h"
#include "Controls.h"

// A dialog in the middle of the panel, and the buttons along its bottom.
namespace f4cf::imgui::controls
{
    /**
     * A button of a dialog: its label, its look, and whether it is disabled.
     */
    struct DialogButton
    {
        std::string_view label;
        ButtonLook look = ButtonLook::Quiet;
        bool disabled = false;
    };

    // a dialog's width, unless it is given another
    constexpr float DIALOG_WIDTH = 576.0f;

    // the least a dialog's button is, which is wider for a longer label
    constexpr float DIALOG_BUTTON_MIN_WIDTH = 112.0f;

    bool beginDialog(const char* id, bool asked, Tone tone, float width = DIALOG_WIDTH);
    int dialogButtons(std::span<const DialogButton> buttons, std::span<ImRect> boxes = {});
}
