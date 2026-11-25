# `imgui/controls/` — the small controls of an ImGui panel in VR

Namespace: `f4cf::imgui::controls`

What a panel's controls are drawn with, in one look: text cut with an ellipsis or wrapped, drawn marks and icons,
frames and outlines, buttons, the switcher, dialogs, the slider, the stepper, the dropdown with its list, and the text
input. A panel on a [canvas](../README.md) builds its rows and
controls from them, so two mods' panels read as one UI. They draw into the current ImGui window, inside a canvas's
content.

> Part of the [F4VR Common Framework](../../README.md) source tree.

## Files

| File | What it is |
| ---- | ---------- |
| [`Controls.h`](Controls.h) | `controls::get()`: the controls' `ControlsStyle`, its colors packed for ImGui, the `Tone` of a value, the thumbstick, Back, and what a control holds from frame to frame. |
| [`Text.h`](Text.h) | One line of text cut with an ellipsis, a text with a suffix, underlined ranges, and `TextPiece`s wrapped as one text. |
| [`TextRange.h`](TextRange.h) | `TextRange` and `TextAlign`, plain std, so game-free code that finds or lays out text has them without ImGui. |
| [`Marks.h`](Marks.h) | The warning, error and info marks, the dot, the check, the Reset and Undo icons, and the arrows. |
| [`Buttons.h`](Buttons.h) | `button` in its `ButtonLook` (primary, secondary, quiet, danger), `iconButton`, and the switcher, which only draws. |
| [`Dialogs.h`](Dialogs.h) | `beginDialog`, a modal dialog in the middle of the display, and `dialogButtons` along its bottom. |
| [`Choices.h`](Choices.h) | The stepper, the dropdown's box, and the list of options a dropdown opens, with its suffixes, header line and action entry. |
| [`Sliders.h`](Sliders.h) | The slider: a track between step arrows and the box of its value, dragged by the trigger and stepped by the arrows and the thumbstick. |
| [`TextInputs.h`](TextInputs.h) | The text input: ImGui's field in a control's box, typed on the framework's keyboard, written as it is left or as it is typed. |
| [`Frames.h`](Frames.h) | A control's box and frame, outlines, a button's frame and ring, the step button, the selected bar, a row's look under the ray, a section's line, a tag, and where a popup opens. |

## Style

`ControlsStyle` is the controls' colors by role (accent, muted, track, hover, pressed, glow, rule, frame border, the
warning and error tones with the text on each, the popup and the scrim behind a dialog) and the sizes a panel may set
otherwise: the control's height, the frame's rounding and border, the popup's rounding, and the detail font size.
Its defaults are the framework's green, the look of `vrui::F4VR_PANEL_STYLE`. Sizes are in the canvas's pixels.

```cpp
#include "imgui/controls/Controls.h"

namespace controls = imgui::controls;

auto style = controls::get().style();
style.accent = render::Color::rgba(255, 180, 60);
controls::get().setStyle(style);
```

- `setStyle` replaces all of it and packs its colors once; `colors()` has them as ImGui's `ImU32`.
- A `Tone` picks a value's colors: `Normal` the accent, `Warning` and `Error` their own. `tone(t)` gives the color and
  the color of a text on its fill. A frame of the `Error` tone has an outline in the error color.
- The marks, icons and arrows have the sizes of their design (`MARK_WIDTH`, `ARROW_SIZE`, ...), and the color of their
  role: the icons and arrows the accent, Undo the warning, each mark its own. A function that takes a color draws in it.

## Drawing

```cpp
#include "imgui/controls/Frames.h"
#include "imgui/controls/Text.h"

const auto box = controls::controlBox(left, top, width);
controls::drawFrame(box, invalid ? controls::Tone::Error : controls::Tone::Normal);
controls::drawTextIn(box, "Medium", controls::get().colors().accent, 0.5f);
```

