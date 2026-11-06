# `f4vr/` — Fallout 4 VR game utilities

Namespace: `f4cf::f4vr`

The bridge to the running game: scene nodes, player, weapon and menu state, other actors, the VR
skeleton and player nodes, Scaleform, and the reverse-engineered RVAs behind them. Each header's doc
comments are the detail; this page is the map and the traps.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | What it has |
|------|-------------|
| [`F4VRUtils.h`](F4VRUtils.h) | The workhorse. Nodes: find (`findNode`, `findAVObject`), show and hide, update transforms (`updateDown`, `updateTransforms`), load a `.nif`. Player and weapon: `isWeaponDrawn`, `isInPowerArmor`, `isSwimming`, `getEquippedWeaponName`... Other actors: `getActorsWithinRangeOfPoint`, `getActorFacingAngleTo` / `isActorFacing`, `getDetectionLevel`, `isInActiveCombat`, `startCombat`. Settings: `isLeftHandedMode`, `getIniSetting`, the Pip-Boy's placement (`getPipboyPlacement`, `setPipboyPlacement`, which saves it as the game's menu does). UI: `showNotification`, `showMessagebox`, `isGameMenuTakingInput`. |
| [`EquippedWeaponHandler.h`](EquippedWeaponHandler.h) | Detects a change of the equipped weapon or of power armor: `detectChange()` each frame, then read `weaponName()`, `weaponNameExtended()` (a "Rifle" suffix for a weapon that is both), `isMelee()`... Also the throwable, which exists only mid-throw (`detectThrowableChange()`). Detection only: the caller decides what a change does. |
| [`PlayerNodes.h`](PlayerNodes.h) | `getVRPlayerNodes()`, CommonLibF4's `RE::VRPlayerNodes` (wands, HMD, Pip-Boy, scope...), and accessors such as `getPrimaryWandNode()`. |
| [`F4VRSkelly.h`](F4VRSkelly.h) | Bone names (`SkellyBones`), and `Skelly`: a bone's world transform by name, a hand's index fingertip. |
| [`BSFlattenedBoneTree.h`](BSFlattenedBoneTree.h) | The engine's flattened bone tree, for fast per-bone transforms. |
| [`PlayerRotation.h`](PlayerRotation.h) | Turns the player the way their comfort settings say, also where the game won't (dialogue, menus): `turnByThumbstick(axisX)` each frame, or `snapTurn` / `smoothTurn`. It rotates the VR room, not the actor ([design](../../docs/tech/vr-player-rotation.md)). |
| [`WandActivationSphere.h`](WandActivationSphere.h) | A gesture zone around a node, and its config section. See [Activation spheres](#activation-spheres). |
| [`SphereStyle.h`](SphereStyle.h) | How a sphere visual looks (activation spheres, the vrui fingertip marker): a mesh, and the color, glow, falloff and texture set on its shader at runtime. The presets (`findSphereStylePreset`). |
| [`EffectShaderMaterials.h`](EffectShaderMaterials.h) | Live values on a cloned mesh's effect shaders (glows, beams, spheres): `makeEffectShaderMaterialsPrivate` on a fresh clone, before it is attached, then `forEachEffectShaderMaterial` to tint or fade it in place. |
| [`CollisionLayers.h`](CollisionLayers.h) | Havok collision layers: a filter word's layer and its readable name, a name back to a layer, and layer masks (`collisionLayerMask({ ... })` at compile time, `parseCollisionLayerMask` from config). |
| [`GameMenusHandler.h`](GameMenusHandler.h) | Menu open and close events, and `isVatsActive`, `isInScopeMenu`, `isFavoritesMenuOpen`. |
| [`ScaleformUtils.h`](ScaleformUtils.h) | GFx menus and the HUD: read values, show and hide, drive list menus, dispatch events. |
| [`F4VRThumbstickControls.h`](F4VRThumbstickControls.h) | Turns the player's thumbstick movement off and on, through the controls' deadzone settings. |
| [`WandMovement.h`](WandMovement.h) | Controller-direction movement with the offhand controller held reversed: `alignTo(key, worldForward)` each frame inverts the offhand stick the game reads while the controller points more than 90° away from that forward. A call lasts through the next frame, so stopping the calls ends it. |
| [`PapyrusGatewayBase.h`](PapyrusGatewayBase.h) | Calls Papyrus from C++, through a script that registers itself. |
| [`DebugDump.h`](DebugDump.h), [`DebugInventory.h`](DebugInventory.h) | `aDumpDataOnce` and `aAddItemsOnce` ([debug settings](../../docs/debug-config.md)). `DebugInventory` also has a typed API (`ItemFilter` with a code `predicate`, `iterateObjects`). |
| [`F4VROffsets.h`](F4VROffsets.h) | Every RVA the framework calls into. |
| [`MiscStructs.h`](MiscStructs.h) | Small game structs CommonLibF4VR doesn't model. |

## Traps

- **Update after a move.** After moving or re-parenting nodes, call `updateDown` / `updateTransforms`, or
  the engine keeps the old world transforms: "the node moved, but nothing changed".
- **`Actor::IsInCombat()` is always false on VR.** Use `isInActiveCombat`.
- **`Actor::GetEyeVector` returns unusable values on VR.** `getActorFacingAngleTo` uses the actor's own
  heading instead, which is also what the AI turns toward a target.
- **Detection is a scale, not a flag.** `getDetectionLevel` is below 0 when unaware, 0 to ~20 when
  suspicious, and 20 and up when it sees the target (~50-60 point blank). An actor with no high process
  reports not detecting.
- **`startCombat` on an actor already fighting the target does nothing**, which is not a failure.
- **`isGameMenuTakingInput`** is true for a menu that pauses, uses the cursor, or is modal, which the VR
  UI stands down for. Workshop build mode sets none of those: pass `true` to count it too.
- **RVAs are for Fallout 4 VR 1.2.72.** The authority is f4sevr 0.6.21, then CommonLibF4VR's address
  library, then `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` in the modding reference library.

## Quick reference

```cpp
#include "f4vr/F4VRUtils.h"
using namespace f4cf::f4vr;

// Find a node by name under the player root, then hide it:
if (auto* n = findNode(playerRoot, "PrimaryWandLaserPointer")) {
    setNodeVisibility(n, /*show*/ false);
    updateTransforms(n);
}

// Branch on game/player state:
if (isInPowerArmor() || isWeaponDrawn()) { /* ... */ }
if (isLeftHandedMode()) { /* swap hands */ }

// Tell the player something:
showNotification("Saved.");
```

Accessing the VR player nodes (lives at `PlayerCharacter + 0x6E0`; the layout is CommonLibF4's
`RE::VRPlayerNodes`, and [`PlayerNodes.h`](PlayerNodes.h) adds wand/weapon convenience accessors
like `getPrimaryWandNode()`):

```cpp
RE::NiNode* hmd = getVRPlayerNodes()->hmdNode;
```

## Activation spheres

An activation sphere is a gesture: a zone around a node (a wand, the HMD, a prop on the body) that a
hand reaches into and presses a binding in. The player's side, every config key and preset, is
[`docs/activation-spheres.md`](../../docs/activation-spheres.md).

- **`WandActivationConfig`** is one config section: the zone and its power-armor variant, two bindings,
  the entry and per-binding haptics, when the sphere and the icon show, and their styles. Load it with
  `ConfigBase::loadWandActivationConfig(config, "Section", defaults)`.
- **`WandActivationSphere`** is the zone at runtime, one per gesture, constructed with an owner key.
  Each frame `onFrameUpdate(node, config, onActivated)` runs the proximity test, suppresses each
  `suppress` binding while a hand is inside (under that key), plays the haptics, applies a cooldown and
  draws the visuals. `onActivated(binding)` gets the binding that fired and returns whether it handled
  it; the first one handled wins the frame.
- **Gating:** a binding with no action in the current state (no weapon drawn) is passed as disabled for
  that frame through the `Frame` overload. `whenavailable` visuals then show only what will work.
- **Handedness:** the caller mirrors the zone transform for left-handed mode.
- **Where the sphere hangs:** the zone is measured from `node`, but a raw tracking node (HMD, wand)
  doesn't render children, so the sphere hangs under `sphereAttachNode` (the primary-hand UI node by
  default) and is moved to the zone each frame. Never attach it inside the player's 3D: character
  creation rebuilds that tree, and a sphere inside it crashes the game.
- **`Frame::showZone`** draws the sphere at the zone's true size whatever the config says, for a tuning
  mode that shows every zone.

The icon (`ActivationIconStyle`: texture, tint, size) faces the HMD and fades in and out, drawn on one
`render` layer shared by every sphere, so it needs no mesh. The sphere is a mesh restyled at runtime by
a [`SphereStyle`](SphereStyle.h); a style change reloads a freshly styled clone.

Because the sphere's texture is set by code, its mesh names no mod, and every mod ships the same files
from `mod-template`, under its own folder:

- `Meshes\<ModName>\f4cf\activation-sphere.nif`, the one mesh every preset draws on
- `Textures\<ModName>\f4cf\activation-sphere.dds` and `debug-sphere.dds`
- `Textures\<ModName>\f4cf\activation-icon-hand.dds` (the default icon), `activation-icon-ring.dds` and
  `activation-icon-circle.dds`
