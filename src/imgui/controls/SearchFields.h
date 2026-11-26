#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

// The search field of the controls.
namespace f4cf::imgui::controls
{
    // the search field's height, a little less than a control's
    constexpr float SEARCH_FIELD_HEIGHT = 40.0f;

    /**
     * What a search field shows beside its query.
     */
    struct SearchField
    {
        // shown while the query is empty and not edited, as "Search 11 mods"
        std::string_view placeholder;

        // how many match, at the right of the query: none for none
        std::optional<std::size_t> count;
    };

    /**
     * What the drawing of a search field gave in this frame.
     */
    struct SearchDrawn
    {
        // the query as it is typed, and whether Clear emptied it
        std::optional<std::string> write;
        bool cleared = false;

        // the ray is on the field, or on Clear
        bool pointed = false;
        bool clearPointed = false;

        // a press started the edit, or cleared
        bool pulse = false;
    };

    SearchDrawn searchField(const char* id, const ImVec2& pos, float width, std::string_view query, const SearchField& field = {});
}
