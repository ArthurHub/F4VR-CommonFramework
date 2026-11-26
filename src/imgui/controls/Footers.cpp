#include "Footers.h"

#include "Controls.h"
#include "Frames.h"

namespace f4cf::imgui::controls
{
    /**
     * A footer from its top left, as wide as the room left: a rule in the theme's `rule` over it, and its buttons at the
     * right in their order, the last `space2` inside the right edge, `space3` apart, in the middle of its height. A
     * disabled button is drawn and not pressed. It takes its room, and gives the room at the left of the buttons, from
     * `space4` in to `space3` before the first, for a help line. Each button is a `button` target by its label.
     */
    FooterDrawn footer(const ImVec2& pos, const std::span<const FooterButton> buttons, const float height, const float buttonWidth)
    {
        const auto& theme = get().theme();
        ImGui::SetCursorScreenPos(pos);
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + theme.frameBorderSize), ImGui::GetColorU32(get().colors().rule));

        FooterDrawn drawn;
        const float top = pos.y + (height - theme.controlHeight) * 0.5f;
        // each button's left from the right edge, each from the one at its right
        const auto leftOf = [&](const std::size_t place) {
            float left = pos.x + width - theme.space2 - buttonWidth;
            for (std::size_t after = buttons.size() - 1; after > place; --after) {
                left = left - theme.space3 - buttonWidth;
            }
            return left;
        };
        for (std::size_t place = 0; place < buttons.size(); ++place) {
            const auto& button = buttons[place];
            ImGui::BeginDisabled(button.disabled);
            if (controls::button(button.label, controlBox(leftOf(place), top, buttonWidth), button.look)) {
                drawn.pressed = static_cast<int>(place);
            }
            ImGui::EndDisabled();
        }

        const float buttonsLeft = buttons.empty() ? pos.x + width : leftOf(0);
        drawn.left = ImRect(pos.x + theme.space4, pos.y, buttonsLeft - theme.space3, pos.y + height);
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(width, height));
        return drawn;
    }
}
