# VR keyboard — typing into an ImGui canvas

A focused `ImGui::InputText` in a canvas is typed into. While ImGui edits a text field, the framework
shows SteamVR's keyboard, and the keys of the game's window reach ImGui too, so a physical keyboard or a
keyboard overlay that types into the window works as well. The content calls `ImGui::InputText` as on a
screen and has nothing else to call.

This is the design. None of it is built yet. It goes in [`src/imgui/`](../../src/imgui/), beside the
pointer that is the canvas's mouse ([`ImGuiPointer.cpp`](../../src/imgui/ImGuiPointer.cpp)), and runs
in the ImGui frame of [`ImGuiLayer.cpp`](../../src/imgui/ImGuiLayer.cpp).

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

The framework subclasses the game's window procedure (`SetWindowLongPtr`, on the window of the swap
chain) and hands its keyboard messages (`WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN`, `WM_SYSKEYUP`,
`WM_CHAR`) to Dear ImGui's Win32 backend, `ImGui_ImplWin32_WndProcHandler`. That backend maps a Windows
key to ImGui's key and character events, as it does for the flat game's ImGui menus (F4SEMenuFramework,
DearModdingUI). It needs the `win32-binding` feature of vcpkg's `imgui` and `ImGui_ImplWin32_Init`
with the window.

Mouse messages are not handed on, and `ImGui_ImplWin32_NewFrame` is not called: the pointer stays the
canvas's mouse, and the canvas's display stays the layer's.

A physical keyboard types into a field this way. So does any keyboard overlay that sends Windows keys
to the focused window, such as Virtual Desktop's, XSOverlay's, OVR Toolkit's or Desktop+'s, on any
runtime, while the game's window has focus.

## SteamVR's keyboard

The bridge follows [imgui-vr-helper](https://github.com/alandtse/imgui-vr-helper)'s
(`src/VRKeyboard.cpp`, and `PumpKeyboard()` in `api/ImGuiVRHelperClientSDK.h`), which does this for
Skyrim VR. It is GPL-3.0-or-later, as the framework is GPL-3.0.

Before each ImGui frame:

- **Shown.** When `io.WantTextInput` turns on, the keyboard is shown for an overlay the framework
  creates, with the field's text as its starting text, in single-line mode. The text is in ImGui's
  `InputTextState` while its id is the active one. The overlay shows nothing: it is the keyboard's
  anchor, placed below the panel.
- **Read.** The keyboard's events are read from that overlay's own queue (`PollNextOverlayEvent`), so the
  game's VR events are left to the game.
- **Typed.** On each `VREvent_KeyboardCharInput` the framework reads the keyboard's whole text and sends
  the field what changed: one `ImGuiKey_Backspace` for each character removed, then the new characters
  (`io.AddInputCharactersUTF8`). The diff is taken on whole UTF-8 characters, as ImGui deletes a whole
  character for a Backspace. The keyboard's text is the one that counts: a focused `InputText` keeps
  its own copy of its text, so the field follows the keyboard only through these events.
- **Done.** `VREvent_KeyboardDone` hides the keyboard and leaves the field (`ImGui::ClearActiveID`),
  with the text it has.
- **Cancelled.** `VREvent_KeyboardClosed`, the keyboard's close button, hides the keyboard and sends the
  field `ImGuiKey_Escape`: ImGui gives the field back the text it had and leaves it.
- **Left.** When the field is left from the content, or its panel is hidden, the keyboard is hidden
  (`HideKeyboard`).

While the keyboard is shown, the canvas's pointer presses nothing. The keyboard has its own lasers, and a
press of the ray beside the field would leave the field. After Done or a cancel, the keyboard is not
shown again until the trigger is let go: the press that pressed Done would otherwise focus the field
again on its release.

Every keyboard call is made on the game thread, in the ImGui frame, and none on the render thread.

### A runtime with no keyboard

A runtime has no keyboard when `ShowKeyboardForOverlay` fails (xrizer), or when `VREvent_KeyboardDone`
comes in the first frame after the show, before any key (OpenComposite). Its text is not taken, as it
is a fixed text and not the player's, and `GetKeyboardText` is never called on it. The framework logs it
once and does not show the keyboard again in the session. The field stays focused for the keys of the
game's window.

## To check in the headset

- Where SteamVR puts the keyboard: by the anchor overlay's transform, or only by
  `SetKeyboardTransformAbsolute`.
- Whether the game still sees the controllers while the keyboard is shown: the ray, and a Back button
  that would hide it.
- That the window's messages come on the thread that makes the ImGui frame, as ImGui's input queue is
  not thread-safe.
- Whether a key typed into a field also reaches the game. F4SEMenuFramework keeps the keys from the
  flat game by not passing the window's messages on while its menu is open.
