#include "Panes.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Controls.h"

namespace
{
    using namespace f4cf::imgui::controls;

    /**
     * The room a navigation pane keeps on the right of its rows: its scroll bar, with `space1` on each side.
     */
    float navRightRoom()
    {
        const auto& theme = get().theme();
        return theme.space1 + theme.scrollbarSize + theme.space1;
    }

    /**
     * Records the scroll of the child that is drawn under an area, where its content was drawn.
     */
    void addPaneScroll(const std::string_view scrollArea)
    {
        if (!ImGui::GetCurrentWindow()->SkipItems) {
            targets().addScroll(scrollArea);
        }
    }
}

namespace f4cf::imgui::controls
{
    /**
     * The width of a row of a navigation pane of a width: without its padding on the left and the room of the scroll bar
     * on the right, so a row is the same width whether or not the scroll bar shows.
     */
    float navRowWidth(const float paneWidth)
    {
        return paneWidth - get().theme().space2 - navRightRoom();
    }

    /**
     * Begins a navigation pane at the cursor, of a width and a height: a child of the theme's `plate`, padded `space2`,
     * whose rows are `space1` apart. The caller draws its header at the cursor, as a search field, and gives it its room
     * with `ImGui::Dummy`, then its rows in `beginNavRows`. Returns whether it is shown; `endNavPane` is called either way.
     */
    bool beginNavPane(const char* id, const float width, const float height)
    {
        const auto& theme = get().theme();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, get().colors().plate);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(theme.space2, theme.space2));
        const bool shown = ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        // the rows of a navigation pane are closer to each other than the rows of a page
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(theme.space3, theme.space1));
        return shown;
    }

    /**
     * Begins the rows of a navigation pane of a width, under its header, which scroll: wider than the rows
     * (`navRowWidth`) by the room of the selected bar's tip and of the scroll bar, to the pane's bottom. `showTop` scrolls
     * it to its top. Returns whether its rows are drawn; `endNavRows` is called either way.
     */
    bool beginNavRows(const char* id, const float paneWidth, const bool showTop)
    {
        if (showTop) {
            ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        }
        return ImGui::BeginChild(id, ImVec2(navRowWidth(paneWidth) + navRightRoom() - get().theme().space1, 0.0f));
    }

    /**
     * Ends the rows of a navigation pane, and records their scroll as a target's area.
     */
    void endNavRows(const std::string_view scrollArea)
    {
        addPaneScroll(scrollArea);
        ImGui::EndChild();
    }

    /**
     * Ends a navigation pane.
     */
    void endNavPane()
    {
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    /**
     * Begins the content's pane at the cursor, of a height, which fills the width left, as beside a navigation pane on
     * the same line. `scrollTo` sets its scroll in this frame: 0 as its page is another, or a place kept for a page.
     * Returns whether it is shown; `endContentPane` is called either way.
     */
    bool beginContentPane(const char* id, const float height, const std::optional<float> scrollTo)
    {
        if (scrollTo) {
            ImGui::SetNextWindowScroll(ImVec2(0.0f, *scrollTo));
        }
        return ImGui::BeginChild(id, ImVec2(0.0f, height));
    }

    /**
     * Ends the content's pane, and records its scroll as a target's area.
     */
    void endContentPane(const std::string_view scrollArea)
    {
        addPaneScroll(scrollArea);
        ImGui::EndChild();
    }
}
