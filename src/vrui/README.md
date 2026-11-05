# `vrui/` — VR UI widget system

Namespace: `f4cf::vrui`

An in-world UI: panels and buttons attached to game nodes (a wand, the wrist, the HMD, the world),
pressed by the player's fingertip. One `UIManager`, `g_uiManager`, owns the tree and runs input and
drawing each frame.

An element is drawn one of two ways, and the two mix in one layout:

- **NIF widgets:** a `.nif` mesh per button, its face in a texture atlas that
  [`vrui_atlas.py`](../../nif-tools/vrui_atlas.py) builds. Part of the game's scene graph.
- **Panels:** no mesh. Text, images and chrome composed at runtime and drawn by
  [`render/`](../render/README.md). They can size to their content and change their text every frame.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Classes

**Tree and layout**

| Class | Extends | What it is |
|-------|---------|------------|
| [`UIElement`](UIElement.h) | | The base: position, rotation, scale, visibility, size, children, layout and frame hooks. Also [`UIPadding`](UIElement.h). |
| [`UIContainer`](UIContainer.h) | `UIElement` | Lays its children out in a row or column, centered or one way. `setBoxStyle` draws a box around them. |
| [`UIToggleGroupContainer`](UIToggleGroupContainer.h) | `UIContainer` | Radio buttons, over [`UIToggleable`](UIToggleable.h), so NIF and panel toggles share a group. |
| [`UIManager`](UIManager.h) | | `g_uiManager`: attach and detach, the placement presets, input, drawing. |
| [`UISkeletonHandler`](UISkeletonHandler.h) | | Reads the skeleton: the index fingertips that press, and the wrist preset's place. |
| [`setFRIKHandPointing`](UIHandPointing.h) | | Points the pressing hand through FRIK's API. |

**NIF widgets**

| Class | Extends | What it is |
|-------|---------|------------|
| [`UIWidget`](UIWidget.h) | `UIElement` | A `.nif` with collision. `setDisabled` blocks presses and draws a disabled overlay. |
| [`UIButton`](UIButton.h) | `UIWidget` | Pressable: `setOnPressHandler`. |
| [`UIToggleButton`](UIToggleButton.h) | `UIWidget` | On and off, with `btn-border.nif` around it while on. |
| [`UIMultiStateToggleButton`](UIMultiStateToggleButton.h) | `UIWidget` | Cycles N states, a NIF each. |

**Panels**

| Class | Extends | What it is |
|-------|---------|------------|
| [`UIPanel`](UIPanel.h) | `UIElement` | The base: background, border, rounding and padding; the content is the subclass's. Hidden behind the world by default. Sizes to its content when the subclass can measure it ([`UIPanelSizing`](UIPanel.h)). |
| [`UITextPanel`](UITextPanel.h) | `UIPanel` | Rows of wrapping text: per-row color, height, underline, tab stops, and [`TextSpan`](UITextPanel.h)s for colors or an image inside a line. |
| [`UIImagePanel`](UIImagePanel.h) | `UIPanel` | One image (loose or BA2), tinted, contained or stretched. |
| [`UIButtonPanel`](UIButtonPanel.h) | `UIPanel` | A button of up to three lines of text and/or an image, pressed like `UIButton`. |
| [`UIToggleButtonPanel`](UIToggleButtonPanel.h) | `UIButtonPanel` | On and off, with a ring outside its border while on. |
| [`UIMultiStateToggleButtonPanel<State>`](UIMultiStateToggleButtonPanel.h) | `UIButtonPanel` | Cycles a `std::map<State, UIButtonPanelContent>` in key order. |
| [`UIPanelStyle`](UIPanelStyle.h) | | The look in one value: content color, background, border, rounding, padding. `F4VR_PANEL_STYLE` and `F4VR_BUTTON_STYLE` are the house looks; `imgui::UIImGuiPanel` takes it too. |

**Shared:** [`UIPressable`](UIPressable.h) (the disabled state of both kinds of button),
[`UIToggleable`](UIToggleable.h) (the on state of both kinds of toggle),
[`BindingPrompt`](BindingPrompt.h) (a controller binding as a text span), and the helpers in
[`UIUtils.h`](UIUtils.h).

