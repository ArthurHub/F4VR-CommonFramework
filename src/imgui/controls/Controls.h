#pragma once

#include <cstdint>

#include <imgui.h>

#include "../../render/Color.h"

namespace f4cf::imgui::controls
{
    /**
     * How the controls look: their colors by role, and the few sizes a panel may set otherwise. Sizes are in the
     * canvas's pixels.
     *
     * Its own defaults are the framework's green, the look of vrui::F4VR_PANEL_STYLE: a mod changes a part by
     * starting from them, as for the keyboard's style.
     *
     *     auto style = controls::get().style();
     *     style.accent = render::Color::rgba(255, 180, 60);
     *     controls::get().setStyle(style);
     */
    struct ControlsStyle
    {
        // the house color: text, icons, a selected or chosen fill, an outline under the ray
        render::Color accent = render::Color::rgba(10, 250, 120);

        // secondary text: headings, units, counts, a cell that is not chosen
        render::Color muted = render::Color::rgba(159, 224, 187);

        // the fill of a control's frame, as a slider's track
        render::Color track = render::Color::rgba(10, 250, 120, 26);

        // a row or a control under the ray, and while the trigger is held on it
        render::Color hover = render::Color::rgba(10, 250, 120, 31);
        render::Color pressed = render::Color::rgba(10, 250, 120, 51);

        // the outline while pressed, and a primary button under the ray
        render::Color glow = render::Color::rgba(196, 255, 220);

        // a heading's rule and a section's line
        render::Color rule = render::Color::rgba(10, 250, 120, 102);

        // a frame's outline at rest
        render::Color frameBorder = render::Color::rgba(10, 250, 120, 140);

        // text and icons on a solid accent fill
        render::Color onAccent = render::Color::rgba(4, 23, 12);

        // the warning tone and the text on its fill
        render::Color warning = render::Color::rgba(255, 191, 51);
        render::Color onWarning = render::Color::rgba(31, 20, 0);

        // the error tone and the text on its fill
        render::Color error = render::Color::rgba(255, 149, 133);
        render::Color onError = render::Color::rgba(36, 8, 5);

        // the background of a list or a dialog that opens over the panel
        render::Color popup = render::Color::rgba(11, 18, 14, 245);

        // every pressable control and row is this high
        float controlHeight = 48.0f;

        // the corner of a control's frame, and the width of its outline
        float frameRounding = 4.0f;
        float frameBorderSize = 2.0f;

        // the font size of a detail, as a tag's text
        float detailFontSize = 18.0f;
    };

    /**
     * The colors of a ControlsStyle as ImGui draws them.
     */
    struct ControlsColors
    {
        ImU32 accent = 0;
        ImU32 muted = 0;
        ImU32 track = 0;
        ImU32 hover = 0;
        ImU32 pressed = 0;
        ImU32 glow = 0;
        ImU32 rule = 0;
        ImU32 frameBorder = 0;
        ImU32 onAccent = 0;
        ImU32 warning = 0;
        ImU32 onWarning = 0;
        ImU32 error = 0;
        ImU32 onError = 0;
        ImU32 popup = 0;
    };

    /**
     * The tone of a value, which picks its colors from the style: the accent, the warning or the error.
     */
    enum class Tone : std::uint8_t
    {
        Normal,
        Warning,
        Error,
    };

    /**
     * The colors of a tone: its own, for a value's text and fill, and that of a text on its fill.
     */
    struct ToneColors
    {
        ImU32 color = 0;
        ImU32 onColor = 0;
    };

    /**
     * The controls' state, which exists once: their style for now.
     *
     *     controls::get().setStyle(style);
     *     controls::drawOutline(min, max, controls::get().colors().accent);
     */
    class Controls
    {
    public:
        static Controls& get();

        void setStyle(const ControlsStyle& style);

        const ControlsStyle& style() const
        {
            return _style;
        }

        const ControlsColors& colors() const
        {
            return _colors;
        }

        ToneColors tone(Tone tone) const;

    private:
        Controls();

        ControlsStyle _style;
        ControlsColors _colors;
    };

    inline Controls& get()
    {
        return Controls::get();
    }
}
