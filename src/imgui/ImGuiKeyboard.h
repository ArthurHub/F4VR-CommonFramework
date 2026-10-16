#pragma once

#include <Windows.h>

namespace f4cf::imgui::internal
{
    /**
     * The keyboard of the canvases: a focused ImGui text field is typed with the keys of the game's window.
     * The window's keyboard messages are handed to Dear ImGui's Win32 backend, which maps them to ImGui's key
     * and character events, so a physical keyboard types into a field, and so does a keyboard overlay that
     * sends keys to the window. The messages go on to the game as before.
     *
     * Mouse messages are not handed on: the pointer is the canvases' mouse. See docs/tech/vr-keyboard.md.
     */
    class Keyboard
    {
    public:
        static Keyboard& get();

        // Internal: used by the layer once its ImGui context is ready.
        void install();

    private:
        Keyboard() = default;

        static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static void releaseKeys();

        // the game's window procedure, which every message is passed on to. Null while none is hooked.
        WNDPROC _gameWindowProc = nullptr;

        // install was called: it looks for the window once
        bool _installTried = false;
    };

    inline Keyboard& keyboard()
    {
        return Keyboard::get();
    }
}
