# General RE Work — TODO

Backlog of reverse-engineering tasks that aren't tied to any specific port effort. These are things we want to know about Fallout 4 VR's binary that would unlock new mod functionality or remove blockers.

Unrelated to f4sevr porting (that's tracked in [f4sevr-port-checklist.md](f4sevr-port-checklist.md)).

---

## Conventions

For every entry below:

- **What:** the question or feature.
- **Why:** what unlocks if we figure it out.
- **How to start:** concrete first step in IDA / source.
- **Status:** `OPEN` (not started), `IN-PROGRESS` (someone working on it), `BLOCKED` (waiting on something), `DONE` (finished — link to commit/doc).

When you finish an item, move its writeup into a focused `*-investigation.md` doc (like `middlehigh-vr-shift-investigation.md`) and update the entry here to `DONE` with a link.

---

## High value

### PlayerCharacter VR-only state block (offset 0xB70..0xFE0)

- **Status:** OPEN
- **What:** RE the contents of the 0x470-byte VR-only block in `PlayerCharacter`. Currently declared as `std::byte vrPlayerStatePadding[0x470]` (per commit `760b8b5`).
- **Why:** This block almost certainly contains HMD pose, controller hand-bone refs, IK rig anchors, and other VR-essential state that VR mods (FRIK, scope mods, hand pose mods, body mods) need direct access to. Right now they get to it via guesswork or through PlayerNodes, not via typed CommonLibF4VR fields.
- **How to start:**
  1. Cross-reference [`Modding-Reference/F4VR/Analysis/gold/FRIK_RE_REFERENCE.md`](../../Modding-Reference/F4VR/Analysis/gold/FRIK_RE_REFERENCE.md) and other VR-aware mod analyses — see which offsets in 0xB70..0xFE0 they read.
  2. In IDA, find the `PlayerCharacter` constructor (likely near `PlayerCharacter::GetSingleton`) and look at the init pattern in the 0xB70 range — mirrors how we cracked MiddleHighProcessData.
  3. Ship one named field at a time, with `static_assert(offsetof(...) == 0x???)` per field. Same `#ifdef ENABLE_FALLOUT_VR` pattern.
- **Estimated commits:** unknown — could be 5-30 depending on how many fields are useful. Each is small.

### VR-vs-flat layout shifts in other commonly-touched structs

- **Status:** OPEN
- **What:** Look for other classes that may have undocumented VR layout shifts beyond `PlayerCharacter` and `MiddleHighProcessData`. Likely candidates: `PlayerCamera`, `BSGraphics`, animation/skeleton-related types.
- **Why:** Same pattern as MiddleHighProcessData — flat-FO4 RE will be wrong for VR, mods built on it will silently misread fields.
- **How to start:**
  1. Pick a CommonLibF4VR struct that's heavily used (BSAnimationGraphManager, ActorMover, BipedAnim, etc.).
  2. Find a small game-function getter in IDA that returns a known field offset.
  3. Compare returned offset against CommonLib's declaration. If they disagree, follow the recipe in [middlehigh-vr-shift-investigation.md](middlehigh-vr-shift-investigation.md).
- **Heuristic:** focus on structs in `CommonLibF4/include/RE/Bethesda/` whose authors had no way of testing against VR (i.e., almost all of them).

### Vtable audit for VR-specific overrides

- **Status:** OPEN
- **What:** Verify that `TESObjectREFR`, `Actor`, `PlayerCharacter`, and other key polymorphic types have the same vtable layout in VR as in flat. We confirmed slot 0xA0 disagreed (commit `3be2d23`); there may be more.
- **Why:** Wrong vtable interpretation = calling the wrong game function = crashes or silent corruption.
- **How to start:**
  1. Open the `.rdata` section in IDA, find the vtable for `class TESObjectREFR` (search for the RTTI name).
  2. Walk the vtable entries one at a time, F5'ing each function; compare what it does to what CommonLibF4VR's declared signature at that slot expects.
  3. Mismatches = either rename per actual function (like commit 15) or add an inline alias (like commit 16).
- **Scope cap:** Don't try to do all classes. Just `TESObjectREFR` (124 vtable entries) and `Actor` (~70 add'l entries) is enough.

