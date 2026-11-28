#pragma once

#include <Windows.h>

namespace f4cf::imgui::internal
{
    /**
     * The keys of the game's window, handed to ImGui: a text field takes a physical keyboard's keys and typed text, and a
     * keyboard overlay's that sends keys to the window. Both runtimes; the input source gives the window.
     */
    class WindowKeys
    {
    public:
        static WindowKeys& get()
        {
            static WindowKeys instance;
            return instance;
        }

        void install(HWND window);
        void setTakingKeys(bool taking, bool escape);

    private:
        WindowKeys() = default;

        static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static void releaseKeys();

        // the game's window procedure, which every message is passed on to. Null while none is hooked.
        WNDPROC _gameWindowProc = nullptr;

        // install was called: the window is hooked once
        bool _installTried = false;

        // a canvas takes input: ImGui is given the window's keys, and Escape among them while it is not the mod's Back
        bool _takingKeys = false;
        bool _escape = true;
    };
}