- **Current font:** text is drawn in ImGui's current font and size; a `TextPiece` may name its own face and size.
- **Wrapped text:** `drawWrappedText` sets a paragraph of pieces in a width by its align; `countTextLines` and
  `drawTextPieces` lay out pieces a line of the current size apart, the last line cut with an ellipsis. A
  `TextPiece` with a `mark` is that mark, with the room of a space after it.
- **Disabled:** inside `ImGui::BeginDisabled`, the frames, marks and `drawText` are faded. A `TextPiece` is drawn in
  its color as given, so a caller passes `ImGui::GetColorU32(color)` for a faded piece.
- **Popups:** `popupTop` places a popup under its box, over it, or at the bottom of its window, never above its top.

## Buttons and dialogs

```cpp
#include "imgui/controls/Dialogs.h"

if (controls::button("Save", box, controls::ButtonLook::Primary)) { save(); }

if (controls::beginDialog("Delete?", asked, controls::Tone::Error)) {
    ImGui::TextUnformatted("Delete the preset?");
    const std::array buttons{ controls::DialogButton{ .label = "Delete", .look = controls::ButtonLook::Danger },
        controls::DialogButton{ .label = "Cancel" } };
    std::array<ImRect, 2> boxes;
    const int pressed = controls::dialogButtons(buttons, boxes);
    if (pressed >= 0) { ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
```

- **A press** is ImGui's: a button returns `true` as the trigger is released on it, and never inside `BeginDisabled`.
  The caller gives the press its pulse.
- **A dialog** is open while `asked` is true and closes as it turns false, as after Back. Its border is in its tone's
  color, and it draws its own popup background, rounding and scrim, so it looks the same in a panel of any ImGui style.
- **Boxes:** a control is given its box, or `dialogButtons` fills `boxes`, so the caller can record where each one is
  (for devbench).
- **The switcher** only draws: an off and an on cell with the one of its value marked in its tone. The row it is in
  takes the press.

## The slider

```cpp
#include "imgui/controls/Sliders.h"

const controls::SliderRange range{ .min = 0.0f, .max = 100.0f, .step = 1.0f };
const auto id = ImGui::GetID("Volume");
const controls::SliderRow row{ .pointed = ImGui::IsItemHovered(), .started = ImGui::IsItemActivated(), .held = ImGui::IsItemActive() };
const auto edit = controls::sliderEdit(id, right, top, row, _volume, range);
if (edit.write) { _volume = edit.value; }
const auto text = controls::fitValueText(std::format("{:.0f} %", edit.value), std::format("{:.0f}", edit.value));
controls::drawSlider(id, right, top, row, edit.value, range, controls::Tone::Normal, text);
```

- **The row is the press:** the caller's item, often its whole row, takes the trigger; the slider reads from the
  pointer's place whether a pull is on its track or an arrow. `sliderEdit` comes first, so the caller writes the value
  and draws it in the look that gives.
- **A drag** follows the ray until the trigger is let go, also beside the track, and `edit.write` is true only then,
  so a store writes once. A pull on the grab itself does not move it.
- **A step arrow** steps as it is pulled, again after 0.4 s and then faster while it is held on the arrow.
- **The thumbstick** of the pointing hand steps the slider whose row the ray is on, once the ray is there 0.3 s. A
  push that began with the ray elsewhere steps nothing until it is let go. The framework's layer reads it each frame:
  the mod passes nothing.
- **`edit.pulse`** says the hand gets its feedback: a pull took hold of the slider, or a step changed it. The caller
  plays it, with its own other press feedback.
- **Back:** `controls::get().back()` ends a drag with nothing written and returns whether it did, so the caller's Back
  does nothing else. `controls::get().reset()` forgets what the controls hold as a panel shows another page.
- **State** is kept by the `ImGuiID` given, in `controls::get()`, from one frame to the next: the framework's layer
  begins and ends the controls' frame around every canvas's content.
- **The value's text:** `fitValueText` gives the text with its unit in the current size, else in the detail size, else
  the number alone, with `withUnit` false so the caller can say the unit elsewhere.

## The stepper and the dropdown

