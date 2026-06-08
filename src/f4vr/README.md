# `f4vr/` — Fallout 4 VR game utilities

Namespace: `f4cf::f4vr`

The bridge to the running game: finding and manipulating scene nodes, reading player/weapon/menu
state, the VR skeleton and player-node layout, Scaleform/HUD access, and the reverse-engineered RVAs
that make it all work.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`F4VRUtils.h`](F4VRUtils.h) | The workhorse: node search (`findNode`/`findAVObject`), visibility, transform updates (`updateDown`/`updateTransforms`), player/weapon state (`IsWeaponDrawn`, `isInPowerArmor`, `isSwimming`…), settings (`isLeftHandedMode`, `getIniSetting`), UI (`showMessagebox`/`showNotification`), and `.nif` loading. |
| [`F4VROffsets.h`](F4VROffsets.h) | All reverse-engineered RVAs (`REL::Relocation` / offsets) the framework calls into. |
| [`PlayerNodes.h`](PlayerNodes.h) | `PlayerNodes` struct — the 43 VR reference `NiNode*`s at `PlayerCharacter + 0x6E0` (wands, weapon offsets, Pip-Boy, HMD, scope, etc.). |
| [`F4VRSkelly.h`](F4VRSkelly.h) | `SkellyBones` — string names for 100+ skeleton bones and finger-pose scalars. |
| [`BSFlattenedBoneTree.h`](BSFlattenedBoneTree.h) | Access to the engine's flattened bone tree for fast per-bone transforms. |
| [`ScaleformUtils.h`](ScaleformUtils.h) | GFx menu/HUD manipulation: read values, toggle visibility, drive list menus, dispatch events. |
| [`GameMenusHandler.h`](GameMenusHandler.h) | `BSTEventSink` for menu open/close; query helpers (`isVatsActive`, `isInScopeMenu`, `isFavoritesMenuOpen`) + a callback. |
| [`F4VRThumbstickControls.h`](F4VRThumbstickControls.h) | Enable/disable the player's analog thumbstick by swapping the controls deadzone INI settings. |
| [`PapyrusGatewayBase.h`](PapyrusGatewayBase.h) | Base class to *call Papyrus from C++* via a registration script handshake (single instance). |
| [`DebugDump.h`](DebugDump.h) | Diagnostic dumps (UI tree, skeleton, geometry, world, nodes) triggered by `sDebugDumpDataOnceNames`. |
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

Accessing the VR player nodes (lives at `PlayerCharacter + 0x6E0`; see
[`PlayerNodes.h`](PlayerNodes.h) for the full 43-node layout and the wand/weapon convenience
accessors like `getPrimaryWandNode()`):

```cpp
RE::NiNode* hmd = getPlayerNodes()->HmdNode;
```

## Notes

- After moving or re-parenting nodes, call the appropriate `updateDown*` / `updateTransforms*`
  helper so the engine recomputes world transforms — skipping this is the usual cause of
  "the node moved but nothing rendered."
- RVAs in [`F4VROffsets.h`](F4VROffsets.h) are version-specific (Fallout 4 VR 1.2.72). The
  authoritative source is `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` and the
  f4sevr 0.6.21 / FRIK references in the modding reference library.
- The full `PlayerNodes` layout (all 43 nodes with byte offsets) is also documented in the reference
  library's `F4VR-CommonFramework_RE_REFERENCE.md`.
