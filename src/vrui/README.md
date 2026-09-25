# `vrui/` — VR UI widget system

Namespace: `f4cf::vrui`

A 3D, in-world UI toolkit: panels and buttons attached to game nodes (a wand, the HMD, etc.), with
finger-collision interaction instead of a mouse cursor. A single `UIManager` owns the scene graph
and drives input + rendering each frame.

An element is drawn one of two ways, and they mix freely in the same layout:

- **NIF widgets** - a `.nif` mesh per button, its face baked into a texture atlas
  ([`vrui_atlas.py`](../../nif-tools/vrui_atlas.py) builds both). Part of the game scene graph.
- **Panels** - no mesh: text, images and chrome composed at runtime and drawn by
  [`render/`](../render/README.md) over the VR view. They can size themselves to their content and
  change their text every frame, which a baked NIF cannot.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Widget hierarchy

**Scene graph and layout**

| Class | Extends | Description |
|-------|---------|-------------|
| [`UIElement`](UIElement.h) | — | Base node: position/scale/visibility/size, parent/child transform, layout + frame-update hooks. Also [`UIPadding`](UIElement.h), the per-side spacing panels take. |
| [`UIContainer`](UIContainer.h) | `UIElement` | Groups multiple elements under one transform and lays them out (row/column, centered or directional). |
| [`UIToggleGroupContainer`](UIToggleGroupContainer.h) | `UIContainer` | Radio-button group (mutually exclusive). Works on [`UIToggleable`](UIToggleable.h), so NIF and panel toggles can share one group. |
| [`UIManager`](UIManager.h) | — | Singleton scene graph: attach/detach, wand/HMD presets, input dispatch, render. |
| [`UIModAdapter`](UIModAdapter.h) | — | Interface the mod implements so the UI knows the interaction bone + how to point the hand. |

**NIF widgets** - a mesh per element

| Class | Extends | Description |
|-------|---------|-------------|
| [`UIWidget`](UIWidget.h) | `UIElement` | A 2D panel built from a `.nif` mesh, with collision detection. Supports a disabled state (`setDisabled(...)`) that blocks pressing and renders a "disabled" overlay on top. |
| [`UIButton`](UIButton.h) | `UIWidget` | Pressable widget; `setOnPressHandler(...)` callback, hover/pressed states. |
| [`UIToggleButton`](UIToggleButton.h) | `UIWidget` | On/off toggle; draws `btn-border.nif` around itself while on. |
| [`UIMultiStateToggleButton`](UIMultiStateToggleButton.h) | `UIWidget` | N-way cycle toggle, a NIF per state. |

**Panels** - composed at runtime, drawn by the primitive renderer

| Class | Extends | Description |
|-------|---------|-------------|
| [`UIPanel`](UIPanel.h) | `UIElement` | Base: a rectangle whose chrome - background, border, corner rounding, padding - it draws, leaving the content to the subclass. Occluded by the world by default; sizes to its content when the subclass can measure it ([`UIPanelSizing`](UIPanel.h)). Not interactive on its own. |
| [`UITextPanel`](UITextPanel.h) | `UIPanel` | Rows of text, each a paragraph that wraps: per-row color, height, underline, tab stops, and [`TextSpan`](UITextPanel.h) pieces - including inline images - for several colors or an icon inside a sentence. |
| [`UIImagePanel`](UIImagePanel.h) | `UIPanel` | One image, from a path the engine loads (loose file or BA2), tinted, contained or stretched. |
| [`UIButtonPanel`](UIButtonPanel.h) | `UIPanel` | A pressable button built from up to three lines of text and/or an image, pressing exactly like `UIButton`. |
| [`UIToggleButtonPanel`](UIToggleButtonPanel.h) | `UIButtonPanel` | On/off toggle; draws a ring outside its own border while on. |
| [`UIMultiStateToggleButtonPanel<State>`](UIMultiStateToggleButtonPanel.h) | `UIButtonPanel` | N-way cycle over a `std::map<State, UIButtonPanelContent>`, in key order. |
| [`UIPanelStyle`](UIPanelStyle.h) | — | The whole look as one aggregate (content color, background, border, rounding, padding). `F4VR_PANEL_STYLE` / `F4VR_BUTTON_STYLE` are the house looks; `imgui::UIImGuiPanel` takes the same value. |

**Shared interfaces**

| Class | Description |
|-------|-------------|
| [`UIPressable`](UIPressable.h) | The disabled state both kinds of button share, so code can enable/disable any mix of them. |
| [`UIToggleable`](UIToggleable.h) | The on/off state both kinds of toggle share - what `UIToggleGroupContainer` drives. |

