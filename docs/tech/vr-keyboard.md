# VR keyboard — typing into an ImGui canvas

A focused `ImGui::InputText` in a canvas is typed into. While ImGui edits a text field, the framework
shows SteamVR's keyboard, and the keys of the game's window reach ImGui too, so a physical keyboard or a
keyboard overlay that types into the window works as well. The content calls `ImGui::InputText` as on a
screen and has nothing else to call.

Both are in [`ImGuiKeyboard.cpp`](../../src/imgui/ImGuiKeyboard.cpp), beside the pointer that is
the canvas's mouse ([`ImGuiPointer.cpp`](../../src/imgui/ImGuiPointer.cpp)), and run in the ImGui
frame of [`ImGuiLayer.cpp`](../../src/imgui/ImGuiLayer.cpp). What the field is sent is decided in
[`ImGuiKeyboardLogic.h`](../../src/imgui/ImGuiKeyboardLogic.h), which is unit tested.

The framework draws no keyboard of its own. A field that takes a number is typed on the same keyboard.

## Why SteamVR's keyboard

VR has no keyboard, and Dear ImGui has none either: it raises `io.WantTextInput` while a text field is
edited, for the platform to show one. OpenVR has one, `IVROverlay::ShowKeyboardForOverlay`, and
Fallout 4 VR uses it for both of its own text fields. A field in a canvas is then typed in the same way
as the game's, on SteamVR's layout for the player's language, with SteamVR's own laser pointers.

## What the game does

Read off `Fallout4VR.exe` **1.2.72.0**, as image-relative offsets (base `0x140000000`).

| Field | Overlay key | Keyboard shown | Events read |
| --- | --- | --- | --- |
| SPECIAL menu, the character's name | `SPECIALOverlay` | `0xBC9DB2`, in `0xBC9C2D` | `0xBCA19D` |
| Examine menu, an item's new name | `ExamineMenu` | `0xB3BA1B` and `0xB3BF89` | `0xB38620` |

Each menu creates an overlay of its own, named `Input Name`, and shows the keyboard for it: input mode
Submit, a single line, at most 26 characters, the menu's name as the description and the field's text
as the starting text. It then reads that overlay's events. `VREvent_KeyboardDone` (1202) takes the text
with `GetKeyboardText` and passes it to the menu's Flash `onVirtualKeyboardResult`, and
`VREvent_KeyboardClosed` (1200) cancels. Both hide the keyboard and the overlay. The code belongs to
those two Flash menus, so there is nothing in it to call for another field.

The game gets `IVROverlay_018`, the version of the framework's
[`external/openvr/openvr.h`](../../external/openvr/openvr.h), so `vr::VROverlay()` calls the same
functions. In that version `ShowKeyboardForOverlay` takes `bool bUseMinimalMode`, not the flags of a
newer SDK.

`BSPCVirtualKeyboardDevice`, the flat game's Steam Big Picture keyboard, is only `ret` stubs in the VR
build.

## The runtimes

| Runtime | Keyboard |
| --- | --- |
| SteamVR, also under Virtual Desktop or Link | Yes, the one the game names the character with |
| OpenComposite | None. `ShowKeyboardForOverlay` sends `VREvent_KeyboardDone` at once, and `GetKeyboardText` gives a fixed text: `Adventurer`, or the `keyboardText` of a fork's config |
| xrizer | None. `ShowKeyboardForOverlay` returns `VROverlayError_RequestFailed`, and `GetKeyboardText` panics |

So SteamVR's keyboard is shown only on SteamVR. On every runtime, a field is also typed with the keys
of the game's window.

## The keys of the game's window

The framework subclasses the game's window procedure (`SetWindowLongPtr`) and hands its keyboard
messages (`WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN`, `WM_SYSKEYUP`, `WM_CHAR`) to Dear ImGui's Win32
backend, `ImGui_ImplWin32_WndProcHandler`. That backend maps a Windows key to ImGui's key and character
events, as it does for the flat game's ImGui menus (F4SEMenuFramework, DearModdingUI). It needs the
`win32-binding` feature of vcpkg's `imgui` and `ImGui_ImplWin32_Init` with the window. Every message
goes on to the game's procedure.

The window is found by its class, `Fallout4VR`, which `WinMain` registers (`0xD8DF37`) and looks for
to keep a second copy of the game from starting (`0xD80BA3`). It is hooked when the ImGui context is
created, so only once a canvas is shown, and only when its messages come on the thread that builds the
ImGui frame: ImGui's input queue is not thread-safe. The window is made by the thread that runs the
game's main loop, so they do; were they not, the framework would log it and hand no keys on.

When the window loses focus (`WM_KILLFOCUS`), the keys ImGui holds down are let go, as the release of a
key then goes to another window: a Backspace that stayed down would repeat in the field. ImGui's own
answer to a lost focus is not used, as it lets go of the mouse button too, which is the pointer's.

Mouse messages are not handed on, and `ImGui_ImplWin32_NewFrame` is not called: the pointer stays the
canvas's mouse, and the canvas's display stays the layer's.

