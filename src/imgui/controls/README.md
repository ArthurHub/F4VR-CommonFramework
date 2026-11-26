# `imgui/controls/` — the small controls of an ImGui panel in VR

Namespace: `f4cf::imgui::controls`

What a panel's controls are drawn with, in one look: text cut with an ellipsis or wrapped, drawn marks and icons,
frames and outlines, buttons, the switcher, dialogs, the slider, the stepper, the dropdown with its list, the text
input, the search field, the rows of a list with the headings of its sections, and the rows of a tree. A panel on a
[canvas](../README.md) builds its rows and controls from them, so two mods' panels read as one UI. They draw into the
current ImGui window, inside a canvas's content.

> Part of the [F4VR Common Framework](../../README.md) source tree.

## Files

| File | What it is |
| ---- | ---------- |
| [`Controls.h`](Controls.h) | `controls::get()`: the theme, its colors packed for ImGui, the `Tone` of a value, the thumbstick, Back, and what a control holds from frame to frame. |
| [`Text.h`](Text.h) | One line of text cut with an ellipsis, a text with a suffix, underlined ranges, and `TextPiece`s wrapped as one text. |
| [`TextRange.h`](TextRange.h) | `TextRange` and `TextAlign`, plain std, so game-free code that finds or lays out text has them without ImGui. |
| [`Marks.h`](Marks.h) | The warning, error and info marks, the dot, the check, the Reset and Undo icons, and the arrows. |
| [`Buttons.h`](Buttons.h) | `button` in its `ButtonLook` (primary, secondary, quiet, danger), `iconButton`, and the switcher, which only draws. |
| [`Dialogs.h`](Dialogs.h) | `beginDialog`, a modal dialog in the middle of the display, `dialogButtons` along its bottom, and `beginRowPopup`, the popup that opens on a row. |
| [`Icons.h`](Icons.h) | `drawIcon`: the drawn icons as one set, a button's and the panel's, each scaled from its design box. |
| [`Choices.h`](Choices.h) | The stepper, the dropdown's box, and the list of options a dropdown opens, with its suffixes, header line and action entry. |
| [`Sliders.h`](Sliders.h) | The slider: a track between step arrows and the box of its value, dragged by the trigger and stepped by the arrows and the thumbstick. |
| [`TextInputs.h`](TextInputs.h) | The text input: ImGui's field in a control's box, typed on the framework's keyboard, written as it is left or as it is typed. |
| [`SearchFields.h`](SearchFields.h) | The search field: the magnifier, a placeholder or the query typed live, how many match, and Clear. |
| [`Frames.h`](Frames.h) | A control's box and frame, outlines, a button's frame and ring, the step button, the selected bar, a row's look under the ray, a section's line, a tag, and where a popup opens. |
| [`Rows.h`](Rows.h) | A row as wide as the rows of its list, the row of a heading that folds, the folding heading with its counts, and a section's heading with a rule. |
| [`Tree.h`](Tree.h) | A tree's row, set in by its level, with its arrow, name, tag, dot and count, selected or disabled, and the row of a group of its nodes. |
| [`Targets.h`](Targets.h) | `controls::targets()`: where the last frame's buttons, options and a mod's rows were, and how far its lists were scrolled, for devbench. |

## Theme

The controls draw in the [`imgui::Theme`](../README.md#theme): its colors by role (accent, muted, track, hover,
pressed, glow, rule, frame border, the warning and error tones with the text on each, the popup and the scrim behind
a dialog, the glass, the plate of a navigation pane, an important button), the control's height, the frame's rounding
and border, the popup's rounding, the detail font size, a slider's grab, and the spacing scale. Its defaults are the
framework's green, the look of `vrui::F4VR_PANEL_STYLE`. Sizes are in the canvas's pixels.

```cpp
#include "imgui/controls/Controls.h"

namespace controls = imgui::controls;

auto theme = controls::get().theme();
theme.accent = render::Color::rgba(255, 180, 60);
controls::get().setTheme(theme);
```

- `setTheme` replaces all of it, packs its colors once, gives the keyboard its colors, and has ImGui's style set from
  it as the next frame begins. `colors()` has the colors as ImGui's `ImU32`. The controls read both as they draw, so
  the next frame is in a new theme, with a slider's drag or an open list kept.
- A test's own ImGui context gets the theme's style with `applyImGuiStyle()`, before its first frame.
- devbench's `theme` action sets fields over the mod's theme (`overrideTheme`, internal), which stay over each
  `setTheme` until its `reset` (`clearThemeOverride`). `theme()` is the theme drawn, with those fields.