Supporting: [`BindingPrompt`](BindingPrompt.h) (a controller binding as a text span with its icon),
[`UIElement` helpers in `UIUtils.h`](UIUtils.h), and the [`UIDebugWidget`](UIDebugWidget.h) for
visualizing interaction points.

## How it works

1. The mod provides a `UIModAdapter` — it answers *"where is the finger?"*
   (`getInteractionBoneWorldPosition`) and *"point the hand for me"* (`setInteractionHandPointing`).
2. Build elements (widgets/buttons) and attach them via the global `g_uiManager`, either to an
   explicit `NiNode*` or with a preset (primary wand top/left, HMD bottom).
3. Call `g_uiManager->onFrameUpdate(adapter)` every frame. The manager tests the interaction bone
   against each pressable widget, fires press callbacks, and updates transforms.

## Quick start

A small wand-mounted config panel — a toggle and two buttons in a row — modeled on the
[Immersive Flashlight](https://github.com/ArthurHub/F4VR-ImmersiveFlashlight) config menu. Three
parts: a `UIModAdapter`, building the panel, then driving and closing it.

> `g_uiManager` is created **for you** by the framework before `onGameLoaded()` runs — never call
> `initUIManager()` yourself. Build your UI in/after `onGameLoaded()`.

### 1. Implement a `UIModAdapter`

The adapter tells the UI where the "finger" is and how to make the hand point. The interaction bone
typically comes from [FRIK](https://github.com/rollingrock/Fallout-4-VR-Body) (full-body IK), which
exposes finger tracking and posing:

```cpp
class MyUIAdapter : public vrui::UIModAdapter
{
public:
    RE::NiPoint3 getInteractionBoneWorldPosition() override
    {
        return FRIKApi::inst->getIndexFingerTipPosition(FRIKApi::Hand::Offhand);
    }

    void setInteractionHandPointing(bool primaryHand, bool toPoint) override
    {
        const auto hand = primaryHand ? FRIKApi::Hand::Primary : FRIKApi::Hand::Offhand;
        if (toPoint) {
            FRIKApi::inst->setHandPoseCustomFingerPositions("MyMod_UI", hand, 0, 1, 0, 0, 0);
        } else {
            FRIKApi::inst->clearHandPose("MyMod_UI", hand);
        }
    }
};
```

### 2. Build a panel

Each button/toggle here is a `.nif` mesh; swap in the panel classes below to compose one from text
and a DDS instead, with no mesh to build. A `UIContainer` lays its children out automatically (rows
or columns), so you don't position each element by hand. Keep the elements you'll query or update as
`shared_ptr` members; the rest can be local.

```cpp
void MyMod::openPanel()
{
    using namespace vrui;

    _modeTgl = std::make_shared<UIToggleButton>("MyMod\\ui_btn_mode_1x1.nif");
    _modeTgl->setOnToggleHandler([this](UIToggleButton*, bool on) { setMode(on); });

    auto saveBtn = std::make_shared<UIButton>("MyMod\\ui_btn_save_1x1.nif");
    saveBtn->setOnPressHandler([this](UIWidget*) { save(); });

    auto exitBtn = std::make_shared<UIButton>("MyMod\\ui_btn_exit_1x1.nif");
    exitBtn->setOnPressHandler([this](UIWidget*) { closePanel(); });

    // Lay the three out centered in a row, 0.3 padding between them.
    auto row = std::make_shared<UIContainer>("Row", UIContainerLayout::HorizontalCenter, 0.3f);
    row->addElement(_modeTgl);
    row->addElement(saveBtn);
    row->addElement(exitBtn);

    // Root container stacks rows bottom-to-top at 0.4 scale.
    _panel = std::make_shared<UIContainer>("Panel", UIContainerLayout::VerticalUp, 0.4f);
    _panel->addElement(row);

    // Attach above the primary wand (offset {0,0,0}).
    g_uiManager->attachPresetToPrimaryWandTop(_panel, { 0, 0, 0 });
}
```

For mutually-exclusive options (radio buttons), add `UIToggleButton`s to a
[`UIToggleGroupContainer`](UIToggleGroupContainer.h) instead of a plain `UIContainer`. The group
works on the [`UIToggleable`](UIToggleable.h) interface, so
[`UIToggleButtonPanel`](UIToggleButtonPanel.h) - the toggle drawn by the primitive renderer, with
text and an image composed at runtime instead of a NIF - can go in the same group, even mixed with
NIF toggles. Like the NIF toggle's white frame, a panel toggle that is on draws an extra ring around
the button, outside its own border, 0.1 units deep by default - so leave at least that much padding in
the container. `setToggleFrame(color, thickness, gap)` changes it.

For an N-way cycle, [`UIMultiStateToggleButtonPanel<State>`](UIMultiStateToggleButtonPanel.h) is the
panel counterpart of `UIMultiStateToggleButton`: instead of a NIF per state it takes a
`std::map<State, UIButtonPanelContent>` - each state's text lines and image - and a press moves to the
next state in key order.

### 3. Drive it each frame, then tear it down

```cpp
void MyMod::onFrameUpdate()
{
    MyUIAdapter adapter;
    vrui::g_uiManager->onFrameUpdate(&adapter);   // hit-tests, fires handlers, renders

    // per-frame UI logic, e.g. react to current state:
    if (_panel) {
        _statusMsg->setVisibility(_modeTgl->isToggleOn());
    }
}

void MyMod::closePanel()
{
    if (!_panel) {
        return;
    }
    g_uiManager->detachElement(_panel, /*releaseSafe*/ true);  // released next frame
    _panel.reset();        // drop the shared_ptr members you kept
    _modeTgl.reset();
}
```

Members held on the mod (or a dedicated UI class):

```cpp
std::shared_ptr<vrui::UIContainer>    _panel;
std::shared_ptr<vrui::UIToggleButton> _modeTgl;
std::shared_ptr<vrui::UIWidget>       _statusMsg;
```

## Assets

A **panel** needs no mesh - only a texture, if it shows an image at all, named by a path the engine
resolves like any other: `setImage("Data\\Textures\\MyMod\\icon.dds")`, or a partial path under the
mod's own `Textures\<ModName>\`. `mod-template` ships a set of common icons at
`Textures\<ModName>\f4cf\vrui\` (save, reset, exit, config, wiki, debug spheres), plus the
controller icons the binding prompts draw under `f4cf\bindings\`. Text is drawn in the framework's
font, or the mod's own TTF at `Data\Interface\<ModName>\<ModName>.ttf` - see
[`render/`](../render/README.md#text).

Making your own is just exporting a `.dds` and pointing `setImage()` at it - any Fallout 4 texture
the engine can load will draw. Match what the framework ships, though: **uncompressed 32-bit BGRA,
no mipmaps**, with both dimensions a multiple of 4 (the shipped UI icons are 140x100, the activation
icons 256x256). A loose icon is mostly defined by its alpha edge, which is exactly what block
compression is worst at, and there is nothing to win by compressing it: every loose UI texture the
framework and a mod like Immersive Flashlight ship comes to about 3 MB uncompressed against 0.7 MB
as BC3, and they are sampled over a few hundred screen pixels each, so neither the memory nor the
bandwidth is measurable. BC3 is for the packed atlases below, where a 1024x1024 sheet makes the
trade worth it.

Mipmaps are the choice that *does* matter, and it goes by how the image is used, not by its size: a
panel icon is read at roughly one size near the hand and ships without them, while the activation
icons - drawn in the world at whatever distance you are standing - ship with a full chain, because
minifying a texture that has none both shimmers and scatters its texture-cache reads. Give one mips
if it will be seen from across a room.

A **NIF widget** needs a mesh and a slot in a texture atlas. Pre-built ones ship in `data/vrui/`:
button grids `ui_btn_NxM.nif` (up to 5×5) and message panels `ui_msg_NxM.nif` (up to 6×2). Re-skin
by editing DDS textures under `data/vrui/Textures/`, or pick a different grid cell by adjusting UV
offsets in the `BDEffectShaderProperty` with NifSkope.

### Creating your own button atlas

Hand-building a combined texture and a NIF per button is tedious — the
[`nif-tools/vrui_atlas.py`](../../nif-tools/vrui_atlas.py) tool does it for you. Draw each button or
label as its own PNG in a folder (the file name becomes the NIF name you reference in code), then:

```
python nif-tools/vrui_atlas.py pack my_buttons --texture-subpath MyMod --name ui-common
```

`--texture-subpath` (required) is the subfolder under `Textures\`, usually your mod's name. It
bin-packs the images into a single `ui-common.DDS` and writes a ready-to-use `<image>.nif` per
sprite — each with the correct UV rectangle, size (`W:<w> H:<h>` in the root name), and the texture path
`Textures\MyMod\ui-common.DDS` baked in. The output is a deployable tree —
`Textures\MyMod\ui-common.DDS` and `Meshes\MyMod\ui-common\<image>.nif` — so point `--output`
at your mod's data folder and load the nifs via `UIButton`/`UIWidget` as shown above. Requires
`pip install Pillow`; full options (and the reverse, `unpack`) are in the
[nif-tools README](../../nif-tools/README.md).

## Panel sizing

[`UITextPanel`](UITextPanel.h) and [`UIImagePanel`](UIImagePanel.h) are drawn by the primitive
renderer rather than a `.nif`, so they can size themselves to their content during layout
([`UIPanelSizing`](UIPanel.h)). The constructor you call picks the mode:

```cpp
auto fit   = std::make_shared<vrui::UITextPanel>("Status");             // width + height fit the text
auto wrap  = std::make_shared<vrui::UITextPanel>("Help", 8.0f);         // fixed width, height fits the wrapped text
auto fixed = std::make_shared<vrui::UITextPanel>("Readout", 8.0f, 4.0f); // fixed; lines past the bottom are dropped
fit->setMaxWidth(10.0f);                                                // fit-content wraps past this width

auto icon  = std::make_shared<vrui::UIImagePanel>("Icon", 3.0f);        // fixed width, height from the image's proportions
auto photo = std::make_shared<vrui::UIImagePanel>("Photo");             // the image's own size, 100 px per unit (as vrui_atlas.py bakes NIFs)
photo->setMaxWidth(4.0f);                                               // ...scaled down past this width, proportions kept

auto btn = std::make_shared<vrui::UIButtonPanel>("Tuning");              // buttons (and toggle / multi-state panels) too:
btn->setFitWidth(true);                                                 // height as given, width fits the label (never narrower than tall)
btn->setMaxWidth(6.0f);                                                 // ...and past this the label shrinks again

auto gui = std::make_shared<imgui::UIImGuiPanel>("Readout", 8.0f);      // ImGui panels take the same constructors
auto fitGui = std::make_shared<imgui::UIImGuiPanel>("Status");          // ...and max width
fitGui->setMaxWidth(10.0f);
```

An `imgui::UIImGuiPanel` can only measure its content by drawing it, which happens after layout, so
it is laid out at the size its content took up the frame before - a change in the content reaches the
layout one frame late - and its first frame is measured without being shown. A dimension that follows
the content lays it out in all the room it may grow to (the max width, or the ~21-unit atlas): wrapped
text wraps there, and a full-width item stretches the panel out to it.

Text wraps at spaces to the width it has (a word too long for a line is broken inside it), and a
`'\n'` in a row starts a new line. A `'\t'` moves to the next tab stop, measured from the line start
(`setTabWidth`), so columns line up across rows. A row can name its own `textHeight`, and can be
given `spans` - pieces in colors of their own - instead of one `text`. A span can be an image instead
of text, for a button prompt inside a sentence: it is sized from the text height (`imageHeight`, 1.5x
by default), centred on the capitals, and wraps like a word that can't be broken:

```cpp
rows.push_back({ .spans = { { "PRESS " }, { .image = "f4cf\\bindings\\right-trigger.dds", .tintWithText = true }, { " TO FIRE" } } });
```

An image draws in its own colours; the span's `color` tints it, and `tintWithText` tints it with the
colour its row's text is drawn in - what a white icon meant to match its text wants.

For a controller prompt, [`BindingPrompt`](BindingPrompt.h) turns an `InputBinding` straight into a
span, so a prompt says what is actually bound rather than a hard-coded key:

```cpp
rows.push_back({ .spans = { { "TURN ON/OFF BY " } } });
appendBindingPrompt(rows.back().spans, config.headActivation.primary);
```

It resolves primary/offhand to the left or right controller by the player's handedness and names
buttons as the controller prints them (the runtime's `A` is `X` on the left one), drawing the icons
mod-template ships in `Textures\<ModName>\f4cf\bindings`. A binding no icon covers - a chord, a touch,
the system button - falls back to words (`bindingLabel`), and `samePrompt` tells whether two bindings
read the same, for a caller listing several at once.

The rows callback runs once per frame during layout; the text is only re-laid-out when a row's text,
spans or height, the width, the text height or the tab width changes (or an image's texture finishes
loading).

## Notes

- `g_uiManager` is created by the framework before `onGameLoaded()`; mods never call `initUIManager()`.
- Containers lay children out automatically by their `UIContainerLayout` (horizontal/vertical,
  centered or directional) — prefer that over positioning each element manually.
- Coordinates on `UIElement::setPosition(x, y, z)` are **relative to the parent**: x = right(+)/left(−),
  y = forward(+)/back(−), z = up(+)/down(−).
- Detaching mid-frame can be unsafe; `UIManager::detachElement(element, releaseSafe=true)` defers the
  release to the next frame.
- A dev layout mode tunes placement live through the INI. Calling
  `UIManager::enableDevLayoutViaConfig()` writes the attached tree out to the config's
  `[VRUI_DevLayout]` section and saves it. While that section has anything in it, the manager
  re-applies it to the tree every frame, so editing a line and saving the INI reaches the running
  game on the next one; emptying the section turns the mode off. Each element's line holds
  `Pos`, `Scale` and `Size`; containers add `Padding` and `Layout`, panels add `Pad:(t,r,b,l)` (and
  `MaxW` while their width follows the content), and text and button panels add their text sizes as
  `Text`. Delete a field from a line and it is simply no longer applied.
