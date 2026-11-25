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

        // what dims the panel behind a dialog
        render::Color scrim = render::Color::rgba(5, 8, 6, 140);

        // every pressable control and row is this high
        float controlHeight = 48.0f;

        // the corner of a control's frame, and the width of its outline
        float frameRounding = 4.0f;
        float frameBorderSize = 2.0f;

        // the corner of a list or a dialog that opens over the panel
        float popupRounding = 8.0f;

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
        ImU32 scrim = 0;
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
     * When a press that is held steps a value: as it begins, and again after a delay and then faster, as a step arrow
     * and the thumbstick step a slider.
     */
    struct StepRepeat
    {
        bool step(float seconds);

        // the press stepped as it began, how long it is held since, and when it steps next
        bool begun = false;
        float heldSeconds = 0.0f;
        float nextStepSeconds = 0.0f;
    };

    namespace internal
    {
        /**
         * What the sliders hold from one frame to the next, by the id of the slider each is about. 0 is none.
         */
        struct SliderState
        {
            // the slider the trigger is held on since a press on its track, the value the drag gave it, which is
            // written as the trigger is let go, and from the middle of the grab to the ray for a press on the grab
            ImGuiID dragged = 0;
            float dragValue = 0.0f;
            float grabOffset = 0.0f;

            // Back ended the drag: the rest of the press changes nothing
            bool dragCancelled = false;

            // the row of the dragged slider was held in this frame
            bool dragHeld = false;

            // the slider whose step arrow the trigger is held on since a press on it, that arrow's side as -1 or 1,
            // and its steps
            ImGuiID arrowHeld = 0;
            int arrowSide = 0;
            StepRepeat arrowRepeat;

            // the slider whose row the ray is on, for how long, and whether a row had the ray in this frame
            ImGuiID pointed = 0;
            float pointedSeconds = 0.0f;
            bool pointedSeen = false;

            // the thumbstick's push: its side as -1 or 1, and 0 while it is not pushed, and its steps
            int pushSide = 0;
            StepRepeat pushRepeat;

            // the thumbstick was pushed while the ray was on no slider, or came to another: it steps nothing until it
            // is let go
            bool pushIgnored = false;

            // the steps the thumbstick moves the slider under the ray by in this frame
            int steps = 0;
        };
    }

    /**
     * The controls' state, which exists once: their style, the input they read, and what a control holds from one
     * frame to the next, as a slider that is dragged.
     *
     *     controls::get().setStyle(style);
     *     controls::drawOutline(min, max, controls::get().colors().accent);
     *
     * The framework's ImGui layer gives it the thumbstick and its frames, so a mod calls none of that: it calls back()
     * first in its Back, and reset() as its page is another.
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

        bool back();
        void reset();

        // a slider is dragged: the trigger holds it since a press on its track
        bool isDragging() const
        {
            return _sliders.dragged != 0 && !_sliders.dragCancelled;
        }

        // how far the pointing hand's thumbstick is pushed to a side in this frame, from -1 at the left to 1 at the right
        float thumbstickSide() const
        {
            return _thumbstickSide;
        }

        // Internal: set by the layer from the pointing hand, or by a test, before the frame begins
        void setThumbstickSide(const float side)
        {
            _thumbstickSide = side;
        }

        // Internal: used by the layer around the content of every canvas, or by a test's frame
        void updateInput();
        void beginFrame(float deltaSeconds);
        void endFrame();

        // Internal: used by the sliders
        internal::SliderState& sliders()
        {
            return _sliders;
        }

    private:
        Controls();

        ControlsStyle _style;
        ControlsColors _colors;
        float _thumbstickSide = 0.0f;
        internal::SliderState _sliders;
    };

    inline Controls& get()
    {
        return Controls::get();
    }
}
