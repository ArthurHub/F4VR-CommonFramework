#include "ImGuiWindowKeys.h"

#include <imgui.h>
#include <imgui_internal.h>

#if !__has_include(<imgui_impl_win32.h>)
#error "F4VR-CommonFramework needs Dear ImGui's Win32 backend: add \"win32-binding\" to the features of imgui in the mod's vcpkg.json"
#endif
#include <imgui_impl_win32.h>

#include "InputSource.h"

// declared by imgui_impl_win32.h only in a comment, for the app to copy
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace f4cf::imgui::internal
{
    /**
     * Hand the keys of the given window, the game's, to ImGui while a canvas takes them (setTakingKeys). The window is
     * hooked once, on the thread that builds the ImGui frame: ImGui's input queue is not thread-safe, so a window whose
     * messages come on another thread is not hooked, and is logged.
     * The window's procedure is replaced with one that passes every message on to it, and is never put back.
     */
    void WindowKeys::install(const HWND window)
    {
        if (_installTried) {
            return;
        }
        _installTried = true;

        DWORD process = 0;
        const DWORD windowThread = window ? GetWindowThreadProcessId(window, &process) : 0;
        if (!window || process != GetCurrentProcessId()) {
            logger::warn("The game's window was not found; a text field takes no keys of a physical keyboard");
            return;
        }
        if (windowThread != GetCurrentThreadId()) {
            logger::warn("The game's window is on thread {}, and the ImGui frame on {}; a text field takes no keys of a physical keyboard", windowThread, GetCurrentThreadId());
            return;
        }

        if (!ImGui_ImplWin32_Init(window)) {
            logger::warn("ImGui's Win32 backend did not start; a text field takes no keys of a physical keyboard");
            return;
        }
        SetLastError(0);
        _gameWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
        if (!_gameWindowProc) {
            logger::warn("The game's window was not hooked (error {}); a text field takes no keys of a physical keyboard", GetLastError());
            ImGui_ImplWin32_Shutdown();
            return;
        }
        logger::info("The keys of the game's window reach a text field");
    }

    /**
     * Whether ImGui takes the window's keys: while a canvas takes input. Not otherwise, as ImGui keeps every key it is
     * given until its next frame, which is not built while no canvas is shown. A key ImGui holds down is let go of as it
     * stops taking them, since its release is not given to it. Escape is not given to ImGui where it is the mod's Back.
     */
    void WindowKeys::setTakingKeys(const bool taking, const bool escape)
    {
        if (_takingKeys && !taking) {
            releaseKeys();
        }
        _takingKeys = taking;
        _escape = escape;
    }

    /**
     * The game's window procedure while it is hooked: its keyboard messages are handed to ImGui while it takes them, and
     * every message then goes on to the game's procedure.
     */
    LRESULT CALLBACK WindowKeys::windowProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            if (get()._takingKeys && (get()._escape || wParam != VK_ESCAPE)) {
                ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
            }
            break;
        case WM_KILLFOCUS:
            releaseKeys();
            InputSource::get().onFocusLost();
            break;
        default:
            break;
        }
        return CallWindowProcW(get()._gameWindowProc, window, message, wParam, lParam);
    }

    /**
     * Let go of every key ImGui holds down, as the window loses focus: a key's release then goes to the window
     * that has the focus, and a key ImGui still held, as Backspace, would repeat in a field.
     * Not ImGui's own answer to a lost focus, which also lets go of the mouse buttons: the input source's.
     */
    void WindowKeys::releaseKeys()
    {
        if (!ImGui::GetCurrentContext()) {
            return;
        }
        // a key that is up already is skipped by ImGui
        auto& io = ImGui::GetIO();
        for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key) {
            io.AddKeyEvent(static_cast<ImGuiKey>(key), false);
        }
        for (const ImGuiKey modifier : { ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super }) {
            io.AddKeyEvent(modifier, false);
        }
    }
}
