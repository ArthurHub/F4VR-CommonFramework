#include "Choices.h"

#include <algorithm>

#include "Frames.h"
#include "Marks.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // between a stepper's buttons and the box of its option, and how far beside a button a press is still on it
    constexpr float STEPPER_GAP = 8.0f;
    constexpr float BUTTON_SLOP = 4.0f;

    // the room between the box of a stepper's option, or of a dropdown, and its text
    constexpr float STEPPER_TEXT_PADDING = 12.0f;
    constexpr float DROPDOWN_TEXT_PADDING = 16.0f;
    constexpr float DROPDOWN_ARROW_GAP = 8.0f;

    // the options of a list are this far from each other, and from the edge of the list
    constexpr float LIST_GAP = 4.0f;
    constexpr float LIST_PADDING = 8.0f;

    // the entry that does something other than pick is this much further from the options, with a rule in the middle
    constexpr float ACTION_GAP = 4.0f;

    // an option's text is in from its edges: the room of the check before it, and after it
    constexpr float OPTION_PADDING = 12.0f;
    constexpr float OPTION_END_PADDING = 16.0f;

    // on the chosen option's solid fill, the muted text is the color on the accent at this alpha, of 255
    constexpr ImU32 MUTED_ON_ACCENT_ALPHA = 153;

    // the line over the options of a list: its text in the detail size, with this room above and under it
    constexpr float HEADER_ABOVE = 4.0f;
    constexpr float HEADER_UNDER = 8.0f;
    constexpr float HEADER_INSET = 8.0f;
    constexpr float HEADER_GAP = 12.0f;

    float headerHeight()
    {
        return HEADER_ABOVE + get().style().detailFontSize + HEADER_UNDER;
    }

    /**
     * Where the parts of a stepper that ends at the right are: the box of its option, between the button to the option
     * before it and the one to the option after.
     */
    struct StepperBoxes
    {
        ImRect next;
        ImRect option;
        ImRect previous;

        StepperBoxes(const float right, const float top, const StepperLayout& layout)
            : next(controlBox(right - get().style().controlHeight, top, get().style().controlHeight)),
              option(controlBox(next.Min.x - STEPPER_GAP - layout.optionWidth, top, layout.optionWidth)),
              previous(controlBox(option.Min.x - STEPPER_GAP - get().style().controlHeight, top, get().style().controlHeight))
        {}

        /**
         * Whether a place of the row is on a button. A button is pressed a little beside it too, and in the gap between
         * it and the box of the option.
         */
        bool onPrevious(const float x) const
        {
            return x >= previous.Min.x - BUTTON_SLOP && x < option.Min.x;
        }

        bool onNext(const float x) const
        {
            return x >= option.Max.x && x < next.Max.x + BUTTON_SLOP;
        }
    };

    /**
     * Where the list of a dropdown is, by the box of the dropdown and the number of its entries: as wide as the box, and
     * as high as its entries, up to the most it shows, its header, and the room of the rule over an entry that does
     * something other than pick, placed as `popupTop` places it in the window it is in.
     */
    ImRect listBox(const ImRect& box, const std::size_t entries, const bool header, const bool action)
    {
        const auto& style = get().style();
        const auto& window = ImGui::GetCurrentWindow()->InnerRect;
        const auto shown = static_cast<float>((std::min)(entries, LIST_MAX_OPTIONS));
        const float headerRoom = header ? headerHeight() + style.frameBorderSize + LIST_GAP : 0.0f;
        const float actionRoom = action ? ACTION_GAP : 0.0f;
        const float height = (std::min)(shown * (style.controlHeight + LIST_GAP) - LIST_GAP + LIST_PADDING * 2.0f + headerRoom + actionRoom, window.GetHeight());
        const float top = popupTop(box, height, window);
        return { box.Min.x, top, box.Max.x, top + height };
    }
}

namespace f4cf::imgui::controls
{
    /**
     * The option the player picks in this frame for a stepper that ends at the right, by its place among the options,
     * and none where they pick none. It has an option at least.
     * A press on a button goes to the option before or after, and stops at the first and the last. With no option
     * chosen, the one after is the first, and the one before is the last where `noneStepsBackToLast`. A press anywhere
     * else on its row goes to the option after, and from the last to the first. A push of the thumbstick to a side, while
     * the ray is on the row, goes to the option before or after as the buttons do: once for each push.
     */
    std::optional<std::size_t> stepperEdit(const float right, const float top, const ChoiceRow& row, const std::optional<std::size_t> chosen, const std::size_t count,
        const bool noneStepsBackToLast, const StepperLayout& layout)
    {
        const StepperBoxes boxes(right, top, layout);
        const int step = get().choices().step;
        const auto after = chosen ? *chosen + 1 : 0;
        const float x = ImGui::GetIO().MousePos.x;
        const bool pushed = row.pointed && step != 0;
        if ((!row.pressed && !pushed) || count == 0) {
            return std::nullopt;
        }
        if (row.pressed ? boxes.onPrevious(x) : step < 0) {
            if (!chosen && noneStepsBackToLast) {
                return count - 1;
            }
            return chosen && *chosen > 0 ? std::optional(*chosen - 1) : std::nullopt;
        }
        if (row.pressed ? boxes.onNext(x) : step > 0) {
            return after < count ? std::optional(after) : std::nullopt;
        }
        return after % count;
    }

