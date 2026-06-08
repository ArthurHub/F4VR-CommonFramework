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
| [`UIWidget`](UIWidget.h) | `UIElement` | A 2D panel built from a `.nif` mesh, with collision detection. |
| [`UIButton`](UIButton.h) | `UIWidget` | Pressable widget; `setOnPressHandler(...)` callback, hover/pressed states. |
| [`UIToggleButton`](UIToggleButton.h) | `UIButton` | On/off toggle. |
| [`UIMultiStateToggleButton`](UIMultiStateToggleButton.h) | `UIButton` | N-way cycle toggle. |
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

```cpp
#include "vrui/UIManager.h"
#include "vrui/UIButton.h"
using namespace f4cf::vrui;

// once, at init:
initUIManager();   // creates g_uiManager

// build a button from a packaged mesh and wire its handler:
auto button = std::make_shared<UIButton>("data/vrui/ui_btn_1x1.nif");
button->setOnPressHandler([](UIButton*) { logger::info("pressed!"); });

// place it on top of the primary wand:
g_uiManager->attachPresetToPrimaryWandTop(button, RE::NiPoint3{ 0, 0, 0 });

// every frame (onFrameUpdate), with your UIModAdapter:
g_uiManager->onFrameUpdate(&myAdapter);
```

## Assets

Pre-built meshes and textures ship in `data/vrui/`: button grids `ui_btn_NxM.nif` (up to 5×5) and
message panels `ui_msg_NxM.nif` (up to 6×2). Re-skin by editing DDS textures under
`data/vrui/Textures/`, or pick a different grid cell by adjusting UV offsets in the
`BDEffectShaderProperty` with NifSkope.

## Notes

- Coordinates on `UIElement::setPosition(x, y, z)` are **relative to the parent**: x = right(+)/left(−),
  y = forward(+)/back(−), z = up(+)/down(−).
- Detaching mid-frame can be unsafe; `UIManager::detachElement(element, releaseSafe=true)` defers the
  release to the next frame.
- A dev layout mode (`UIManager::enableDevLayoutViaConfig`) reads element positions from the config's
  `debugVRUIProperties` so you can tune placement live via INI reload.
