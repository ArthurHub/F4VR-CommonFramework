#include "Labelled.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include <imgui_internal.h>

#include "Buttons.h"
#include "Choices.h"
#include "Frames.h"
#include "Rows.h"
#include "Targets.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    /**
     * A labelled row while its control is edited and drawn: it pushes the label's ID, adds the row at the cursor, and is
     * disabled as it is told. As it ends, a row that is shown is a `row` target by its label, and the row is ImGui's last
     * item again, so the caller asks ImGui whether the ray is on it.
     */
    class LabelledRow
    {
    public:
        LabelledRow(const std::string_view label, const Labelled& look, const bool pressable)
            : _label(label),
              _look(look)
        {
            ImGui::PushID(label.data(), label.data() + label.size());
            pos = ImGui::GetCursorScreenPos();
            right = pos.x + rowWidth();
            row = addRow(pressable && !look.disabled);
            enabled = !look.disabled;
            _item = ImGui::GetCurrentContext()->LastItemData;
            ImGui::BeginDisabled(look.disabled);
        }

        LabelledRow(const LabelledRow&) = delete;
        LabelledRow& operator=(const LabelledRow&) = delete;

        ~LabelledRow()
        {
            ImGui::EndDisabled();
            if (row.shown) {
                targets().add(target::ROW, _label, ImRect(pos, ImVec2(right, pos.y + get().theme().controlHeight)));
            }
            ImGui::GetCurrentContext()->LastItemData = _item;
            ImGui::PopID();
        }

        /**
         * The label, from the row's left to `space3` before its control.
         */
        void drawLabel(const float controlLeft) const
        {
            drawRowLabel(pos, controlLeft - get().theme().space3, _label, _look.mark, _look.found);
        }

        ImVec2 pos;
        float right = 0.0f;
        Row row;

        // the row is pressed, pointed and held only while it is enabled
        bool enabled = true;

    private:
        std::string_view _label;
        const Labelled& _look;
        ImGuiLastItemData _item;
    };

    /**
     * The box of a control that fills the column, at the right of its row.
     */
    ImRect columnBox(const LabelledRow& labelled)
    {
        return controlBox(labelled.right - CONTROL_COLUMN_WIDTH, labelled.pos.y, CONTROL_COLUMN_WIDTH);
    }

    /**
     * The widths of a slider with its value's box that fills the column.
     */
    SliderLayout columnSlider()
    {
        constexpr SliderLayout DEFAULT;
        return { .trackWidth = CONTROL_COLUMN_WIDTH - get().theme().space2 - DEFAULT.valueWidth, .valueWidth = DEFAULT.valueWidth };
    }

    /**
     * The width of a stepper's box of its option, so the stepper with its buttons fills the column.
     */
    StepperLayout columnStepper()
    {
        const auto& theme = get().theme();
        return { .optionWidth = CONTROL_COLUMN_WIDTH - (theme.controlHeight + theme.space2) * 2.0f };
    }

    /**
     * The option a place among options is, none for one that is past them.
     */
    std::optional<std::size_t> optionAt(const std::size_t chosen, const std::size_t count)
    {
        return chosen < count ? std::optional(chosen) : std::nullopt;
    }
}

namespace f4cf::imgui::controls
{
    /**
     * A row's label alone, for a mod's own row: its text `headingTextInset()` in from the row's left in the accent, cut
     * with an ellipsis before its mark, which follows it `space2` after, so the mark always shows. The ranges a search
     * found are underlined. Returns the width it drew, from the inset to the end of the mark.
     */
    float drawRowLabel(const ImVec2& pos, const float right, const std::string_view text, const Mark mark, const std::span<const TextRange> found)
    {
        const auto& theme = get().theme();
        const auto color = get().colors().accent;
        const float left = pos.x + headingTextInset();
        const float textRight = mark != Mark::None ? right - theme.space2 - MARK_WIDTH : right;
        drawText(left, textRight, pos.y, theme.controlHeight, color, text);
        drawUnderlines(left, textRight, pos.y, theme.controlHeight, color, text, found);
        float width = text.empty() ? 0.0f : (std::max)((std::min)(textWidth(text), textRight - left), 0.0f);
        if (mark != Mark::None) {
            const float markLeft = left + width + (width > 0.0f ? theme.space2 : 0.0f);
            drawMark(mark, ImVec2(markLeft + MARK_WIDTH * 0.5f, pos.y + theme.controlHeight * 0.5f));
            width = markLeft + MARK_WIDTH - left;
        }
        return width;
    }

