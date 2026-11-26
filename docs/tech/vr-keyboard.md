# VR keyboard — typing into an ImGui canvas

A focused `ImGui::InputText` in a canvas is typed into. While ImGui edits a text field, the framework
shows its own keyboard under the field's canvas, typed with the rays, and the keys of the game's window
reach ImGui too, so a physical keyboard or a keyboard overlay that types into the window works as well.
The content calls `ImGui::InputText` as on a screen and has nothing else to call.

The keyboard is in [`ImGuiKeyboard.cpp`](../../src/imgui/ImGuiKeyboard.cpp), beside the pointer that
is the canvas's mouse ([`ImGuiPointer.cpp`](../../src/imgui/ImGuiPointer.cpp)), and runs in the ImGui
frame of [`ImGuiLayer.cpp`](../../src/imgui/ImGuiLayer.cpp). Its pages, the key a point hits, Shift,
Caps, the repeat of a held key and the strokes ImGui is given are in
[`ImGuiKeyboardLogic.h`](../../src/imgui/ImGuiKeyboardLogic.h), which is unit tested.

## Why the framework's own

VR has no keyboard, and Dear ImGui has none either: it raises `io.WantTextInput` while a text field is
edited, for the platform to show one. OpenVR has one, `IVROverlay::ShowKeyboardForOverlay`, which
Fallout 4 VR names the character with, but:

- **Not on every runtime.** OpenComposite has none: `ShowKeyboardForOverlay` sends
  `VREvent_KeyboardDone` at once, and `GetKeyboardText` gives a fixed text (`Adventurer`). In xrizer
  `ShowKeyboardForOverlay` returns `VROverlayError_RequestFailed`, and `GetKeyboardText` panics.
- **Not where the field is.** SteamVR puts it where its dashboard was last opened, and
  `SetKeyboardTransformAbsolute` closes it or takes its handle away.
- **Not the field's text.** It sends only its whole text, with no event when its last character is
  deleted, so the field is brought to it by comparing the two texts.

The framework's keyboard is drawn and typed like any canvas, on every runtime, and types into the field
with ImGui's own key and character events.

## What it is

- **Shown** while ImGui edits a text field (the active item with an `InputTextState` that is not
  read-only) on an interactive canvas that is shown. It is hidden when the field is left, which Done
  does, and when no interactive canvas is shown.