## Quick start

`g_uiManager` exists before `onGameLoaded()`: build the UI there or later, and never call
`initUIManager()`. A `UIContainer` lays its children out, so nothing is positioned by hand. Keep the
elements you update as `shared_ptr` members.

```cpp
void MyMod::openPanel()
{
    using namespace vrui;

    _modeTgl = std::make_shared<UIToggleButton>("ui-example\\btn-example-1.nif");
    _modeTgl->setOnToggleHandler([this](UIToggleButton*, bool on) { setMode(on); });

    auto saveBtn = std::make_shared<UIButton>("ui-common\\btn-save.nif");
    saveBtn->setOnPressHandler([this](UIWidget*) { save(); });

    auto exitBtn = std::make_shared<UIButton>("ui-common\\btn-exit.nif");
    exitBtn->setOnPressHandler([this](UIWidget*) { closePanel(); });

    // a centered row, 0.3 between the buttons
    auto row = std::make_shared<UIContainer>("Row", UIContainerLayout::HorizontalCenter, 0.3f);
    row->addElement(_modeTgl);
    row->addElement(saveBtn);
    row->addElement(exitBtn);

    // the root stacks rows upward, at 0.4 scale
    _panel = std::make_shared<UIContainer>("Panel", UIContainerLayout::VerticalUp, 0.4f);
    _panel->addElement(row);

    g_uiManager->attachPresetToPrimaryWandTop(_panel, { 0, 0, 0 });
}

void MyMod::closePanel()
{
    if (_panel) {
        g_uiManager->detachElement(_panel, /*releaseSafe*/ true); // released next frame
        _panel.reset();
        _modeTgl.reset();
    }
}
```

A NIF path is under the mod's `Meshes\<ModName>\`. For panels instead of meshes, swap in the panel
classes: their handlers have the same names, taking the panel.

- **Each frame:** the framework updates the UI after the mod's `onFrameUpdate()` returns, while the
  player is loaded. So a press handler runs after the mod's frame, and the mod sees its effect the next
  one. A mod that needs the UI updated at a point of its own frame calls `g_uiManager->onFrameUpdate()`
  there, and the framework skips it that frame.
- **Detaching** mid-frame can be unsafe: `detachElement(element, true)` releases it next frame.
- **Radio buttons:** put the toggles in a `UIToggleGroupContainer`. A panel toggle that is on draws a
  ring 0.1 units outside its border (`setToggleFrame(color, thickness, gap)`), so leave that much
  padding.

## Panel sizing

The constructor picks how a panel is sized ([`UIPanelSizing`](UIPanel.h)):

```cpp
auto fit   = std::make_shared<vrui::UITextPanel>("Status");             // width and height fit the text
auto wrap  = std::make_shared<vrui::UITextPanel>("Help", 8.0f);         // fixed width, height fits the wrapped text
auto fixed = std::make_shared<vrui::UITextPanel>("Readout", 8.0f, 4.0f); // fixed; lines past the bottom are dropped
fit->setMaxWidth(10.0f);                                                // fit-content wraps past this width

auto icon  = std::make_shared<vrui::UIImagePanel>("Icon", 3.0f);        // fixed width, height from the image
auto photo = std::make_shared<vrui::UIImagePanel>("Photo");             // the image's size, 100 px per unit
photo->setMaxWidth(4.0f);                                               // ...scaled down past this width

auto btn = std::make_shared<vrui::UIButtonPanel>("Tuning");             // buttons and toggles too:
btn->setFitWidth(true);                                                 // width fits the label, never narrower than tall
btn->setMaxWidth(6.0f);                                                 // ...past this the label shrinks

auto gui = std::make_shared<imgui::UIImGuiPanel>("Readout", 8.0f);      // ImGui panels take the same constructors
```

An `imgui::UIImGuiPanel` measures its content only by drawing it, after layout, so it is laid out at
the size of the frame before: a change reaches the layout one frame late, and its first frame is
measured without being shown. A dimension that follows the content is laid out in all the room it may
grow to (the max width, or the ~21-unit atlas).

## Text

