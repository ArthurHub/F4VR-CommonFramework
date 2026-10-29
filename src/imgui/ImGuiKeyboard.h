#pragma once

#include <cstdint>
#include <string>

#include <Windows.h>
#include <imgui.h>

#include "../../external/openvr/openvr.h"

struct ImGuiInputTextState;

namespace f4cf::imgui::internal
{
    /**
     * The keyboard of the canvases: a focused ImGui text field is typed into.
     *
     * While ImGui edits a text field, SteamVR's keyboard is shown, and what is typed on it is sent to the field
     * as ImGui's key and character events. Done leaves the field with its text, and the keyboard's close button
     * gives the field back the text it had.
     *
     * The keyboard messages of the game's window are handed to Dear ImGui's Win32 backend too, so a physical
     * keyboard types into a field, and so does a keyboard overlay that sends keys to the window. On a runtime
     * with no SteamVR keyboard, that is the only way to type. The messages go on to the game as before.
     *
     * Mouse messages are not handed on: the pointer is the canvases' mouse. See docs/tech/vr-keyboard.md.
     */
    class Keyboard
    {
    public:
        static Keyboard& get();

        /**
         * The canvases' pointer is on no canvas: SteamVR's keyboard is shown, with its own lasers, or was hidden a
         * few frames ago, while the game's wands are still where they were before it was shown.
         */
        bool holdsPointer() const
        {
            return _state == State::Shown || _pointerWait > 0;
        }

        // Internal: used by the layer once its ImGui context is ready, and before each ImGui frame.
        void install();
        void update(bool canvasShown);

    private:
        /**
         * Where SteamVR's keyboard is with the text field ImGui edits.
         */
        enum class State : std::uint8_t
        {
            // not shown: it is shown when ImGui edits a text field
            Hidden,

            // shown, and what is typed on it is sent to the field
            Shown,

            // Done was pressed: the keyboard is hidden, the field is sent what it has not been sent yet, and is left
            Done,

            // the close button was pressed: the keyboard is hidden, and the field is sent Escape
            Cancelled,

            // the field was left or is being left: not shown again until ImGui edits no text field
            Closed,
        };

        Keyboard() = default;

        static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static void releaseKeys();

        void show(const ImGuiInputTextState& field);
        void hide(State state, bool closedItself = false);
        void readEvents();
        bool type(ImGuiInputTextState& field, bool keyReleased);
        void pressKey(ImGuiKey key);

        // the game's window procedure, which every message is passed on to. Null while none is hooked.
        WNDPROC _gameWindowProc = nullptr;

        // install was called: it looks for the window once
        bool _installTried = false;

        State _state = State::Hidden;

        // the runtime has a keyboard; false once it showed none, for the rest of the session
        bool _available = true;

        // the overlay the keyboard is shown for, whose queue has its events. It is not shown or placed.
        vr::VROverlayHandle_t _overlay = vr::k_ulOverlayHandleInvalid;

        // the id of the field the keyboard was shown for
        ImGuiID _fieldId = 0;

        // the keyboard's text, which the field is brought to
        std::string _text;

        // the characters the keyboard takes
        std::uint32_t _maxCharacters = 0;

        // the keyboard's text changed, and the field was not yet brought to it
        bool _changed = false;

        // a key was typed on the keyboard since it was shown
        bool _typed = false;

        // the frames the pointer still waits after the keyboard was hidden
        int _pointerWait = 0;

        // the frames since the keyboard was shown
        int _shownFrames = 0;

        // the key that was pressed last frame and is let go of this frame, ImGuiKey_None for none
        ImGuiKey _heldKey = ImGuiKey_None;
    };

    inline Keyboard& keyboard()
    {
        return Keyboard::get();
    }
}