---

## Medium value

### `BSOpenVR` / OpenVR plumbing inside the EXE

- **Status:** OPEN
- **What:** Find and document the integration between Fallout4VR.exe and `openvr_api.dll`. Where does the game read controller pose? How does it dispatch button events into Papyrus or the input system?
- **Why:** VR mods that want to inject custom input (custom button bindings, gesture detection, alternative wand models) need to know where to hook.
- **How to start:**
  1. In IDA, find calls to imported `openvr_api.dll` functions — `IVRSystem::GetControllerState`, `IVRCompositor::WaitGetPoses`, etc.
  2. Trace the call sites back to the game's input dispatch.
  3. Document the hook points in a new `vr-input-pipeline.md`.

### Scope/zoom rendering pipeline

- **Status:** OPEN
- **What:** Finish documenting the 4 scope-zoom approaches in `Modding-Reference/F4VR/knowledge-base/scope_zoom_techniques.md`. Some approaches are partially RE'd; others are sketches.
- **Why:** Scope mods are popular and currently fragile. Better RE = more robust mods.
- **How to start:** Read the existing knowledge-base doc, then trace one approach end-to-end in IDA.

### First-person/third-person bone rig differences

- **Status:** OPEN
- **What:** Document how VR's first-person body rig differs from third-person. F4VR has both rigs active simultaneously (HMD camera attached to first-person, but third-person body visible to others/mirrors).
- **Why:** Body mods, holster mods, and physics mods all need to know which rig owns which bones.
- **How to start:** Already partially documented in `Modding-Reference/F4VR/Analysis/gold/FRIK_RE_REFERENCE.md`. Extend it.

---

## Low value / curiosity

### What's the 8 mystery bytes at MiddleHighProcessData+0x268?

- **Status:** OPEN (likely BLOCKED — see below)
- **What:** The 8-byte VR-only field at MiddleHighProcessData+0x268 (added in commit `f5bddb9`).
- **Why:** Curiosity. Currently declared as opaque padding which is layout-correct.
- **Status note:** Investigation in [middlehigh-vr-shift-investigation.md](middlehigh-vr-shift-investigation.md) found the field appears to be **functionally dead in the shipping VR build** — initialized to 0 in the constructor, never read or written by any code we could find via Hex-Rays decompile search. Could be vestigial or accessed via assembly the decompiler hides.
- **Why low value:** If nothing reads it, mods don't need it.

### PlayerCamera state-name disagreement (slots 7 & 8)

- **Status:** OPEN (documented, no code change needed)
- **What:** f4sevr names PlayerCamera state slot 7 as `kCameraState_ThirdPerson1` and slot 8 as `kCameraState_ThirdPerson2`. CommonLibF4VR names them `kAnimated` and `k3rdPerson`. Same vtable slots, different semantic interpretation.
- **Why low value:** The slot indices match either way. Anyone iterating cameraStates[] uses the index, not the name. Worth verifying which interpretation matches the actual game logic but not urgent.
- **How to start:** Trace one of the slot-7/slot-8 pointers and see what the camera state class's `OnEnter`/`Update` methods actually do.

---

## Workflow notes

- **One investigation per doc.** When you start working on something here, create `Analysis/<topic>-investigation.md` and put the running notes there. Keep this TODO doc as a one-line summary per item.
- **Build-verify everything.** Same `cmake --build build --config Debug` rule as the f4sevr-port branch. If the build doesn't pass, the change isn't real.
- **Use the IDA recipe in [middlehigh-vr-shift-investigation.md](middlehigh-vr-shift-investigation.md)** as the template for any field-offset-disagreement question. It worked twice (MiddleHighProcessData and PlayerCharacter); it'll work for similar problems.

## Adding new entries

When you spot something you want RE'd later, add it under the appropriate priority section with:

```markdown
### One-line title

- **Status:** OPEN
- **What:** what's the question / feature
- **Why:** what does it unlock
- **How to start:** concrete first step
```

Resist the urge to flesh out the investigation up-front. The point of this doc is a queue, not a knowledge base.