    /**
     * A row of a switcher at the cursor, its label at the left and the switcher at its right: a press anywhere on the
     * row turns it, with its pulse. Returns whether it changed, which it has written.
     */
    bool labelledSwitcher(const std::string_view label, bool& on, const Labelled& row)
    {
        const LabelledRow labelled(label, row, true);
        const bool changed = labelled.row.pressed;
        if (changed) {
            on = !on;
            get().pulse();
        }
        if (labelled.row.shown) {
            labelled.drawLabel(drawSwitcher(labelled.right, labelled.pos.y, on, row.tone));
        }
        return changed;
    }

    /**
     * A row of a slider at the cursor, with the box of its value, filling the column at its right: `sliderEdit` and
     * `drawSlider` in one. A drag is written as it is let go, and a step of an arrow or the thumbstick at once, each
     * on a step of the range. The value's text has its decimals and its unit. Returns whether it changed, which it has
     * written.
     */
    bool labelledSlider(const std::string_view label, float& value, const SliderRange& range, const SliderText& text, const Labelled& row)
    {
        const LabelledRow labelled(label, row, true);
        const auto layout = columnSlider();
        const auto id = ImGui::GetID("slider");
        const SliderRow sliderRow{ .pointed = labelled.enabled && labelled.row.pointed, .started = labelled.row.started, .held = labelled.row.held };
        const auto edit = sliderEdit(id, labelled.right, labelled.pos.y, sliderRow, value, range, layout);
        if (edit.pulse) {
            get().pulse();
        }
        if (edit.write) {
            value = edit.value;
        }
        if (labelled.row.shown) {
            auto number = std::format("{:.{}f}", edit.value, (std::max)(text.decimals, 0));
            auto withUnit = number + std::string(text.unit);
            const auto shown = fitValueText(std::move(withUnit), std::move(number), layout);
            labelled.drawLabel(drawSlider(id, labelled.right, labelled.pos.y, sliderRow, edit.value, range, row.tone, shown, layout));
        }
        return edit.write;
    }

    /**
     * A row of a stepper at the cursor, filling the column at its right: its buttons go to the option before and after,
     * a press elsewhere on the row to the option after, and the thumbstick steps it, each with its pulse. Returns
     * whether it changed, which it has written.
     */
    bool labelledStepper(const std::string_view label, std::size_t& chosen, const std::span<const std::string_view> options, const Labelled& row)
    {
        const LabelledRow labelled(label, row, true);
        const auto layout = columnStepper();
        const ChoiceRow choiceRow{ .pointed = labelled.enabled && labelled.row.pointed, .held = labelled.row.held, .pressed = labelled.row.pressed };
        const auto current = optionAt(chosen, options.size());
        const auto picked = stepperEdit(labelled.right, labelled.pos.y, choiceRow, current, options.size(), false, layout);
        const bool changed = picked && picked != current;
        if (changed) {
            chosen = *picked;
            get().pulse();
        }
        if (labelled.row.shown) {
            const auto now = optionAt(chosen, options.size());
            const auto text = now ? options[*now] : std::string_view();
            labelled.drawLabel(drawStepper(labelled.right, labelled.pos.y, choiceRow, text, row.tone, now, options.size(), false, layout));
        }
        return changed;
    }