- **A canvas of its own context** (`Canvas::setOwnContext`, see the [imgui README](../../src/imgui/README.md#how-it-is-drawn)):
  an `imgui::UIImGuiPanel` drawn in a second ImGui context into its own texture, so it shows beside a
  dedicated canvas, and nothing done on it reaches the main context, where the field stays the active
  item. It is 24 by 10 vrui units, 1152 by 480 pixels, scaled to its width in the world.
- **Hidden by the world** in front of it, as a canvas is by default (`setOccluded`): the hands, which
  are near it as they type, are drawn over it.
- **Pages:** the rows of a US PC keyboard, as SteamVR's keyboard has them, with what a US keyboard
  types with Shift, Enter and both Shifts where it has them; Clear is where Tab is. The symbols
  (`?123`), each a key of its own, with the marks and signs of Latin-1 such as ¿ « € °. The accented
  letters of Latin-1 and of the languages of Europe beside it, with their capitals (`áé`). Each page
  ends in two page keys, Space and Done, and has Backspace, Clear and Enter. A key shows what it types:
  its shifted character while Shift is on. Shift shifts one character; Caps shifts the letters until
  pressed again. A key wider than one spans the gaps it covers, so the rows are staggered as on a
  keyboard, and a row with fewer keys is centered. The keys that do not type a character are darker,
  as SteamVR's are; Enter, which a one-line field has no use for, has a dim label there and is not
  pressed.
- **The line of the field's text** along its top, as a row of the keys, with Left and Right at its
  right: the text, its selection and its caret, scrolled to keep the caret in view. The keyboard reads
  them from the field's `InputTextState` before each frame, as its own context does not have the field,
  and with them its flags: whether it is multiline.

## Typing with the rays

The keyboard's canvas is a pointer target (`PointerTarget::ownContext`). A ray that meets it before any
other canvas is on it, and the pointer handles it as on any interactive canvas: the wand is hidden from
the game, a pull that began on it stays on it until the trigger is released, and a trigger already down
when the ray arrives is the game's. But it is not ImGui's mouse: the pointer keeps each hand on it apart
(`Pointer::ownCanvasHand`), and draws both hands' rays with their marks. So both hands type at once, and
a hand on the keyboard leaves the other the mouse of the canvas behind it.

- **A pull on a key** presses it, as the trigger is pulled. The key is held until the trigger is
  released, wherever the ray goes then. Backspace, Left and Right repeat while held: after 0.5 s, every
  50 ms. Each press is a tick on the hand. A key a ray is on is lighter, and a held key, and Shift or
  Caps while on, are in the accent color.
- **A pull on the line** puts the field's caret at the place of the text nearest to it
  (`InputTextState::SetSelection`), from where the text was drawn in the frame before.
- **What a key gives the field:** a character is typed (`io.AddInputCharactersUTF8`), so the field's
  filter takes it as a typed one: a number field drops a letter. Backspace, Left and Right are their
  keys. Clear is Ctrl+A, which selects the whole text, then Backspace. Done leaves the field with its
  text: Enter on a one-line field, and Ctrl+Enter on a multiline one, where Enter starts a new line
  (the other way round with `ImGuiInputTextFlags_CtrlEnterForNewLine`). Nothing on the keyboard gives
  the field back the text it had, as no VR keyboard does; a physical keyboard's Escape still does.
- **One stroke a frame** (`KeyStrokeQueue`): the keys of a stroke are pressed in one frame and let go of
  in the next, and text is given in a frame of its own. The layer has ImGui take every event in the
  frame it is given in (`ConfigInputTrickleEventQueue` off), so a key pressed and let go of in one frame
  would not be seen as pressed, and the field takes the strokes in the order they were typed.
- **The close binding** (`setCloseBinding`), on a hand whose ray is on the keyboard or on a canvas,
  leaves the field as Done does. A mod gives its Back binding, so Back closes the keyboard, and skips
  its own Back while `isOpen()`: Back would otherwise leave a page with a field still edited.

## Where it is

- **Under the canvas of the field** at first: the canvas whose window holds the field (by its window's
  name), or the first interactive canvas when a popup holds it. It is as wide as `widthShare` of that
  canvas, its top a gap under the canvas's bottom edge, in the canvas's plane, and tilted back by
  `tiltDegrees`, so it faces up toward the player. The canvas is taken as the flat quad it is laid out
  as, also when it is curved. It is bent as that canvas is, around a cylinder of its radius: it is
  about as far from the player, so the two read as one surface. It stays as far from the player as the canvas: nearer, it comes down to
  the hands, which then point at its back from under it, and the pointer meets a canvas from its front only.
- **Moved by the player** with the world-move binding (`vrui::UIManager::setWorldMoveBinding`): it is a
  root in the world (`attachPresetToWorldAtHMD`), and its panel reports the hands on it
  (`markPointedAt`), so vrui moves it as any panel in the world, level, smoothly, from the hand that
  holds it.
- **Kept beside the canvas:** each frame its place is set from the canvas's
  (`vrui::UIManager::setWorldPlace`), so it moves with the canvas. While a hand moves it, its place from
  the canvas is taken from where it is (`getWorldPlace`), and kept for the session: the next keyboard
  is shown there too. It is not saved.
- **Placed a frame ahead:** vrui gives a root its place in its frame update, before the ImGui frame, so a
  keyboard shown in one frame is drawn from the next, where it was put.

`KeyboardStyle` has its size, tilt and colors; Mods Config FO4 VR gives it the colors of its panel.

## The keys of the game's window

The framework subclasses the game's window procedure (`SetWindowLongPtr`) and hands its keyboard
messages (`WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN`, `WM_SYSKEYUP`, `WM_CHAR`) to Dear ImGui's Win32
backend, `ImGui_ImplWin32_WndProcHandler`. That backend maps a Windows key to ImGui's key and character
events, as it does for the flat game's ImGui menus (F4SEMenuFramework, DearModdingUI). It needs the
`win32-binding` feature of vcpkg's `imgui` and `ImGui_ImplWin32_Init` with the window. Every message
goes on to the game's procedure.

The window is found by its class, `Fallout4VR`, which `WinMain` registers (`0xD8DF37`, image-relative
in `Fallout4VR.exe` 1.2.72.0) and looks for to keep a second copy of the game from starting
(`0xD80BA3`). It is hooked when the ImGui context is created, so only once a canvas is shown, and only
when its messages come on the thread that builds the ImGui frame: ImGui's input queue is not
thread-safe. The window is made by the thread that runs the game's main loop, so they do; were they
not, the framework would log it and hand no keys on.

When the window loses focus (`WM_KILLFOCUS`), the keys ImGui holds down are let go, as the release of a
key then goes to another window: a Backspace that stayed down would repeat in the field. ImGui's own
answer to a lost focus is not used, as it lets go of the mouse button too, which is the pointer's.

Mouse messages are not handed on, and `ImGui_ImplWin32_NewFrame` is not called: the pointer stays the
canvas's mouse, and the canvas's display stays the layer's.

A physical keyboard types into a field this way. So does any keyboard overlay that sends Windows keys
to the focused window, such as Virtual Desktop's, XSOverlay's, OVR Toolkit's or Desktop+'s, on any
runtime, while the game's window has focus.

## To check in the headset

- That the keyboard opens under the field's canvas, facing up toward the player, and both hands type.
- That a key typed with a ray does not also reach the game, and that a physical key typed into a field
  does not either: F4SEMenuFramework keeps the keys from the flat game by not passing the window's
  messages on while its menu is open.
