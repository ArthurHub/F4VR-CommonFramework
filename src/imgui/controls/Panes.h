#pragma once

#include <optional>
#include <string_view>

#include "Targets.h"

// The panes of a panel's body: a navigation pane with a header that stays and rows that scroll, and the content's
// pane beside it.
namespace f4cf::imgui::controls
{
    float navRowWidth(float paneWidth);

    bool beginNavPane(const char* id, float width, float height);
    bool beginNavRows(const char* id, float paneWidth, bool showTop = false);
    void endNavRows(std::string_view scrollArea = target::TREE_AREA);
    void endNavPane();

    bool beginContentPane(const char* id, float height, std::optional<float> scrollTo = {});
    void endContentPane(std::string_view scrollArea = target::PAGE_AREA);
}