    /**
     * A stepper that ends at the right: the text of its option in a box in its tone's color, between a button to the
     * option before it and one to the option after, each disabled where it goes nowhere. Returns its left edge.
     */
    float drawStepper(const float right, const float top, const ChoiceRow& row, const std::string_view text, const Tone tone, const std::optional<std::size_t> chosen,
        const std::size_t count, const bool noneStepsBackToLast, const StepperLayout& layout)
    {
        const StepperBoxes boxes(right, top, layout);
        const float x = ImGui::GetIO().MousePos.x;
        const bool onPrevious = boxes.onPrevious(x);
        const bool onNext = boxes.onNext(x);
        const auto after = chosen ? *chosen + 1 : 0;
        const bool hasPrevious = chosen ? *chosen > 0 : noneStepsBackToLast;
        drawStepButton(boxes.previous, Arrow::Left, hasPrevious, row.pointed && onPrevious, row.held && onPrevious);
        drawFrame(boxes.option, tone);
        drawTextIn(boxes.option, text, get().tone(tone).color, 0.5f, STEPPER_TEXT_PADDING);
        drawStepButton(boxes.next, Arrow::Right, after < count, row.pointed && onNext, row.held && onNext);
        return boxes.previous.Min.x;
    }

    /**
     * A dropdown in its box: the text of its option at its left in its tone's color, with a muted suffix after it, as a
     * file's extension, and an arrow at its right. Its outline is in the accent while its list is open. It only draws:
     * the caller opens its list.
     */
    void drawDropdown(const ImRect& box, const std::string_view text, const std::string_view suffix, const Tone tone, const bool listOpen)
    {
        const auto& colors = get().colors();
        const float arrowLeft = box.Max.x - DROPDOWN_TEXT_PADDING - ARROW_SIZE;
        drawFrame(box, tone);
        if (listOpen) {
            drawOutline(box.Min, box.Max, colors.accent);
        }
        drawTextWithSuffix(box.Min.x + DROPDOWN_TEXT_PADDING, arrowLeft - DROPDOWN_ARROW_GAP, box.Min.y, box.GetHeight(), get().tone(tone).color, text, colors.muted, suffix);
        drawControlArrow(ImVec2(arrowLeft + ARROW_SIZE * 0.5f, box.GetCenter().y), Arrow::Down);
    }

    /**
     * An option of the list that is open, at the cursor: the mark of the chosen one, and its text, with a muted text after
     * it, as a file's extension. The chosen option is a solid accent fill, and any other has the look of a row under the
     * ray. Returns whether it was pressed.
     */
    bool listOption(const std::string_view text, const bool chosen, const float width, const std::string_view suffix)
    {
        const auto& style = get().style();
        const auto& colors = get().colors();
        const auto pos = ImGui::GetCursorScreenPos();
        const ImVec2 max(pos.x + width, pos.y + style.controlHeight);
        const bool pressed = ImGui::InvisibleButton("option", ImVec2(width, style.controlHeight));
        auto color = colors.accent;
        auto muted = colors.muted;
        if (chosen) {
            ImGui::GetWindowDrawList()->AddRectFilled(pos, max, ImGui::GetColorU32(colors.accent), style.frameRounding);
            drawCheck(ImVec2(pos.x + OPTION_PADDING, pos.y + (style.controlHeight - CHECK_SIZE) * 0.5f), colors.onAccent);
            color = colors.onAccent;
            muted = (colors.onAccent & ~IM_COL32_A_MASK) | (MUTED_ON_ACCENT_ALPHA << IM_COL32_A_SHIFT);
        } else {
            drawRowState();
        }
        drawTextWithSuffix(pos.x + OPTION_PADDING + CHECK_SIZE + OPTION_PADDING, max.x - OPTION_END_PADDING, pos.y, style.controlHeight, color, text, muted, suffix);
        return pressed;
    }

    /**
     * The entry of the list that is open that does something other than pick, as typing a new value, at the cursor: its
     * text where an option's is, muted, and in the accent with the look of a row under the ray. Returns whether it was
     * pressed.
     */
    bool listAction(const std::string_view text, const float width)
    {
        const auto& style = get().style();
        const auto& colors = get().colors();
        const auto pos = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("action", ImVec2(width, style.controlHeight));
        drawRowState();
        const auto color = ImGui::IsItemHovered() || ImGui::IsItemActive() ? colors.accent : colors.muted;
        drawText(pos.x + OPTION_PADDING + CHECK_SIZE + OPTION_PADDING, pos.x + width - OPTION_END_PADDING, pos.y, style.controlHeight, color, text);
        return pressed;
    }

