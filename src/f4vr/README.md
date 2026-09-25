# `f4vr/` — Fallout 4 VR game utilities

Namespace: `f4cf::f4vr`

The bridge to the running game: finding and manipulating scene nodes, reading player/weapon/menu
state, the VR skeleton and player-node layout, Scaleform/HUD access, and the reverse-engineered RVAs
that make it all work.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`F4VRUtils.h`](F4VRUtils.h) | The workhorse: node search (`findNode`/`findAVObject`), visibility, transform updates (`updateDown`/`updateTransforms`), player/weapon state (`IsWeaponDrawn`, `isInPowerArmor`, `isSwimming`, `getEquippedWeaponName`…), other actors (`getActorsWithinRangeOfPoint` for a radius query, `getActorFacingAngleTo` / `isActorFacing` for how far an actor is turned away from a world point, off its own heading — `Actor::GetEyeVector` returns unusable values on VR — `getDetectionLevel` for how well one actor currently perceives another — a continuous scale, not a flag: `< 0` unaware, `0`–`20` suspicious/investigating, `>= 20` actually seeing (~50-60 point blank) — `isInActiveCombat` (use instead of the `Actor::IsInCombat()` virtual, which is always false on VR), and `startCombat` to queue an actor into combat with a target), settings (`isLeftHandedMode`, `getIniSetting`), UI (`showMessagebox`/`showNotification`), and `.nif` loading. |
| [`EquippedWeaponHandler.h`](EquippedWeaponHandler.h) | Tracks the equipped weapon and detects when it (or power-armor state) changes; `detectChange(weaponNode)` returns true on change, then `weaponName()`/`isWeaponDrawn()`/`isInPowerArmor()`/`isMelee()` report the new state. Also tracks the throwable weapon (grenade/mine, present only mid-throw) via `detectThrowableChange()` + `throwableNode()`/`throwableName()`, and exposes `getEquippedWeaponNameExtended` (base game name + a "Rifle" suffix for pistol/rifle weapons). Detection only — the caller decides what to do on a change. |
| [`CollisionLayers.h`](CollisionLayers.h) | Havok collision-layer (`RE::COL_LAYER`) helpers: the layer in a collision-filter word (`getCollisionLayer`), its readable name (`getCollisionLayerName` — "static", "actorZone", "charController"…, which is what makes a physics-query result legible), a name-or-number token back to a layer (`parseCollisionLayer`), and layer bitmasks for "which layers does this query care about" sets — `collisionLayerMask({ kTrigger, kActorZone… })` (`constexpr`, so a fixed set is free and a misspelling is a compile error), `parseCollisionLayerMask` for a set that comes from config, and `isCollisionLayerInMask` to test a filter word against either. |
| [`F4VROffsets.h`](F4VROffsets.h) | All reverse-engineered RVAs (`REL::Relocation` / offsets) the framework calls into. |
| [`PlayerRotation.h`](PlayerRotation.h) | Turns the player the way their VR comfort settings say (`iRotationType:VR` and friends, read live off the engine's `Setting` objects) — including where the game refuses to, since vanilla turning is a player-controls input handler and dialogue/menus take it away while a mod can still see the stick. Feed `turnByThumbstick(axisX)` each frame for the whole behaviour (snap per flick, continuous turn, or nothing, plus the stick latch), or drive it with `snapTurn`/`smoothTurn`; `getYaw`/`setYaw`/`rotateBy` bypass the configured style. Rotates the VR world (room) transform, as the engine does — not the actor. See [`../../docs/tech/vr-player-rotation.md`](../../docs/tech/vr-player-rotation.md). |
| [`PlayerNodes.h`](PlayerNodes.h) | `getVRPlayerNodes()` — CommonLibF4's `RE::VRPlayerNodes`, the VR reference nodes at `PlayerCharacter + 0x6E0` (wands, weapon offsets, Pip-Boy, HMD, scope, etc.) — plus wand/weapon accessors like `getPrimaryWandNode()`. The older `PlayerNodes` struct / `getPlayerNodes()` are deprecated and removed in v0.5.0. |
| [`WandActivationSphere.h`](WandActivationSphere.h) | Reusable proximity "activation sphere": a sphere around a node that, while a wand is inside it, suppresses each opted-in binding's button (owner-keyed, per-binding `InputBinding::suppress`), pulses a one-shot entry haptic, and fires an `onActivated` callback when a bound press lands (with a per-binding activation haptic). Two optional visuals, each drawn never / always / only `WhenInside` a hand / `WhenAvailable` (any binding fed enabled) (`ActivationSphereVisibility`): a sphere styled per sphere by a `SphereStyle` (default = the `white-subtle` preset; a runtime change reloads a freshly styled clone) at a visual-only scale (draw it smaller than the interaction radius as an "inside" hint; `Frame::showZone` forces it on at the zone's true size), facing the player's heading or the world axes (`ActivationSphereOrientation`); and an icon (`ActivationIconStyle`: a DDS, tint, world size — default `f4cf\activation-icon-hand.dds`) drawn at the zone center facing the HMD, on one primitive-overlay layer shared by all spheres, fading in and out. The authored bundle is `WandActivationConfig` (zone + optional power-armor zone + two bindings + entry/per-binding haptics + per-visual visibility + sphere and icon styles), loaded from one INI section via `ConfigBase::loadWandActivationConfig`. The caller mirrors the zone transform for handedness. See [`../../docs/input-binding.md`](../../docs/input-binding.md#activation-spheres). |
| [`SphereStyle.h`](SphereStyle.h) | How a sphere visual looks (activation-sphere zones, vrui debug markers): a mesh plus the values `applySphereStyle` sets at runtime on its effect shader — color, alpha, glow, center/rim falloff opacity, and the texture. Each shape is restyled on a material of its own (a cloned mesh already owns one; a shared one is swapped for a private copy first). Named presets (`findSphereStylePreset`: `<color>-<strength>`, `debug`). Because the texture is set by code, the shipped meshes name no mod: every mod ships the same sphere mesh and textures from the `mod-template`. |
| [`EffectShaderMaterials.h`](EffectShaderMaterials.h) | Runtime values on a cloned mesh's effect shaders (`BSEffectShaderProperty` — glows, beams, sphere visuals): `makeEffectShaderMaterialsPrivate` gives each effect-shaded shape a material of its own (call on a fresh clone, before it is first attached), then `forEachEffectShaderMaterial` hands you each such material to write — base color, base color scale, falloff opacities — which is safe live on a shown mesh, so it tints or fades one in place. `applySphereStyle` is built on it. |
| [`F4VRSkelly.h`](F4VRSkelly.h) | `SkellyBones` — string names for 100+ skeleton bones and finger-pose scalars. |
| [`BSFlattenedBoneTree.h`](BSFlattenedBoneTree.h) | Access to the engine's flattened bone tree for fast per-bone transforms. |
| [`ScaleformUtils.h`](ScaleformUtils.h) | GFx menu/HUD manipulation: read values, toggle visibility, drive list menus, dispatch events. |
| [`GameMenusHandler.h`](GameMenusHandler.h) | `BSTEventSink` for menu open/close; query helpers (`isVatsActive`, `isInScopeMenu`, `isFavoritesMenuOpen`) + a callback. |
| [`F4VRThumbstickControls.h`](F4VRThumbstickControls.h) | Enable/disable the player's analog thumbstick by swapping the controls deadzone INI settings. |
| [`PapyrusGatewayBase.h`](PapyrusGatewayBase.h) | Base class to *call Papyrus from C++* via a registration script handshake (single instance). |
| [`DebugDump.h`](DebugDump.h) | Diagnostic dumps (UI tree, skeleton, geometry, world, nodes) triggered by `sDumpDataOnceNames`. |
| [`DebugInventory.h`](DebugInventory.h) | Bulk-add game items to the player inventory by category (weapons/throwables/ammo/armor/aid/misc). Spec starts with an operation (`get` = obtainable / `get-all` = everything / `print` = dry-run of get / `print-all` = dry-run of get-all) then `category[:filter]` tokens, where filter is a `\|`-separated `key=value` set (`name=`/`keyword=` any category, armor `slot=`/`class=`, weapon `class=melee/gun/unarmed`). Also a typed public API (`ItemFilter`, `iterateObjects`) whose `ItemFilter` carries an optional code `predicate` (an arbitrary `bool(const TESForm*)` AND-constraint, e.g. to print/add only forms matching caller-side logic). Triggered by `sAddItemsOnceNames`. |
| [`MiscStructs.h`](MiscStructs.h) | Small game structs not modeled by CommonLibF4VR. |

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
if (isInPowerArmor() || IsWeaponDrawn()) { /* ... */ }
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

## Notes

- After moving or re-parenting nodes, call the appropriate `updateDown*` / `updateTransforms*`
  helper so the engine recomputes world transforms — skipping this is the usual cause of
  "the node moved but nothing rendered."
- RVAs in [`F4VROffsets.h`](F4VROffsets.h) are version-specific (Fallout 4 VR 1.2.72). The
  authoritative source is `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` and the
  f4sevr 0.6.21 / FRIK references in the modding reference library.
- The node table is documented and maintained as `RE::VRPlayerNodes` in CommonLibF4
  (`RE/Bethesda/PlayerCharacter.h`). The deprecated `f4vr::PlayerNodes` / `getPlayerNodes()` alias
  it under older member names and are removed in v0.5.0.
