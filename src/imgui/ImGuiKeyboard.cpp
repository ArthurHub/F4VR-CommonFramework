#include "ImGuiKeyboard.h"

#include <algorithm>
#include <cstring>
#include <format>

#include <imgui.h>
#include <imgui_internal.h>

#if !__has_include(<imgui_impl_win32.h>)
#error "F4VR-CommonFramework needs Dear ImGui's Win32 backend: add \"win32-binding\" to the features of imgui in the mod's vcpkg.json"
#endif
#include <imgui_impl_win32.h>

#include "ImGuiKeyboardLogic.h"

// declared by imgui_impl_win32.h only in a comment, for the app to copy
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace f4cf::imgui::internal
{
    namespace
    {
        // the class of the game's window, which WinMain registers and creates it with
        constexpr auto GAME_WINDOW_CLASS = "Fallout4VR";

        // the characters SteamVR's keyboard takes, unless the field has more already
        constexpr std::uint32_t MAX_CHARACTERS = 256;

        // the frames the pointer still waits after the keyboard is hidden: the game's wands are where they were
        // before it was shown until it has the controllers' poses again
        constexpr int POINTER_WAIT_FRAMES = 5;

        bool isKeyboardEvent(const std::uint32_t type)
        {
            return type == vr::VREvent_KeyboardClosed || type == vr::VREvent_KeyboardCharInput || type == vr::VREvent_KeyboardDone;
        }

        /**
         * The text field ImGui edits: the active item, when it is a text field that is not read-only.
         */
        ImGuiInputTextState* editedField()
        {
            const ImGuiContext* context = ImGui::GetCurrentContext();
            ImGuiInputTextState* field = context ? ImGui::GetInputTextState(context->ActiveId) : nullptr;
            return field && !(field->Flags & ImGuiInputTextFlags_ReadOnly) ? field : nullptr;
        }

        std::string_view fieldText(const ImGuiInputTextState& field)
        {
            return field.TextA.Data ? std::string_view(field.TextA.Data, static_cast<std::size_t>(field.TextLen)) : std::string_view();
        }

    }

    Keyboard& Keyboard::get()
    {
        static Keyboard instance;
        return instance;
    }

    /**
     * Hand the keys of the game's window to ImGui, from now on. It looks for the window once, on the thread
     * that builds the ImGui frame: ImGui's input queue is not thread-safe, so a window whose messages come on
     * another thread is not hooked, and is logged.
     * The window's procedure is replaced with one that passes every message on to it, and is never put back.
     */
    void Keyboard::install()
    {
        if (_installTried) {
            return;
        }
        _installTried = true;

        HWND window = FindWindowA(GAME_WINDOW_CLASS, nullptr);
        DWORD process = 0;
        const DWORD windowThread = window ? GetWindowThreadProcessId(window, &process) : 0;
        if (!window || process != GetCurrentProcessId()) {
            logger::warn("The game's window was not found; a text field takes no keys");
            return;
        }
        if (windowThread != GetCurrentThreadId()) {
            logger::warn("The game's window is on thread {}, and the ImGui frame on {}; a text field takes no keys", windowThread, GetCurrentThreadId());
            return;
        }

        if (!ImGui_ImplWin32_Init(window)) {
            logger::warn("ImGui's Win32 backend did not start; a text field takes no keys");
            return;
        }
        SetLastError(0);
        _gameWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
        if (!_gameWindowProc) {
            logger::warn("The game's window was not hooked (error {}); a text field takes no keys", GetLastError());
            ImGui_ImplWin32_Shutdown();
            return;
        }
        logger::info("The keys of the game's window reach a text field");
    }

    /**
     * The game's window procedure while it is hooked: its keyboard messages are handed to ImGui, and every
     * message then goes on to the game's procedure.
     */
    LRESULT CALLBACK Keyboard::windowProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
            break;
        case WM_KILLFOCUS:
            releaseKeys();
            break;
        default:
            break;
        }
        return CallWindowProcW(get()._gameWindowProc, window, message, wParam, lParam);
    }

    /**
     * Let go of every key ImGui holds down, as the window loses focus: a key's release then goes to the window
     * that has the focus, and a key ImGui still held, as Backspace, would repeat in a field.
     * ImGui's own answer to a lost focus also lets go of the mouse button, which is the pointer's.
     */
    void Keyboard::releaseKeys()
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

    /**
     * SteamVR's keyboard, before each ImGui frame: it is shown while ImGui edits a text field on an interactive
     * canvas, and what is typed on it is sent to the field. It is hidden when the field is left, or no
     * interactive canvas is shown.
     * A key is pressed in one frame and let go of in the next: the layer has ImGui take every event in the
     * frame it is given in, so a press and a release given together would be lost.
     */
    void Keyboard::update(const bool canvasShown)
    {
        if (_pointerWait > 0) {
            --_pointerWait;
        }
        if (_state == State::Hidden && _heldKey == ImGuiKey_None && !canvasShown) {
            return;
        }

        const bool keyReleased = _heldKey != ImGuiKey_None;
        if (keyReleased) {
            ImGui::GetIO().AddKeyEvent(_heldKey, false);
            _heldKey = ImGuiKey_None;
        }

        // a field the content focused in place of the one the keyboard was shown for hides it, to be shown again for that field
        ImGuiInputTextState* field = canvasShown ? editedField() : nullptr;
        if (!field || (_state != State::Hidden && field->ID != _fieldId)) {
            hide(State::Hidden);
            return;
        }

        switch (_state) {
        case State::Hidden:
            if (_available) {
                show(*field);
            }
            break;
        case State::Shown:
            ++_shownFrames;
            readEvents();
            if (_state == State::Shown) {
                type(*field, keyReleased);
            } else if (_state == State::Done && !type(*field, keyReleased)) {
                ImGui::ClearActiveID();
                _state = State::Closed;
            }
            break;
        case State::Done:
            // the field is left once it has the keyboard's text: what it was sent in the last frame, it took in that frame
            if (!type(*field, keyReleased)) {
                ImGui::ClearActiveID();
                _state = State::Closed;
            }
            break;
        case State::Cancelled:
            // ImGui gives the field back the text it had and leaves it
            if (!keyReleased) {
                pressKey(ImGuiKey_Escape);
                _state = State::Closed;
            }
            break;
        case State::Closed:
            break;
        }
    }

    /**
     * Show SteamVR's keyboard for the field, with its text. SteamVR places it. A runtime that shows none is
     * logged, and no keyboard is shown again in the session.
     */
    void Keyboard::show(const ImGuiInputTextState& field)
    {
        auto* overlay = vr::VROverlay();
        if (!overlay) {
            _available = false;
            logger::info("No VR overlays; a text field is typed only with the keys of the game's window");
            return;
        }
        if (_overlay == vr::k_ulOverlayHandleInvalid) {
            // the key is unique to this mod: every mod built on the framework has a keyboard of its own
            const auto key = std::format("f4cf.keyboard.{:x}", reinterpret_cast<std::uintptr_t>(this));
            if (const auto error = overlay->CreateOverlay(key.c_str(), "Keyboard", &_overlay); error != vr::VROverlayError_None) {
                _overlay = vr::k_ulOverlayHandleInvalid;
                _available = false;
                logger::warn("The keyboard's overlay was not created (error {}); a text field is typed only with the keys of the game's window", static_cast<int>(error));
                return;
            }
        }
        _text.assign(fieldText(field));
        const auto characters = static_cast<std::uint32_t>(std::ranges::count_if(_text, [](const char c) {
            return (static_cast<std::uint8_t>(c) & 0xC0) != 0x80;
        }));
        _maxCharacters = (std::max)(MAX_CHARACTERS, characters);

        // the events left in the queue are of the keyboard shown before, which is not read once it is hidden
        vr::VREvent_t stale{};
        while (overlay->PollNextOverlayEvent(_overlay, &stale, sizeof(stale))) {
            if (isKeyboardEvent(stale.eventType)) {
                logger::trace("SteamVR's keyboard: event {} of the keyboard shown before is dropped", stale.eventType);
            }
        }

        const std::string shownText = std::string(KEYBOARD_TEXT_FLOOR) + _text;
        const auto error = overlay->ShowKeyboardForOverlay(_overlay,
            vr::k_EGamepadTextInputModeNormal,
            vr::k_EGamepadTextInputLineModeSingleLine,
            "",
            _maxCharacters + 1,
            shownText.c_str(),
            false,
            0);
        if (error != vr::VROverlayError_None) {
            _available = false;
            logger::info("The runtime has no keyboard (error {}); a text field is typed only with the keys of the game's window", static_cast<int>(error));
            return;
        }
        logger::trace("SteamVR's keyboard is shown for a text field");
        _state = State::Shown;
        _fieldId = field.ID;
        _typed = false;
        _shownFrames = 0;
        _changed = false;
    }

    /**
     * Hide SteamVR's keyboard, if it is shown, and go on to the given state.
     * @param closedItself the keyboard closed itself, on Done or its close button: HideKeyboard then would send
     * another Closed, which could come after the next keyboard is shown and close it
     */
    void Keyboard::hide(const State state, const bool closedItself)
    {
        if (_state == State::Shown) {
            if (auto* overlay = vr::VROverlay()) {
                if (!closedItself) {
                    overlay->HideKeyboard();
                }
            }
            _pointerWait = POINTER_WAIT_FRAMES;
        }
        _state = state;
    }

    /**
     * Read the keyboard's events from its own overlay's queue, so the game's VR events are left to the game.
     * Done that comes in the first frame after the keyboard is shown, before a key, is a runtime with no
     * keyboard (OpenComposite): its text is a fixed one and not the player's, so it is not taken, and the field
     * stays edited for the keys of the game's window.
     */
    void Keyboard::readEvents()
    {
        auto* overlay = vr::VROverlay();
        if (!overlay) {
            hide(State::Hidden);
            return;
        }
        const auto readText = [this, overlay] {
            // up to 4 bytes a character in UTF-8, the floor's character, and the zero that ends it
            const std::uint32_t bytes = (_maxCharacters + 1) * 4;
            std::string text(bytes + 1, char());
            overlay->GetKeyboardText(text.data(), bytes);
            // what the keys of the game's window typed into the field stays until the keyboard's text changes
            const std::string_view received = withoutFloor(text.c_str());
            if (received != _text) {
                _text.assign(received);
                _changed = true;
            }
        };

        vr::VREvent_t event{};
        while (_state == State::Shown && overlay->PollNextOverlayEvent(_overlay, &event, sizeof(event))) {
            if (isKeyboardEvent(event.eventType)) {
                logger::trace("SteamVR's keyboard: event {}, after {} frames", event.eventType, _shownFrames);
            }
            switch (event.eventType) {
            case vr::VREvent_KeyboardCharInput:
                readText();
                _typed = true;
                break;
            case vr::VREvent_KeyboardDone:
                if (!_typed && _shownFrames <= 1) {
                    _available = false;
                    logger::info("The runtime has no keyboard (Done at once); a text field is typed only with the keys of the game's window");
                    hide(State::Closed, true);
                } else {
                    readText();
                    hide(State::Done, true);
                }
                break;
            case vr::VREvent_KeyboardClosed:
                hide(State::Cancelled, true);
                break;
            default:
                break;
            }
        }
    }

    /**
     * Make the one edit that turns the field's text into the keyboard's, at the place it changed: the part that
     * changed is selected, and the new characters are typed over it, through the field's filter, or Backspace
     * takes it away. Only a change of the keyboard's text is made: the field is then left as it is, so a filter
     * that drops or changes a character, or the keys of the game's window, are not undone in the next frame.
     * @param keyReleased a key was let go of in this frame, so none is pressed in it
     * @return whether an edit was sent in this frame or waits for the next one
     */
    bool Keyboard::type(ImGuiInputTextState& field, const bool keyReleased)
    {
        if (!_changed) {
            return false;
        }
        const TextEdit edit = textEdit(fieldText(field), _text);
        if (edit.empty()) {
            _changed = false;
            return false;
        }
        if (!edit.insert.empty()) {
            field.SetSelection(static_cast<int>(edit.from), static_cast<int>(edit.to));
            ImGui::GetIO().AddInputCharactersUTF8(std::string(edit.insert).c_str());
            _changed = false;
        } else if (!keyReleased) {
            // the change is made once the field takes the Backspace, in this frame
            field.SetSelection(static_cast<int>(edit.from), static_cast<int>(edit.to));
            pressKey(ImGuiKey_Backspace);
        }
        return true;
    }

    /**
     * Press the key in this frame, and let go of it in the next.
     */
    void Keyboard::pressKey(const ImGuiKey key)
    {
        ImGui::GetIO().AddKeyEvent(key, true);
        _heldKey = key;
    }
}
