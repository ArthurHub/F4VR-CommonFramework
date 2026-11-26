#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

#include "../../render/Color.h"
#include "../ImGuiTheme.h"

namespace f4cf::imgui::controls
{
    /**
     * The colors of the theme as ImGui draws them.
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
        ImU32 glass = 0;
        ImU32 plate = 0;
        ImU32 important = 0;
    };

    /**
     * The tone of a value, which picks its colors from the theme: the accent, the warning or the error.
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

        /**
         * What the steppers and the lists hold from one frame to the next.
         */
        struct ChoiceState
        {
            // the side the thumbstick is pushed to, as -1 or 1, and 0 while it is not pushed, and the step of the
            // stepper under the ray in this frame: the side of a push that began in it
            int pushSide = 0;
            int step = 0;

            // the list that is open, by its popup's id, 0 for none, the level of its popup among the open ones, and
            // whether it was shown in this frame
            ImGuiID openList = 0;
            int openLevel = 0;
            bool openSeen = false;

            // Back or a reset closes that list, and closePopups every popup but a modal one, as the next frame begins:
            // ImGui closes a popup inside a frame
            bool closeList = false;
            bool closeAll = false;
        };

        /**
         * What the text input that is edited holds from one frame to the next. One is edited at a time, as ImGui
         * edits one field.
         */
        struct TextState
        {
            // the text input that is edited, 0 for none, and the id of its ImGui field
            ImGuiID edited = 0;
            ImGuiID field = 0;

            // its text as the edit started, and the text of its field
            std::string startText;
            std::string text;

            // ImGui took the field since the edit started: the frame of the press is before it
            bool active = false;

            // the edit ends with nothing written as the next frame begins, for Back
            bool end = false;

            // the text input was drawn in this frame
            bool seen = false;
        };
    }

    /**
     * The controls' state, which exists once: the theme, the input they read, and what a control holds from one frame
     * to the next, as a slider that is dragged or a text input that is edited.
     *
     *     controls::get().setTheme(theme);
     *     controls::drawOutline(min, max, controls::get().colors().accent);
     *
     * The framework's ImGui layer gives it the thumbstick and its frames, so a mod calls none of that: it calls back()
     * first in its Back, reset() as its page is another or an editor opens, and closePopups() as its page is another.
     */
    class Controls
    {
    public:
        static Controls& get();

        void setTheme(const Theme& theme);

        const Theme& theme() const
        {
            return _theme;
        }

        const ControlsColors& colors() const
        {
            return _colors;
        }

        ToneColors tone(Tone tone) const;

        bool back();
        void reset();
        void closePopups();

        // a slider is dragged: the trigger holds it since a press on its track
        bool isDragging() const
        {
            return _sliders.dragged != 0 && !_sliders.dragCancelled;
        }

        // a list of options is open, and Back has not closed it
        bool isListOpen() const
        {
            return _choices.openList != 0 && !_choices.closeList;
        }

        // a text input is edited, and Back has not ended its edit
        bool isTextEdited(const ImGuiID id) const
        {
            return _text.edited == id && id != 0 && !_text.end;
        }

        bool endTextEdit();

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

        void applyImGuiStyle() const;

        // Internal: used by the layer before the main context's frame begins
        void updateImGuiStyle();

        // Internal: used by the layer around the content of every canvas, or by a test's frame
        void updateInput();
        void beginFrame(float deltaSeconds);
        void endFrame();

        // Internal: used by the sliders
        internal::SliderState& sliders()
        {
            return _sliders;
        }

        // Internal: used by the steppers and the lists
        const internal::ChoiceState& choices() const
        {
            return _choices;
        }

        void listShown(ImGuiID id, int level);

        // Internal: used by the text inputs
        internal::TextState& text()
        {
            return _text;
        }

    private:
        Controls();

        Theme _theme;
        ControlsColors _colors;

        // ImGui's style of the main context has the theme: false until the layer applies it, and after a new theme
        bool _imGuiStyleApplied = false;

        float _thumbstickSide = 0.0f;
        internal::SliderState _sliders;
        internal::ChoiceState _choices;
        internal::TextState _text;
    };

    inline Controls& get()
    {
        return Controls::get();
    }
}
