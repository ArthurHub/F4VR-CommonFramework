#include "Dialogs.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Frames.h"
#include "Text.h"

namespace
{
    // the room inside a dialog, around its content
    constexpr float DIALOG_PADDING = 24.0f;

    // the spacing of a dialog's window, across and down
    constexpr ImVec2 DIALOG_ITEM_SPACING(8.0f, 16.0f);

    // between the buttons of a dialog, and how much further they are from its text than its text is from its title
    constexpr float DIALOG_BUTTON_GAP = 8.0f;

    // the room on each side of a dialog button's label
    constexpr float DIALOG_BUTTON_PADDING = 16.0f;
}

namespace f4cf::imgui::controls
{
    /**
     * Begins a dialog: a modal popup of ImGui in the middle of the display, which dims what is behind it and takes every
     * press, with a border in its tone's color, in the style's popup look. It is opened while it is asked, and closed as
     * it is not, as after Back: ImGui opens and closes a popup inside a frame. Returns whether its content is drawn,
     * which ends with `ImGui::EndPopup`.
     */
    bool beginDialog(const char* id, const bool asked, const Tone tone, const float width)
    {
        const auto popup = ImGui::GetID(id);
        if (asked && !ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None)) {
            ImGui::OpenPopupEx(popup);
        }
        if (!ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None)) {
            return false;
        }

        const auto& style = get().style();
        const auto& colors = get().colors();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(DIALOG_PADDING, DIALOG_PADDING));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, DIALOG_ITEM_SPACING);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, style.popupRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, style.frameBorderSize);
        ImGui::PushStyleColor(ImGuiCol_Border, get().tone(tone).color);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, colors.popup);
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, colors.scrim);
        const bool shown = ImGui::BeginPopupModal(id,
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(4);
        if (!shown) {
            return false;
        }
        if (!asked) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return false;
        }
        return true;
    }

    /**
     * The buttons of a dialog, from its right in their order, a little further from its text than that is from its
     * title. A button is as wide as its label with its padding, and no less than `DIALOG_BUTTON_MIN_WIDTH`. Each one's
     * box is put in `boxes`, where it has room. Returns the place of the one that was pressed, and -1 for none.
     */
    int dialogButtons(const std::span<const DialogButton> buttons, const std::span<ImRect> boxes)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + DIALOG_BUTTON_GAP);
        const auto pos = ImGui::GetCursorScreenPos();
        float right = pos.x + ImGui::GetContentRegionAvail().x;
        int pressed = -1;
        for (std::size_t place = 0; place < buttons.size(); ++place) {
            const auto& dialogButton = buttons[place];
            const float width = (std::max)(DIALOG_BUTTON_MIN_WIDTH, std::ceil(textWidth(dialogButton.label) + DIALOG_BUTTON_PADDING * 2.0f));
            const auto box = controlBox(right - width, pos.y, width);
            if (place < boxes.size()) {
                boxes[place] = box;
            }
            right -= width + DIALOG_BUTTON_GAP;
            ImGui::BeginDisabled(dialogButton.disabled);
            if (button(std::string(dialogButton.label).c_str(), box, dialogButton.look)) {
                pressed = static_cast<int>(place);
            }
            ImGui::EndDisabled();
        }
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, get().style().controlHeight));
        return pressed;
    }
}