    /**
     * The list of options of a dropdown that is open, by its popup's id, at the box of its dropdown, inside the window it
     * is drawn in. The caller opens it with `ImGui::OpenPopupEx(id)`. One with many options scrolls, from the chosen option
     * as it opens. A press on an option picks it and closes the list, and a press beside it closes it and does nothing
     * else. An option can have more drawn after its text, and a muted text, and the list a line over its options, which
     * stays as they scroll, and an entry after them that does something other than pick, under a rule, which is the place
     * after the options. Back closes it (`Controls::back`). Nothing for a list that is not open.
     */
    ListDrawn drawList(const ImGuiID id, const ImRect& box, const std::span<const std::string_view> texts, const std::optional<std::size_t> chosen,
        const OptionDecoration& decorate, const ListExtras& extras)
    {
        if (!ImGui::IsPopupOpen(id, ImGuiPopupFlags_None)) {
            return {};
        }
        const auto& style = get().style();
        const auto& colors = get().colors();
        const bool header = !extras.headerLeft.empty() || !extras.headerRight.empty();
        const bool action = !extras.action.empty();
        const auto list = listBox(box, texts.size() + (action ? 1 : 0), header, action);
        ImGui::SetNextWindowPos(list.Min);
        ImGui::SetNextWindowSize(list.GetSize());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(LIST_PADDING, LIST_PADDING));
        ImGui::PushStyleColor(ImGuiCol_Border, colors.accent);
        const bool shown = ImGui::BeginPopupEx(id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!shown) {
            return {};
        }
        // the level the list is at among the open popups, which Back closes it to
        get().listShown(id, ImGui::GetCurrentContext()->BeginPopupStack.Size - 1);

        ListDrawn drawn{ .shown = true };
        drawn.boxes.reserve(texts.size() + (action ? 1 : 0));
        const bool appearing = ImGui::IsWindowAppearing();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, LIST_GAP));
        // the header stays at the top, and the options scroll under it, in a window of their own
        if (header) {
            const auto pos = ImGui::GetCursorScreenPos();
            const float headerWidth = ImGui::GetContentRegionAvail().x;
            ImGui::Dummy(ImVec2(headerWidth, headerHeight() + style.frameBorderSize));
            ImGui::PushFont(nullptr, style.detailFontSize);
            const float rightWidth = textWidth(extras.headerRight);
            const float textTop = pos.y + HEADER_ABOVE;
            const float textRight = pos.x + headerWidth - HEADER_INSET;
            drawText(pos.x + HEADER_INSET, textRight - rightWidth - HEADER_GAP, textTop, style.detailFontSize, colors.muted, extras.headerLeft);
            drawText(textRight - rightWidth, textRight, textTop, style.detailFontSize, colors.muted, extras.headerRight);
            ImGui::PopFont();
            const float ruleY = pos.y + headerHeight() + style.frameBorderSize * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, ruleY), ImVec2(pos.x + headerWidth, ruleY), colors.rule, style.frameBorderSize);
            ImGui::BeginChild("options");
        }
        // the options end a little before a scroll bar
        const float width = ImGui::GetContentRegionAvail().x - (ImGui::GetCurrentWindow()->ScrollbarY ? LIST_PADDING : 0.0f);
        for (std::size_t i = 0; i < texts.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (listOption(texts[i], chosen == i, width, i < extras.suffixes.size() ? extras.suffixes[i] : std::string_view())) {
                drawn.picked = i;
                ImGui::CloseCurrentPopup();
            }
            const ImRect optionBox(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            drawn.boxes.push_back(optionBox);
            if (decorate) {
                decorate(i, optionBox, chosen == i);
            }
            if (ImGui::IsItemHovered()) {
                drawn.pointed = i;
            }
            if (chosen == i && appearing) {
                ImGui::SetScrollHereY();
            }
            ImGui::PopID();
        }
        if (action) {
            ImGui::Dummy(ImVec2(0.0f, ACTION_GAP - LIST_GAP));
            const auto pos = ImGui::GetCursorScreenPos();
            const float ruleY = pos.y - (LIST_GAP + ACTION_GAP) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, ruleY), ImVec2(pos.x + width, ruleY), colors.rule, style.frameBorderSize);
            if (listAction(extras.action, width)) {
                drawn.picked = texts.size();
                ImGui::CloseCurrentPopup();
            }
            drawn.boxes.emplace_back(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (ImGui::IsItemHovered()) {
                drawn.pointed = texts.size();
            }
        }
        // the options scroll in their child under a header, and in the popup with none
        drawn.scrollY = ImGui::GetScrollY();
        drawn.scrollMaxY = ImGui::GetScrollMaxY();
        if (header) {
            ImGui::EndChild();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
        return drawn;
    }
}