- **Gaps** inside a control and between its parts are steps of the theme's spacing scale (`space1` to `space6`): a
  list is padded `space2` with `space1` between its options, a dialog padded `space6`, a tree's level set in `space4`.
  The four a caller lines up with are functions of the theme: `headingTextInset()`, `sliderValuePadding()`,
  `textInputPadding()` and `treeIndent()`. A slider's grab is the theme's `grabWidth` and `grabRounding`, as high as
  `controlHeight` less `space2`.
- What stays a constant is the size of a drawn part or of one control's design: the marks, arrows and icons, a
  switcher's cell, a dialog's width, a tag's line, the tree's arrow column, and the timings.
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

## Icons

```cpp
#include "imgui/controls/Icons.h"

controls::drawIcon(controls::Icon::Cross, box.GetCenter(), controls::get().colors().accent);
controls::drawIcon(controls::Icon::Gear, ImVec2(left + 10.0f, middle), color, 20.0f);
```

- **One set:** Search, Cross (Close, Clear), Minimize and Vibrate (a haptic's Play: a dot with arcs going out), and the
  icons of a button: Play, Stop, Bolt, Target, Check, Window, External, Link, Gear, Import, Export, Copy, Eye, Hand,
  Sound, Pin, Refresh, Trash. `Marks.h` keeps the marks, the arrows, Reset and Undo.
- **`size`** is the box it is drawn in, `ICON_SIZE` (24) unless given. Each icon is drawn from the box it is designed
  in (`iconDesignBox`), strokes too: Search, Cross, Minimize and Vibrate in 24 with `ICON_THICKNESS` strokes, a
  button's icons in 20 with strokes of 2 and round ends, so a button's icon is as designed at 20 and a fifth larger at
  24. A sharp join, as the eye's tips, reaches a little past the box.
- Inside `ImGui::BeginDisabled` it is faded; an opaque stroke of a button's icon has round ends, a faded one square.

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
- **A row's popup**, as an editor that opens on a row: `beginRowPopup(id, row, width, height)` places a popup of that
  size centered on the row as far as the window lets it, under or over the row as `popupTop` places it, padded
  `space4` and outlined in the accent. The caller opens it with `ImGui::OpenPopupEx(id)` and ends it with
  `ImGui::EndPopup()` when it returns true.

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

## The search field

```cpp
#include "imgui/controls/SearchFields.h"

const auto drawn = controls::searchField("search", pos, width, _query, { .placeholder = "Search 11 mods", .count = _matches });
if (drawn.write) { _query = *drawn.write; }
if (drawn.cleared) { _query.clear(); }
```

- **The look:** `SEARCH_FIELD_HEIGHT` high, a control's track and outline, the magnifier, then the `placeholder` in the
  detail size while the query is empty and not edited, or the query, then the `count` in the detail size and Clear
  with its cross while there is a query. Under the ray it has a row's hover, and its magnifier and placeholder the
  accent; edited, its outline is the accent.
- **The caller keeps the query:** a press edits it on a live, bare [text input](#the-text-input), whose id is
  `ImGui::GetID(id)`, so `get().isTextEdited(ImGui::GetID(id))` says it is edited. `write` is the query as it is typed;
  `cleared` is Clear, after which the caller empties it, and which ends an edit. Back ends the edit and keeps the
  query, as does a physical keyboard's Escape.
- **`pointed`** and **`clearPointed`** say where the ray is, for a help line; **`pulse`** is for a press that started
  the edit or cleared.

## Rows and headings

```cpp
#include "imgui/controls/Rows.h"

controls::sectionHeading("Display", ImGui::GetCursorScreenPos().x + controls::rowWidth());

const auto pos = ImGui::GetCursorScreenPos();
const auto row = controls::addRow(true);
if (row.shown) { controls::drawText(pos.x + controls::headingTextInset(), pos.x + controls::rowWidth(), pos.y, 48.0f, color, "Quality"); }

const auto at = ImGui::GetCursorScreenPos();
const auto heading = controls::addFoldingRow("Advanced", _advancedFolded);
const std::array counts{ controls::MarkedCount{ .mark = controls::CountMark::Error, .text = "1 invalid", .color = controls::get().colors().error } };
if (heading.shown) { controls::drawFoldingHeading("Advanced", at, controls::rowWidth(), _advancedFolded, counts); }
```

- **A row** is as high as a control and as wide as `rowWidth()`: the window's width without the room of its scroll
  bar, so it does not change as the scroll bar shows, and without the indent of a section it is in. `addRow` adds it
  at the cursor and gives `Row{shown, pointed, started, held, pressed}`; a pressable one has the look of a row under
  the ray. A row that is not `shown` is scrolled out of the list: its caller draws nothing of it.
- **A folding heading:** `addFoldingRow(id, folded)` is a pressable row whose press flips `folded`, which the caller
  keeps, so it can fold or open a section by other means too. `drawFoldingHeading` draws the heading in it: the arrow,
  the text in the muted color, or in a mark's color after the error or warning mark, and a rule to the right edge,
  ended by its `MarkedCount`s in the detail size, as what a folded section hides.
- **A section's heading:** `sectionHeading` is its muted text and a rule that ends at the edge given, where the rows
  end, not in the room of the scroll bar. It is ImGui's separator text in the controls' look, in any ImGui style. It
  is its text with 8 px above and below, or as high as the `height` given, as a row's height for a heading in a row's
  place.
- **Text** in a row and a heading starts `headingTextInset()` in, so the headings and the rows' texts line up. The
  ranges `found` of a heading's text are underlined, as a search's matches.

## The tree's rows

```cpp
#include "imgui/controls/Tree.h"

ImGui::PushID(index);
const auto drawn = controls::treeRow({ .name = node.name, .level = level, .arrow = node.children.empty() ? controls::TreeArrow::None
    : open ? controls::TreeArrow::Open : controls::TreeArrow::Closed, .selected = index == _selected }, width);
if (drawn.pressed) { _selected = index; }
ImGui::PopID();

if (controls::treeGroupRow("Weapons", weapons.size(), _weaponsOpen, false, width).pressed) { _weaponsOpen = !_weaponsOpen; }
```

- **Only the row:** the caller walks its nodes, keeps which are open and selected, and gives each row its level and
  arrow; a row is the last item, so the caller takes its rect (for devbench). Each row needs its own ID, as
  `ImGui::PushID` of its node gives.
- **The look:** the name is set in `treeIndent()` for each level, up to `TREE_MAX_INDENT_LEVEL`, after an arrow column
  every row keeps, so the names of a level line up. The selected row is the selected bar, any other a row under the
  ray. A `tag` is a small outlined word at the row's right, a `count` in the detail size before it, and the `dot`
  follows the name, which ends in an ellipsis before all of them. The ranges `found` of the name are underlined.
- **`show`** scrolls a selected row into view where it is not whole in the shown rows, as after it is selected by
  other means.
- **A disabled row** is faded, with its arrow right, and has the look of a row under the ray, so a help shown for it is
  seen to be its; it is never pressed.
- **A group row** is muted: its arrow, its name, a rule, and the count of its nodes. A press is the caller's to open or
  close it.

## Targets

```cpp
#include "imgui/controls/Targets.h"

controls::targets().add("setting", setting.id, rowBox);
controls::targets().add("tree", mod.name, rowBox, pagePath);
controls::targets().addScroll("page");
```

Where a panel's targets were in its last frame, which devbench's [`targets` action](../../devbench/README.md) tells,
so a test with the controllers asks for a button or a row by a key it knows and points at it.

- **Recording** is off until devbench's first `targets` call, so a game with no test pays one check a target.
- **A target** is a `kind` and a `key`, both texts, and a `path` where its key alone does not tell it, with its rect:
  the part its window shows, in the canvas's pixels from its top left, the space `pointer().state()` is in. One its
  window does not show, as a row scrolled away, is not kept. A **scroll** is an `area`, how far its list is scrolled
  and how far it can be: `addScroll(area)` of the window drawn, or with the two a list in its own window gives.
- **The controls record themselves:** `button`, and each of `dialogButtons`, as `target::BUTTON` by its label;
  `drawList`'s options as `target::OPTION` by their text, its action entry by its own, and its scroll as
  `target::LIST_AREA`; `searchField` as `target::SEARCH`, `Field` while it is not edited and `Clear` while there is a
  query.
- **A mod records** a row whose key is its own, by kinds it names and documents: a setting's row by its id, a tree's
  row by its node and path, a section by its text.
- Each frame drops the last one's, as the controls' frame begins, and the layer gives each canvas its top left before
  its content. With two panels at once, a target does not say which canvas it is in.
