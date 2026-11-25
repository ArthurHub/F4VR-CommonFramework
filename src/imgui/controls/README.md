# `imgui/controls/` — the small controls of an ImGui panel in VR

Namespace: `f4cf::imgui::controls`

What a panel's controls are drawn with, in one look: text cut with an ellipsis or wrapped, drawn marks and icons,
frames and outlines. A panel on a [canvas](../README.md) builds its rows and controls from them, so two mods' panels
read as one UI. They draw into the current ImGui window, inside a canvas's content.

> Part of the [F4VR Common Framework](../../README.md) source tree.

## Files

| File | What it is |
| ---- | ---------- |
| [`Controls.h`](Controls.h) | `controls::get()`: the controls' `ControlsStyle`, its colors packed for ImGui, and the `Tone` of a value. |
| [`Text.h`](Text.h) | One line of text cut with an ellipsis, a text with a suffix, underlined ranges, and `TextPiece`s wrapped as one text. |
| [`TextRange.h`](TextRange.h) | `TextRange` and `TextAlign`, plain std, so game-free code that finds or lays out text has them without ImGui. |
| [`Marks.h`](Marks.h) | The warning, error and info marks, the dot, the check, the Reset and Undo icons, and the arrows. |
| [`Frames.h`](Frames.h) | A control's box and frame, outlines, a button's frame and ring, the step button, the selected bar, a row's look under the ray, a section's line, a tag, and where a popup opens. |

## Style

`ControlsStyle` is the controls' colors by role (accent, muted, track, hover, pressed, glow, rule, frame border, the
warning and error tones with the text on each, the popup) and the sizes a panel may set otherwise: the control's
height, the frame's rounding and border, and the detail font size. Its defaults are the framework's green, the look of
`vrui::F4VR_PANEL_STYLE`. Sizes are in the canvas's pixels.

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
