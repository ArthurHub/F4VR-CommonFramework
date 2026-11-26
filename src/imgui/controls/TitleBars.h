#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "Icons.h"

// The title bar of a panel: breadcrumbs that shorten to fit, and buttons at the right.
namespace f4cf::imgui::controls
{
    /**
     * How a breadcrumb is drawn, and whether it is pressed: only a link is.
     */
    enum class CrumbLook : std::uint8_t
    {
        // a crumb before the last, which is pressed to go there
        Link,
        // the last crumb: where the player is
        Current,
        // a crumb whose place is open, as a menu's front page: a solid fill, as a selected row has
        Selected,
    };

    /**
     * A breadcrumb of a title bar.
     */
    struct Crumb
    {
        std::string_view text;
        CrumbLook look = CrumbLook::Link;

        // false for a crumb that keeps its width where the second one would shorten
        bool shortens = true;
    };

    /**
     * A button at the right of a title bar.
     */
    struct TitleButton
    {
        // its name, which is its ID and its target's key
        const char* name = "";
        Icon icon = Icon::None;

        // drawn in place of an icon, as "?", and the wider text of one that stands out
        std::string_view text;
        std::string_view highlightText;
    };

    /**
     * What a title bar shows.
     */
    struct TitleBar
    {
        std::span<const Crumb> crumbs;

        // from the right edge
        std::span<const TitleButton> buttons;

        // said after the last crumb, as " (filtered)"
        std::string_view suffix;
        float height = 64.0f;
    };

    /**
     * What a title bar gave in this frame: the places of a pressed crumb, a pressed button and the button the ray is on.
     */
    struct TitleBarDrawn
    {
        std::optional<std::size_t> pressedCrumb;
        std::optional<std::size_t> pressedButton;
        std::optional<std::size_t> pointedButton;
    };

    TitleBarDrawn titleBar(const TitleBar& bar);
}
