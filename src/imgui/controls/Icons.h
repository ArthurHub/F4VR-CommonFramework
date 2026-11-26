#pragma once

#include <cstdint>

#include <imgui.h>

// The drawn icons of the controls, as one set.
namespace f4cf::imgui::controls
{
    /**
     * A drawn icon. Each is drawn from the box it is designed in, scaled to the box it is drawn in: Search, Cross,
     * Minimize and Vibrate in a box of 24 with `ICON_THICKNESS` strokes, the rest, the icons of a button, in a box of 20
     * with strokes of 2 and round ends.
     */
    enum class Icon : std::uint8_t
    {
        None,
        // a magnifier, a cross (Close, Clear), a line at the bottom (Minimize), and a dot with arcs going out of it, as a
        // pulse (a haptic's Play)
        Search,
        Cross,
        Minimize,
        Vibrate,
        // the icons of a button
        Play,
        Stop,
        Bolt,
        Target,
        Check,
        Window,
        External,
        Link,
        Gear,
        Import,
        Export,
        Copy,
        Eye,
        Hand,
        Sound,
        Pin,
        Refresh,
        Trash,
    };

    // the box an icon is drawn in, unless it is given another
    constexpr float ICON_SIZE = 24.0f;

    float iconDesignBox(Icon icon);
    void drawIcon(Icon icon, const ImVec2& center, ImU32 color, float size = ICON_SIZE);
}
