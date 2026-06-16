# `vrui/` — VR UI widget system

Namespace: `f4cf::vrui`

A 3D, in-world UI toolkit: panels and buttons rendered as `.nif` meshes attached to game nodes
(a wand, the HMD, etc.), with finger-collision interaction instead of a mouse cursor. A single
`UIManager` owns the scene graph and drives input + rendering each frame.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Widget hierarchy

| Class | Extends | Description |
|-------|---------|-------------|
| [`UIElement`](UIElement.h) | — | Base node: position/scale/visibility, parent/child transform, frame-update hook. |
| [`UIWidget`](UIWidget.h) | `UIElement` | A 2D panel built from a `.nif` mesh, with collision detection. Supports a disabled state (`setDisabled(...)`) that blocks pressing and renders a "disabled" overlay on top. |
| [`UIButton`](UIButton.h) | `UIWidget` | Pressable widget; `setOnPressHandler(...)` callback, hover/pressed states. |
| [`UIToggleButton`](UIToggleButton.h) | `UIWidget` | On/off toggle. |
| [`UIMultiStateToggleButton`](UIMultiStateToggleButton.h) | `UIWidget` | N-way cycle toggle. |
| [`UIContainer`](UIContainer.h) | `UIElement` | Groups multiple elements under one transform. |
| [`UIToggleGroupContainer`](UIToggleGroupContainer.h) | `UIContainer` | Radio-button group (mutually exclusive). |
| [`UIManager`](UIManager.h) | — | Singleton scene graph: attach/detach, wand/HMD presets, input dispatch, render. |
| [`UIModAdapter`](UIModAdapter.h) | — | Interface the mod implements so the UI knows the interaction bone + how to point the hand. |

Supporting: [`UIElement` helpers in `UIUtils.h`](UIUtils.h) and the [`UIDebugWidget`](UIDebugWidget.h)
for visualizing interaction points.

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

Each button/toggle is a `.nif` mesh; a `UIContainer` lays its children out automatically (rows or
columns), so you don't position each element by hand. Keep the elements you'll query or update as
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
[`UIToggleGroupContainer`](UIToggleGroupContainer.h) instead of a plain `UIContainer`.

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

Pre-built meshes and textures ship in `data/vrui/`: button grids `ui_btn_NxM.nif` (up to 5×5) and
message panels `ui_msg_NxM.nif` (up to 6×2). Re-skin by editing DDS textures under
`data/vrui/Textures/`, or pick a different grid cell by adjusting UV offsets in the
`BDEffectShaderProperty` with NifSkope.

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

## Notes

- `g_uiManager` is created by the framework before `onGameLoaded()`; mods never call `initUIManager()`.
- Containers lay children out automatically by their `UIContainerLayout` (horizontal/vertical,
  centered or directional) — prefer that over positioning each element manually.
- Coordinates on `UIElement::setPosition(x, y, z)` are **relative to the parent**: x = right(+)/left(−),
  y = forward(+)/back(−), z = up(+)/down(−).
- Detaching mid-frame can be unsafe; `UIManager::detachElement(element, releaseSafe=true)` defers the
  release to the next frame.
- A dev layout mode (`UIManager::enableDevLayoutViaConfig`) reads element positions from the config's
  `debugVRUIProperties` so you can tune placement live via INI reload.