```cpp
#include "imgui/controls/Choices.h"

const controls::ChoiceRow row{ .pointed = ImGui::IsItemHovered(), .held = ImGui::IsItemActive(), .pressed = ImGui::IsItemDeactivated() && ImGui::IsItemHovered() };
if (const auto picked = controls::stepperEdit(right, top, row, _quality, QUALITIES.size())) { _quality = *picked; }
controls::drawStepper(right, top, row, QUALITIES[*_quality], controls::Tone::Normal, _quality, QUALITIES.size());

const auto listId = ImGui::GetID("list");
if (row.pressed) { ImGui::OpenPopupEx(listId); }
controls::drawDropdown(box, QUALITIES[*_quality], {}, controls::Tone::Normal, controls::get().isListOpen());
const auto drawn = controls::drawList(listId, box, QUALITIES, _quality);
if (drawn.picked) { _quality = *drawn.picked; }
```

- **A stepper's press:** its buttons go to the option before and after and stop at the ends; a press elsewhere on its
  row goes to the option after, from the last to the first. With no option chosen the one after is the first, and the
  one before is the last where `noneStepsBackToLast`. The thumbstick of the pointing hand steps the stepper whose row
  the ray is on once for each push.
- **A dropdown** only draws its box; the caller opens its list with `ImGui::OpenPopupEx` on a press of its row.
- **The list** is a popup at the box, under it or over it as `popupTop` places it, showing at most
  `LIST_MAX_OPTIONS` before it scrolls, from the chosen option as it opens. A press on an option picks it and closes
  it. `ListExtras` gives each option a muted suffix, a line over the options that stays as they scroll, and an action
  entry under a rule, which is the place after the options. A decoration draws more of each option.
- **What it gives back:** `ListDrawn` has the option picked and the one under the ray, each option's box with the
  action's last, and the scroll of the options, so the caller can record them (for devbench).
- **Back:** `controls::get().back()` closes the open list as the next frame begins, with nothing picked, after a
  drag in its order. It closes the list and what opened over it, and not a popup it is in, as an editor.
  `controls::get().reset()` forgets what the controls hold and closes an open list, and `closePopups()` closes every
  popup but a modal one, as a panel shows another page.

## The text input

```cpp
#include "imgui/controls/TextInputs.h"

const auto id = ImGui::GetID("Name");
const auto box = controls::controlBox(right - 392.0f, top, 392.0f);
const auto edit = controls::textEdit(id, box, rowPressed, _name, { .maxLength = 32 });
if (edit.write) { _name = *edit.write; }
if (!controls::get().isTextEdited(id)) { controls::drawTextInput(box, _name, controls::Tone::Normal); }
```

- **A press starts the edit:** ImGui's field takes it from the next frame, with the caret at the end, and the
  framework's [keyboard](../README.md#text-fields-and-the-keyboard) is shown for it. `textEdit` is called in every
  frame, also while its box is scrolled out of sight: a frame that does not draw it ends the edit with nothing written.
- **When it writes:** as the field is left by the keyboard's Done, Enter or a press beside it, with a text other than
  the one it started with. A `live` one writes each change as it is typed, as a search's query. A physical keyboard's
  Escape writes nothing; ImGui gives its field back the text it started with, which a live one does not write, so the
  typed text stays.
- **What it takes:** `TextChars` keeps only the characters of a whole number or of any number, and `maxLength` cuts
  the text at that many UTF-8 characters as it is typed or pasted, with the count at the right of the box (`12 / 32`).
- **The look:** a framed one is a control's frame with an accent outline while edited, and `drawTextInput` draws it
  idle in a tone. A bare one (`TextLook{ .framed = false, .padding = 0.0f }`) is only the field, in a box whose look
  its caller draws, as a search field's.
- **Back:** `controls::get().back()` ends the edit with nothing more written as the next frame begins, after a drag and
  a list in its order; `endTextEdit()` does so for a text set by other means. `reset()` keeps the edit, as a search in
  a header outlives the page; a field that is not drawn any more ends it.
- **`edit.pulse`** is for the press that starts it; the keyboard ticks each key itself.
