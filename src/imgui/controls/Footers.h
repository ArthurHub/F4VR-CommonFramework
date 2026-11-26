#pragma once

#include <span>

#include <imgui.h>
#include <imgui_internal.h>

#include "Buttons.h"

// The footer of a panel: a rule, its buttons at the right, and the room of a help line at their left.
namespace f4cf::imgui::controls
{
    /**
     * A button of a footer, as Save or Discard.
     */
    struct FooterButton
    {
        const char* label = "";
        ButtonLook look = ButtonLook::Secondary;
        bool disabled = false;
    };

    /**
     * What a footer gave in this frame.
     */
    struct FooterDrawn
    {
        // the place of the pressed button, -1 for none
        int pressed = -1;

        // the room at the left of the buttons, for a help line
        ImRect left;
    };

    FooterDrawn footer(const ImVec2& pos, std::span<const FooterButton> buttons, float height = 72.0f, float buttonWidth = 160.0f);
}
