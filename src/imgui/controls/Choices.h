#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "Controls.h"

// The stepper, the dropdown, and the list of options a dropdown opens.
namespace f4cf::imgui::controls
{
    /**
     * What the row a stepper or a dropdown is in tells of the pointer in this frame. The row is the item the trigger
     * presses; a stepper reads from the pointer's place whether a press is on a button.
     */
    struct ChoiceRow
    {
        // the ray is on the row
        bool pointed = false;

        // the trigger is held since a pull on the row, and was let go on it in this frame
        bool held = false;
        bool pressed = false;
    };

    /**
     * The width of a stepper's box of its option, between its buttons.
     */
    struct StepperLayout
    {
        float optionWidth = 280.0f;
    };

    /**
     * What an open list of options did in this frame: the option a press picked, the one the ray is on, the box of each
     * option and then of the action entry, and the scroll of its options. All empty for a list that is not shown.
     */
    struct ListDrawn
    {
        std::optional<std::size_t> picked;
        std::optional<std::size_t> pointed;
        std::vector<ImRect> boxes;
        bool shown = false;
        float scrollY = 0.0f;
        float scrollMaxY = 0.0f;
    };

    // draws more of an option of a list, in its box, after its text: whether it is the chosen one says its colors
    using OptionDecoration = std::function<void(std::size_t option, const ImRect& box, bool chosen)>;

    /**
     * What a list of options has beside its options' texts: a muted text after each, as a file's extension, a line over
     * them that is not pressed, with a text at its left and one at its right, as a folder and how many files it has, and
     * an entry after them that does something other than pick, as typing a new value.
     */
    struct ListExtras
    {
        // one for each option, and none for a list with none
        std::span<const std::string_view> suffixes;

        // the line over the options: none while both are empty
        std::string_view headerLeft;
        std::string_view headerRight;

        // the entry after the options, set apart by a rule: none while empty. A press on it picks the place after the
        // options
        std::string_view action;
    };

    // the options a list shows at most: one with more scrolls
    constexpr std::size_t LIST_MAX_OPTIONS = 5;

    std::optional<std::size_t> stepperEdit(float right, float top, const ChoiceRow& row, std::optional<std::size_t> chosen, std::size_t count, bool noneStepsBackToLast = false,
        const StepperLayout& layout = {});
    float drawStepper(float right, float top, const ChoiceRow& row, std::string_view text, Tone tone, std::optional<std::size_t> chosen, std::size_t count,
        bool noneStepsBackToLast = false, const StepperLayout& layout = {});
    void drawDropdown(const ImRect& box, std::string_view text, std::string_view suffix, Tone tone, bool listOpen);
    bool listOption(std::string_view text, bool chosen, float width, std::string_view suffix = {});
    bool listAction(std::string_view text, float width);
    ListDrawn drawList(ImGuiID id, const ImRect& box, std::span<const std::string_view> texts, std::optional<std::size_t> chosen, const OptionDecoration& decorate = {},
        const ListExtras& extras = {});
}
