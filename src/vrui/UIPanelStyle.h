#pragma once

#include "../render/PrimitiveDraw.h"
#include "UIElement.h"

namespace f4cf::vrui
{
    /**
     * How a panel looks - the colour of its content, and the chrome around it: background, border,
     * corner rounding and padding - as one value, stated in vrui units.
     *
     * Its own defaults ARE the default panel: no background, no border, a little padding. A bare
     * panel and UIPanelStyle{} are therefore the same thing described once, and the two cannot drift
     * apart the way a separate set of default constants would.
     *
     * It is a plain aggregate, so a mod names its own house style in one constant, and starts from
     * an existing one by copying it and changing a field:
     *
     *     auto style = vrui::F4VR_PANEL_STYLE;
     *     style.borderColor = render::colors::Orange;
     *     panel->setStyle(style);
     *
     * setStyle applies the lot at once; the individual setters stay for changing one thing after the
     * fact, or for something that has to move per frame.
     *
     * Units, not pixels, which is what lets one style serve both kinds of panel - imgui::UIImGuiPanel
     * and the vrui::UIPanel family - each converting as it needs. A raw imgui::Canvas is placed by
     * its own provider and has no vrui units to state a style in, so it keeps its pixel setters
     * instead.
     */
    struct UIPanelStyle
    {
        // What the panel's own content is drawn in: a text panel's rows, an ImGui panel's text.
        // Content that names a colour of its own still wins - this is only the one it falls back to.
        render::Color color = render::colors::White;

        // alpha 0 is what "no background" means, so no separate flag is needed
        render::Color background{ 0.0f, 0.0f, 0.0f, 0.0f };

        // ...and zero thickness is what "no border" means
        render::Color borderColor = render::colors::White;
        float borderThicknessUnits = 0.0f;

        // rounds the background as well as the border, so the two cannot disagree at a corner and
        // leave background showing past the stroke
        float cornerRadiusUnits = 0.0f;

        // enough that content does not sit hard against the edge, and no more
        UIPadding padding = UIPadding::all(0.15f);
    };

    /**
     * The house style: dark glass with a green edge, half-transparent so a panel sits among the vrui
     * widget meshes rather than punching an opaque hole through the world behind them.
     *
     *     panel->setStyle(vrui::F4VR_PANEL_STYLE);
     *
     * Both kinds of panel take it, which is what lets an ImGui panel and a text panel stand side by side
     * and read as one UI rather than as two widgets that happen to be adjacent.
     */
    inline constexpr UIPanelStyle F4VR_PANEL_STYLE{
        // Opaque, both of them: these are picked to match the widgets beside the panel, and an alpha
        // below 255 blends a colour toward whatever is behind it, which reads as the wrong colour
        // rather than as a see-through one. The background is the only part that wants an alpha.
        .color = render::Color::rgba(10, 250, 120),
        .background = render::Color::rgba(15, 15, 15, 166),
        .borderColor = render::Color::rgba(10, 250, 120),
        .borderThicknessUnits = 0.05f,
        .cornerRadiusUnits = 0.2f,
        .padding = UIPadding::all(0.15f),
    };

    /**
     * The house style for buttons: the panel look with tight padding, since a 2x2 button has little
     * room to give away. Matches the look the NIF buttons were drawn with.
     *
     *     button->setStyle(vrui::F4VR_BUTTON_STYLE);
     */
    inline constexpr UIPanelStyle F4VR_BUTTON_STYLE{
        .color = render::Color::rgba(10, 250, 120),
        .background = render::Color::rgba(15, 15, 15, 166),
        .borderColor = render::Color::rgba(10, 250, 120),
        .borderThicknessUnits = 0.05f,
        .cornerRadiusUnits = 0.2f,
        .padding = UIPadding::symmetric(0.15f, 0.2f),
    };
}
