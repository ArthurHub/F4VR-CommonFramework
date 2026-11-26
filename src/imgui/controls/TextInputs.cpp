#include "TextInputs.h"

#include <format>

#include <imgui_stdlib.h>

#include "Frames.h"
#include "Text.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // the name of the ImGui field of a text input, under the text input's id
    constexpr const char* FIELD_ID = "##textField";

    /**
     * What ImGui's field asks of a text input as it is typed. One that takes a number keeps only the characters of
     * one, and returns 1 for one it drops. A text with a most of characters is cut after its last character that
     * fits, as it is edited, so neither a key nor a paste takes it past its most.
     */
    int fieldCallback(ImGuiInputTextCallbackData* data)
    {
        const auto* options = static_cast<const TextOptions*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
            const auto c = data->EventChar;
            const bool kept = (c >= '0' && c <= '9') || c == '-' || (c == '.' && options->chars == TextChars::Decimal);
            return kept ? 0 : 1;
        }
        int length = 0;
        for (int place = 0; place < data->BufTextLen; ++place) {
            // a byte that starts a character of UTF-8
            if ((static_cast<unsigned char>(data->Buf[place]) & 0xC0) != 0x80 && ++length > options->maxLength) {
                data->DeleteChars(place, data->BufTextLen - place);
                break;
            }
        }
        return 0;
    }

    /**
     * The count of a text's characters of its most, as `12 / 32`.
     */
    std::string countText(const int length, const int most)
    {
        return std::format("{} / {}", length, most);
    }
}

namespace f4cf::imgui::controls
{
    /**
     * Edits a text input in a box: a press starts the edit, and ImGui's field takes it from the next frame, with the
     * caret at the end of the text, which the framework shows its keyboard for. Called in every frame while it is
     * edited, also while its box is scrolled out of sight, else the edit ends as the frame does, with nothing written.
     * A framed one draws a control's frame with an accent outline under the field, and the count of a most of
     * characters at the right of its box.
     * The edit ends as ImGui leaves the field: on the keyboard's Done, Enter, or a press beside it, with its text then
     * written where it is another than it started with. A physical keyboard's Escape and Back write nothing; a live
     * one has written each change as it was typed, and keeps it on Escape, which gives ImGui's field back its start.
     * The pulse is for the press that starts it.
     */
    TextEdit textEdit(const ImGuiID id, const ImRect& box, const bool pressed, const std::string_view text, const TextOptions& options, const TextLook& look)
    {
        TextEdit edit;
        auto& state = get().text();
        ImGui::PushOverrideID(id);
        const ImGuiID field = ImGui::GetID(FIELD_ID);
        ImGui::PopID();
        if (state.edited != id || state.end) {
            if (pressed) {
                state = { .edited = id, .field = field, .startText = std::string(text), .text = std::string(text), .seen = true };
                ImGui::ActivateItemByID(field);
                edit.pulse = true;
            }
            return edit;
        }
        state.seen = true;

        const auto& theme = get().theme();
        const auto& colors = get().colors();
        if (look.framed) {
            drawFrame(box);
            drawOutline(box.Min, box.Max, colors.accent);
        }

        // the count of a text with a most of characters is at the right of the box, where the field ends: the field has
        // the room of the widest count
        float countWidth = 0.0f;
        if (look.framed && options.maxLength > 0) {
            ImGui::PushFont(nullptr, theme.detailFontSize);
            countWidth = textWidth(countText(options.maxLength, options.maxLength)) + look.padding;
            ImGui::PopFont();
        }

        // the field is an item at the box, and the cursor is put back after it, so what follows is placed as before
        const auto cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(box.Min);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, colors.accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(look.padding, (box.GetHeight() - ImGui::GetFontSize()) * 0.5f));
        ImGui::SetNextItemWidth(box.GetWidth() - countWidth);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
        if (options.chars != TextChars::Any) {
            flags |= ImGuiInputTextFlags_CallbackCharFilter;
        }
        if (options.maxLength > 0) {
            flags |= ImGuiInputTextFlags_CallbackEdit;
        }
        ImGui::PushOverrideID(id);
        const bool changed = ImGui::InputText(FIELD_ID, &state.text, flags, flags != ImGuiInputTextFlags_None ? fieldCallback : nullptr, const_cast<TextOptions*>(&options));
        ImGui::PopID();
        const bool activated = ImGui::IsItemActivated();
        const bool active = ImGui::IsItemActive();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (look.framed && options.maxLength > 0) {
            ImGui::PushFont(nullptr, theme.detailFontSize);
            drawTextIn(box, countText(utf8Length(state.text), options.maxLength), colors.muted, 1.0f, look.padding);
            ImGui::PopFont();
        }
        // not with SetCursorScreenPos: ImGui takes a cursor that is set and followed by no item, as after the last row of
        // a page, for one that grows the page past its items, which is an error
        ImGui::GetCurrentWindow()->DC.CursorPos = cursor;

        if (activated) {
            if (auto* fieldState = ImGui::GetInputTextState(field)) {
                fieldState->SetSelection(fieldState->TextLen, fieldState->TextLen);
            }
        }
        if (active) {
            state.active = true;
            if (options.live && changed) {
                edit.write = state.text;
            }
            return edit;
        }

        // left, or never taken by ImGui: then nothing was typed
        const bool wasActive = state.active;
        const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        auto ended = std::move(state.text);
        const auto startText = std::move(state.startText);
        state = {};
        if (!wasActive || escaped) {
            return edit;
        }
        if (options.live ? changed : ended != startText) {
            edit.write = std::move(ended);
        }
        return edit;
    }

    /**
     * A text input that is not edited, in its box: a control's frame in its tone, and its text at the left in the
     * tone's color, which ends in an ellipsis where it is longer than the box.
     */
    void drawTextInput(const ImRect& box, const std::string_view text, const Tone tone)
    {
        drawFrame(box, tone);
        drawTextIn(box, text, get().tone(tone).color, 0.0f, TEXT_INPUT_PADDING);
    }
}