- **Wrapping:** at spaces, to the width it has; a word too long for a line is broken. `'\n'` starts a
  line. `'\t'` moves to the next tab stop from the line start (`setTabWidth`), so columns line up.
- **Rows:** each can set its own `textHeight`, and give `spans`, pieces in colors of their own, instead
  of one `text`.
- **Images in a line:** a span can be an image, sized from the text height (`imageHeight`, 1.5x by
  default), centered on the capitals, wrapping like a word. `color` tints it, and `tintWithText` tints
  it with the row's text color.
- **Re-layout:** the rows callback runs each frame, but the text is laid out again only when a row's
  text, spans or height, the width, the text height or the tab width changes, or an image loads.

```cpp
rows.push_back({ .spans = { { "PRESS " }, { .image = "f4cf\\bindings\\right-trigger.dds", .tintWithText = true }, { " TO FIRE" } } });
```

### Binding prompts

Never write a button's name into the UI: it is wrong once the player rebinds it.
[`appendBindingPrompt`](BindingPrompt.h) turns an `InputBinding` into a span:

```cpp
rows.push_back({ .spans = { { "TURN ON/OFF BY " } } });
appendBindingPrompt(rows.back().spans, config.headActivation.primary);
```

- **The controller:** primary and offhand become left or right by the player's handedness, and the
  buttons are named as that controller prints them (the runtime's `A` is `X` on the left).
- **The icon:** from `Textures\<ModName>\f4cf\bindings\`. A press or tap is the button alone; the rest
  have a mark (a clock for a long press, an arrow down for a hold, 2x, an arrow up for a release,
  ripples for a touch). A thumbstick push shows its arrow, the stick moved (`any`, `updown`,
  `rightleft`) shows arrowheads around its rim, and a chord is two icons with " + ".
- **Words:** only the system button, which has no icon (`bindingLabel`).
- **`samePrompt(a, b)`:** whether two bindings read the same, for a list of several.

## One box around several panels

Some content is more than one panel, as a two-line label with a binding's icon beside it. Give their
container a box style and it draws one box around them:

```cpp
auto label = std::make_shared<vrui::UITextPanel>("Label"); // bare: no box of its own
label->setStyle(vrui::UIPanelStyle{ .color = vrui::F4VR_PANEL_STYLE.color, .padding = vrui::UIPadding::all(0) });
label->setContent([](auto& rows) { rows.emplace_back("TOGGLE"); rows.emplace_back("SELFIE"); });

auto button = std::make_shared<vrui::UITextPanel>("Button");
button->setStyle(vrui::UIPanelStyle{ .color = vrui::F4VR_PANEL_STYLE.color, .padding = vrui::UIPadding::all(0) });
button->setContent([](auto& rows) { rows.push_back({}); vrui::appendBindingPrompt(rows.back().spans, binding); });

auto box = std::make_shared<vrui::UIContainer>("Selfie", vrui::UIContainerLayout::HorizontalCenter, 0.3f);
box->setBoxStyle(vrui::F4VR_PANEL_STYLE);
box->addElement(label);
box->addElement(button);
```

- The box is the style's background, border and rounding; its content color is not used.
- Its padding is between the elements and the box's edge, and the container grows by it and the border.
- A layout that places its elements each frame shifts them by half the difference of opposite paddings,
  so the box stays where the layout anchors it. A manual layout's elements stay put, boxed where they are.
- An empty box is not drawn. A box is drawn under every panel in it, and an outer box under an inner one.

## Assets

- **A panel** needs no mesh, only a texture if it shows an image: `setImage("Data\\Textures\\MyMod\\icon.dds")`,
  or a path under the mod's `Textures\<ModName>\`. `mod-template` ships icons in
  `Textures\<ModName>\f4cf\vrui\` (save, reset, exit, config, wiki, debug spheres) and the binding
  prompts' in `f4cf\bindings\`, drawn by [`tools/gen_binding_icons.py`](../../tools/gen_binding_icons.py).
  Text uses the framework's font, or the mod's TTF at `Data\Interface\<ModName>\<ModName>.ttf`
  ([`render/`](../render/README.md#text)).
- **A NIF widget** needs a mesh and an atlas. `mod-template` ships two: `ui-common` (back, exit, save,
  reset, the toggle frames and the disabled overlay, which the NIF classes use) and `ui-example`, a demo.

### Making a texture

Match what the framework ships: **uncompressed 32-bit BGRA**, both sides a multiple of 4 (the UI icons
are 140x100, the activation icons 256x256).

- **No compression for loose icons.** An icon is mostly its alpha edge, which block compression is worst
  at, and there is nothing to win: all the loose UI textures of the framework and a mod come to ~3 MB
  against 0.7 MB as BC3. BC3 is for the atlases, where a 1024x1024 sheet makes it worth it.
- **Mipmaps by use, not size.** A panel icon, read at about one size near the hand, has none. The
  activation icons, seen in the world from any distance, have a full chain, since minifying a texture
  without them shimmers. Give one mips if it is seen from across a room.

### Making an atlas

Draw each button as a PNG in a folder (the file name is the NIF name), then:

```
python nif-tools/vrui_atlas.py pack my_buttons --texture-subpath MyMod --name ui-common --output data/mod
```

It packs the images into `Textures\MyMod\ui-common.DDS` and writes `Meshes\MyMod\ui-common\<image>.nif`
for each, with its UV rectangle, its size (`W:<w> H:<h>` in the root name) and the texture path inside
it. `--texture-subpath` is required, usually the mod's name. Needs Pillow. All the options, and
`unpack`: [nif-tools](../../nif-tools/README.md).

## Pressing

- **The finger:** the tip of an index finger presses, read from the skeleton by
  [`UISkeletonHandler`](UISkeletonHandler.h). Either hand can: a button is tested against the nearer
  finger, and that hand points and gets the haptic. While a finger is close, the button stays with it.
- **A UI on a hand** is pressed only by the other hand, as its own finger can't reach it.
- **No hand drawn** (no FRIK, or the hand away from the controller): the controller presses, and a small
  gold sphere marks the point. `bVRUIShowFingerTip` in `[Debug]` shows it on the fingertip too.
- **Starting:** a finger interacts only from in front of the button's face, within 15 units, while the
  player looks at it (the head in front of the face, the button within 40° of where the head faces).
  Only then does the hand point and can the finger push.
- **Keeping on:** until the finger is 20 units away or 12 behind the face, or the button is 55° off the
  head's direction, so a press can go through the button and back. The values are in
  [`UIElement.cpp`](UIElement.cpp).
- **A push** needs the finger to cross the face from the front, on the button. After a press, or after
  going in beside the button, the finger has to come back out in front first, so a finger coming up
  from behind moves nothing, not even the button next to the one it pressed.
- **From behind a UI:** the hand doesn't point as the finger comes out in front. It points once the
  finger turns back toward a button by 0.7 units, or after it moved 15 units away and came back.
- **Pointing:** the hand points while its finger is near any button, so it doesn't let go between
  presses. The pose is asked of [FRIK](https://github.com/rollingrock/Fallout-4-VR-Body) through its
  API, under the tag `<mod name>_UI`, a little above FRIK's default priority, so it wins over the mod's
  own pose on that hand. Without FRIK nothing is posed. A mod that poses hands itself takes over with
  `g_uiManager->setHandPointingHandler(...)`.
- **Game menus:** while one takes the controllers' input (`f4vr::isGameMenuTakingInput`: pause, a message
  box, the Pip-Boy, lockpicking, other mods' menus; not workshop build mode), nothing is pressed and no
  hand points. The UI stays shown, under the menu.

## Placing

- **Coordinates:** `setPosition(x, y, z)` is relative to the parent: x right, y forward, z up.
- **Turning:** a UI faces the way its attach node does. Give the root a rotation to turn it,
  `root->setRotation(MatrixUtils::getMatrixFromEulerAnglesDegrees(heading, roll, attitude))` (around x,
  y and z). The tree turns around the root's position, and a button is still pressed along its own
  depth. A child turns around its own position, on top of its parent; containers lay out children as if
  unturned.
- **Primary wand:** `attachPresetToPrimaryWandTop` / `Left`.
- **Offhand wand:** `attachPresetToOffhandWandTop` / `Right`, for the primary hand to press. Right is the
  side toward the other hand, mirrored in left-handed mode.
- **Offhand wrist:** `attachPresetToOffhandWrist(root, offset)` lays the UI on the inner wrist, along the
  forearm, kept there as the elbow moves; bending the wrist doesn't move it. It needs an arm drawn at the
  controller (FRIK); otherwise the UI sits at a fixed place by the controller. Power armor and
  left-handed mode have placements of their own, and a replaced skeleton needs nothing from the mod. The
  offset is a transform applied from that base, as are the dev layout's `Pos` and `Rot`.
- **HMD:** `attachPresetToHMDBottom`.
- **The world:** `attachPresetToWorldAtHMD(root)` puts the UI where the HMD is and leaves it there, level
  and facing where the head faced: `root->setPosition(0, 120, -20)` is 120 units ahead and 20 below.
  `recenterWorldElement(root)` brings it back in front of the player. With a distance,
  `attachPresetToWorldAtHMD(root, 350)` does that by itself once the player walks further than that from
  it. A hidden root is left where it is.

### Moving a world UI

`setWorldMoveBinding(binding)` lets the player move a world root: point at it, work the binding on the
pointing hand, and it follows the hand until the button is released. Off by default.

- **One binding** for every world root. `Hand::Any` is whichever hand points. Read it from config as any
  binding (`vrcf::parseInputBindingTable`).
- **When it takes hold:** when the binding fires, a press at once, a long press after its duration. A
  shorter press moves nothing, so a tap of the same button can be the mod's own, such as Back
  (`VRControllers.check(back, hand)`). A tap, double, release or axis binding never moves anything.
- **How it moves:** with the wand, around the point the ray met it, as a thing at the end of a stick.
  It stays level: it turns and tilts with the hand, but doesn't roll.
- **Smoothing:** `setWorldMoveSmoothing(seconds)`, the time to cover about two thirds of the way, 0.05 by
  default. Held from a distance the hand's shake is large: at 0.05 about a third of a fast shake gets
  through, at 0.15 about an eighth, and it trails more. 0 follows at once.
- **The wand is taken from the game** from press to release (owner `UIWorldMove`). A button already down
  when the ray reaches the root moves nothing until released.
- **Letting go** leaves the root where it is; recentering works as before. One hand moves a root at a
  time, and a hidden root is let go.
- **The ray:** vrui has none. An element marks that a hand's ray is on it, and where
  (`UIFrameUpdateContext::markPointedAt`), as an interactive
  [`imgui::UIImGuiPanel`](../imgui/README.md#a-panel-the-player-moves) does.
- **The log** says when a hand takes hold and lets go, and why; at debug level, why a press moved nothing.

## Dev layout

Tunes placement live, through a file, with nothing for the mod to call. Set `bVRUIDevLayout = true` in
`[Debug]` (it is not in the shipped config). [`UIDevLayout`](UIDevLayout.h) then keeps
`<mod>_DevLayout.ini` beside the config:

- **Sections:** one per attached root, named after it, with a line for the root and one per element,
  keyed by its path below the root (`Row1.SaveButton`). So a mod's roots need different names.
- **Applied every frame:** a saved edit reaches the game in a fraction of a second, and wins over a value
  the code sets each frame.
- **Kept:** a detached root's lines stay and apply again when a root of that name is attached, so moving
  between screens keeps what was tuned.
- **Added:** a missing line is added with the element's current values, a new screen's section at the
  end of the file. A removed line, section or file comes back the same way; remove a section while its
  root is detached to get the code's values back.
- **Comments** written by hand stay, with the line or section below them.
- **Fresh each time:** the file starts empty when the mode turns on, and is removed when it turns off.

A line has `Pos`, `Rot` (degrees around x, y, z), `Scale` and `Size`. A container has `Padding` and
`Layout` instead of `Size`, and a boxed one its box's `Pad`. Panels add `Pad:(t,r,b,l)`, and `MaxW`
while their width follows the content; text and button panels add their text sizes as `Text`; an ImGui
panel adds its curve radius as `Curve`, 0 when flat. A field deleted from a line is no longer applied.