    /**
     * A row of a dropdown at the cursor, filling the column at its right: a press on the row opens its list, with its
     * pulse, and a press on an option picks it, with its pulse where it is another. The list is drawn while it is open,
     * also with the row scrolled out. Returns whether it changed, which it has written.
     */
    bool labelledDropdown(const std::string_view label, std::size_t& chosen, const std::span<const std::string_view> options, const Labelled& row)
    {
        const LabelledRow labelled(label, row, true);
        const auto box = columnBox(labelled);
        const auto id = ImGui::GetID("list");
        if (labelled.row.pressed) {
            ImGui::OpenPopupEx(id);
            get().pulse();
        }
        const auto current = optionAt(chosen, options.size());
        const auto drawn = drawList(id, box, options, current);
        const bool changed = drawn.picked && drawn.picked != current;
        if (changed) {
            chosen = *drawn.picked;
            get().pulse();
        }
        if (labelled.row.shown) {
            const auto now = optionAt(chosen, options.size());
            drawDropdown(box, now ? options[*now] : std::string_view(), {}, row.tone, ImGui::IsPopupOpen(id, ImGuiPopupFlags_None));
            labelled.drawLabel(box.Min.x);
        }
        return changed;
    }

    /**
     * A row of a text input at the cursor, filling the column at its right: a press on the row starts its edit, with
     * its pulse, and the framework's keyboard types it. It is called for the row scrolled out too, as an edit lasts
     * there. Returns whether it changed, which it has written: as the edit ends, or as it is typed for a live one.
     */
    bool labelledText(const std::string_view label, std::string& text, const TextOptions& options, const Labelled& row)
    {
        ImGui::PushID(label.data(), label.data() + label.size());
        const auto id = ImGui::GetID("text");
        ImGui::PopID();
        // while it is edited the field is drawn by its edit, and a press on the row is the field's
        const bool edited = get().isTextEdited(id);
        const LabelledRow labelled(label, row, !edited);
        const auto box = columnBox(labelled);
        const auto edit = textEdit(id, box, labelled.row.pressed, text, options);
        if (edit.pulse) {
            get().pulse();
        }
        const bool changed = edit.write.has_value();
        if (changed) {
            text = *edit.write;
        }
        if (labelled.row.shown) {
            if (!edited) {
                drawTextInput(box, text, row.tone);
            }
            labelled.drawLabel(box.Min.x);
        }
        return changed;
    }

    /**
     * A row of a button at the cursor: the button at its right, as wide as its icon and its text, in the tone's color.
     * The whole row is pressed, with its pulse. Returns whether it was pressed.
     */
    bool labelledButton(const std::string_view label, const std::string_view buttonText, const Labelled& row, const Icon icon)
    {
        const LabelledRow labelled(label, row, true);
        const bool pressed = labelled.row.pressed;
        if (pressed) {
            get().pulse();
        }
        if (labelled.row.shown) {
            const auto& theme = get().theme();
            const float iconRoom = icon != Icon::None ? ICON_SIZE + theme.space2 : 0.0f;
            const float width = std::ceil(iconRoom + textWidth(buttonText) + theme.space4 * 2.0f);
            const auto box = controlBox(labelled.right - width, labelled.pos.y, width);
            const auto color = get().tone(row.tone).color;
            drawButtonFrame(box, labelled.enabled && labelled.row.pointed, labelled.enabled && labelled.row.held);
            const float left = box.Min.x + theme.space4;
            if (icon != Icon::None) {
                drawIcon(icon, ImVec2(left + ICON_SIZE * 0.5f, box.GetCenter().y), color);
            }
            drawText(left + iconRoom, box.Max.x - theme.space4, box.Min.y, box.GetHeight(), color, buttonText);
            labelled.drawLabel(box.Min.x);
        }
        return pressed;
    }

    /**
     * A row of a value that is only shown at the cursor: its text at the right of the row in the tone's color, cut with
     * an ellipsis where it is wider than the column.
     */
    void labelledValue(const std::string_view label, const std::string_view value, const Labelled& row)
    {
        const LabelledRow labelled(label, row, false);
        if (!labelled.row.shown) {
            return;
        }
        const auto& theme = get().theme();
        const float width = (std::min)(textWidth(value) + theme.space3, CONTROL_COLUMN_WIDTH);
        const auto box = controlBox(labelled.right - width, labelled.pos.y, width);
        drawText(box.Min.x, box.Max.x - theme.space3, box.Min.y, box.GetHeight(), get().tone(row.tone).color, value);
        labelled.drawLabel(box.Min.x);
    }
}
