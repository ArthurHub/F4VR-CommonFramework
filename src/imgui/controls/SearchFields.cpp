#include "SearchFields.h"

#include <imgui_internal.h>

#include "Controls.h"
#include "Frames.h"
#include "Icons.h"
#include "Targets.h"
#include "Text.h"
#include "TextInputs.h"

namespace
{
    using namespace f4cf::imgui::controls;

    // the magnifier before the text, Clear at the right and its cross
    constexpr float MAGNIFIER_SIZE = 20.0f;
    constexpr float CLEAR_SIZE = 32.0f;
    constexpr float CROSS_SIZE = 18.0f;

    // the query is written as it is typed
    constexpr TextOptions QUERY_OPTIONS{ .live = true };
}

namespace f4cf::imgui::controls
{
    /**
     * A search field at a place, in a width: the magnifier, then the placeholder in the detail size, or the query, then
     * how many match and Clear while there is a query. A press edits it on the live text input, bare in the field's look,
     * by the id `ImGui::GetID(id)`, so `isTextEdited` of it says it is edited and Back ends the edit; a physical
     * keyboard's Escape keeps the query. The caller keeps the query: it writes `write`, and empties it on `cleared`,
     * which ends an edit. The field is a target of kind `search` by `Field` while it is not edited, and Clear by `Clear`.
     */
    SearchDrawn searchField(const char* id, const ImVec2& pos, const float width, const std::string_view query, const SearchField& field)
    {
        const auto& theme = get().theme();
        const auto& colors = get().colors();
        SearchDrawn drawn;
        const ImRect box(pos, ImVec2(pos.x + width, pos.y + SEARCH_FIELD_HEIGHT));
        const float middle = box.GetCenter().y;
        const ImGuiID queryId = ImGui::GetID(id);
        const bool edited = get().isTextEdited(queryId);
        const bool hasQuery = !query.empty();
        const ImRect clearBox(ImVec2(box.Max.x - theme.space1 - CLEAR_SIZE, middle - CLEAR_SIZE * 0.5f), ImVec2(box.Max.x - theme.space1, middle + CLEAR_SIZE * 0.5f));
        const float fieldRight = hasQuery ? clearBox.Min.x : box.Max.x;

        ImGui::PushID(id);
        bool pressed = false;
        if (!edited) {
            ImGui::SetCursorScreenPos(box.Min);
            pressed = ImGui::InvisibleButton("##field", ImVec2(fieldRight - box.Min.x, SEARCH_FIELD_HEIGHT));
            drawn.pointed = ImGui::IsItemHovered();
            targets().add(target::SEARCH, "Field", ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
        }
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(box.Min, box.Max, drawn.pointed ? colors.hover : colors.track, theme.frameRounding);
        drawOutline(box.Min, box.Max, edited ? colors.accent : colors.frameBorder);
        drawIcon(Icon::Search, ImVec2(box.Min.x + theme.space2 + MAGNIFIER_SIZE * 0.5f, middle), drawn.pointed ? colors.accent : colors.muted, MAGNIFIER_SIZE);

        // how many match, at the right of the text
        const float textLeft = box.Min.x + theme.space2 + MAGNIFIER_SIZE + theme.space2;
        float textRight = fieldRight - theme.space2;
        if (hasQuery && field.count) {
            ImGui::PushFont(nullptr, theme.detailFontSize);
            const auto count = std::to_string(*field.count);
            const float countLeft = textRight - textWidth(count);
            drawText(countLeft, textRight, box.Min.y, SEARCH_FIELD_HEIGHT, colors.muted, count);
            ImGui::PopFont();
            textRight = countLeft - theme.space2;
        }

        const ImRect textBox(ImVec2(textLeft, box.Min.y), ImVec2(textRight, box.Max.y));
        const TextLook look{ .framed = false, .padding = 0.0f };
        if (edited) {
            drawn.write = textEdit(queryId, textBox, false, query, QUERY_OPTIONS, look).write;
        } else if (hasQuery) {
            drawText(textLeft, textRight, box.Min.y, SEARCH_FIELD_HEIGHT, colors.accent, query);
        } else {
            ImGui::PushFont(nullptr, theme.detailFontSize);
            drawText(textLeft, textRight, box.Min.y, SEARCH_FIELD_HEIGHT, drawn.pointed ? colors.accent : colors.muted, field.placeholder);
            ImGui::PopFont();
        }

        if (hasQuery) {
            ImGui::SetCursorScreenPos(clearBox.Min);
            if (ImGui::InvisibleButton("##clear", clearBox.GetSize())) {
                drawn.cleared = true;
                drawn.write.reset();
                drawn.pulse = true;
                if (edited) {
                    get().endTextEdit();
                }
            }
            targets().add(target::SEARCH, "Clear", clearBox);
            drawRowState();
            drawn.clearPointed = ImGui::IsItemHovered();
            drawIcon(Icon::Cross, clearBox.GetCenter(), colors.accent, CROSS_SIZE);
        }
        ImGui::PopID();

        if (pressed) {
            textEdit(queryId, textBox, true, query, QUERY_OPTIONS, look);
            drawn.pulse = true;
        }
        return drawn;
    }
}