A physical keyboard types into a field this way. So does any keyboard overlay that sends Windows keys
to the focused window, such as Virtual Desktop's, XSOverlay's, OVR Toolkit's or Desktop+'s, on any
runtime, while the game's window has focus.

## SteamVR's keyboard

The bridge is like [imgui-vr-helper](https://github.com/alandtse/imgui-vr-helper)'s
(`src/VRKeyboard.cpp`, and `PumpKeyboard()` in `api/ImGuiVRHelperClientSDK.h`), which does this for
Skyrim VR, and edits the field in its own way. It is GPL-3.0-or-later, as the framework is GPL-3.0.

Before each ImGui frame:

- **Shown.** When ImGui edits a text field, the active item with an `InputTextState` that is not
  read-only, the keyboard is shown for an overlay the framework creates, with the field's text as its
  starting text, in single-line mode, for at least 256 characters. The overlay shows nothing and is not
  placed: it only owns the keyboard's events. SteamVR puts the keyboard where its dashboard was last
  opened, with the handle the player moves it by, and it does not go by the overlay, placed or shown.
  `SetKeyboardTransformAbsolute` moves it, but before the show it is not used, right after the show the
  keyboard closes, and a few frames later the keyboard loses its handle and sometimes closes: the
  framework leaves it to SteamVR. Its key has the address of the mod's keyboard
  in it, so each mod built on the framework has an overlay of its own.
- **One keyboard at a time.** The keyboard closes itself on Done and on its close button, and sends
  `VREvent_KeyboardClosed` after Done too. `HideKeyboard` is called only when the framework hides it: on a
  keyboard that closed itself it would send another Closed, which can come after the next keyboard is
  shown and close it. The events left in the queue are dropped before a keyboard is shown.
- **The floor.** The keyboard's text starts with a no-break space, before the field's, which is not seen
  and which the field never gets. SteamVR sends no event when the last character of its text is
  deleted, so with nothing before the field's text the field would keep its last character.
- **Read.** The keyboard's events are read from that overlay's own queue (`PollNextOverlayEvent`), so the
  game's VR events are left to the game.
- **Typed.** On each `VREvent_KeyboardCharInput` the framework reads the keyboard's whole text. When it
  differs from the text read before, it turns the field's own text, in its `InputTextState`, into it with one edit, where it changed: the part between
  the start and the end the two texts share. That part is selected in the field (`SetSelection`), and the
  new characters are typed over it (`io.AddInputCharactersUTF8`), or a Backspace takes it away. So a
  character changed in the middle is replaced there, in one frame. The texts are compared on whole UTF-8
  characters. Only a change of the keyboard's text is made in the field, which is then left as it is
  until the next one. So what the keys of the game's window type into the field stays, and so does what
  the field's filter did: characters go through ImGui's input, and a filter can drop a character, as a
  number field drops a letter, or change it, as an upper-case field does. OpenVR has no call that sets
  the text of a keyboard that is shown, so the keyboard does not get what the keys of the window typed,
  and its next change is made over it where the two texts differ. A Backspace is pressed in one
  frame and let go of in the next: the layer has ImGui take every event in the frame it is given in, so a
  press and a release given together would be lost.
- **Done.** `VREvent_KeyboardDone` hides the keyboard. The field is sent what it was not sent yet, and is
  then left (`ImGui::ClearActiveID`), with the text it has.
- **Cancelled.** `VREvent_KeyboardClosed`, the keyboard's close button, hides the keyboard and sends the
  field `ImGuiKey_Escape`: ImGui gives the field back the text it had and leaves it.
- **Left.** When the field is left from the content, another field is focused in its place, or no
  interactive canvas is shown, the keyboard is hidden (`HideKeyboard`). Once Done or a cancel has left
  the field, it is not shown again until ImGui edits no field.

While the keyboard is shown, the canvas's pointer is on no canvas and draws no ray: the keyboard has its
own lasers, and a press of the ray beside the field would leave the field. SteamVR keeps the controllers
from the game, and from every mod's reads, until the keyboard is closed: a mod's Back button does nothing
meanwhile, and the keyboard is closed with Done or its close button. The pointer waits five frames
more after the keyboard is hidden: the game's wands are still where they were before it was shown until
the game has the controllers' poses again, and the ray would show there for a frame. A trigger still down when it is
hidden is the game's until it is released, as for any ray that comes to a canvas with the trigger down,
so the press on Done does not press the canvas behind it.

Every keyboard call is made on the game thread, in the ImGui frame, and none on the render thread.

### A runtime with no keyboard

A runtime has no keyboard when `ShowKeyboardForOverlay` fails (xrizer), or when `VREvent_KeyboardDone`
comes in the first frame after the show, before any key (OpenComposite). Its text is not taken, as it
is a fixed text and not the player's, and `GetKeyboardText` is never called on it. The framework logs it
once and does not show the keyboard again in the session. The field stays focused for the keys of the
game's window.

## To check in the headset

- That the log says the keys of the game's window reach a text field, and not that the window is on
  another thread.
- Whether a key typed into a field also reaches the game. F4SEMenuFramework keeps the keys from the
  flat game by not passing the window's messages on while its menu is open.
