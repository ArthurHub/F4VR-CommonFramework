# f4sevr → CommonLibF4VR RE Port — Progress & TODO

**Status: Phases 1, 2, and 3 done — 17 commits on `f4sevr-port` branch in `external/CommonLibF4VR`.**

**Goal:** For each class in `src/f4sevr/Forms.h`, compare to the CommonLibF4VR equivalent on three axes — **(1) layout** (size + offset), **(2) field types** at each offset, **(3) methods/enums/inner-types** declared on the class. Where f4sevr has something CommonLibF4VR is missing, port it. Where they disagree, prefer f4sevr per user direction. VR > flat.

---

## What was actually shipped

**Branch:** `f4sevr-port` in `external/CommonLibF4VR` (local-only, not pushed). Based on `master @ f29ed93`. **17 commits**, all build cleanly with `cmake --build build --config Debug`:

### Phase 1: Layout verification (5 commits)

| # | Hash | Subject |
|---|------|---------|
| 1 | `a0ff76b` | re: TESForm hierarchy — lock down field offsets via f4sevr asserts |
| 2 | `5a0e91c` | re: Actor — lock down race/biped field offsets |
| 3 | `e36a42a` | re: PlayerCamera — lock down cameraStates/cameraBodyID offsets |
| 4 | `6c90869` | re: TESObjectARMO — lock down armor field offsets |
| 5 | `760b8b5` | **re: PlayerCharacter — fix VR layout (insert 0x470 byte VR padding)** |

### Phase 2: Gap-fill (9 commits)

| # | Hash | Type | Subject |
|---|------|------|---------|
| 6 | `075598c` | Field + 4 methods | re: BGSBipedObjectForm — fill missing weightClass field & slot mutators |
| 7 | `93d0679` | Enum + helper | re: Actor — name kFlag_Teammate (bit 26 of niFlags) + IsPlayerTeammate |
| 8 | `116c27b` | Enum constants | re: TESForm — name the FormFlags bit constants |
| 9 | `a01815b` | Enum constant | re: LOADED_REF_DATA — name kFlag_PhysicsInitialized |
| 10 | `2b53780` | Method | re: PlayerCamera — port GetCameraStateId from f4sevr SDK |
| 11 | `ea0e463` | Method | re: Actor — port GetEquippedExtraData from f4sevr SDK |
| 12 | `b590c49` | Method | re: TESObjectARMA — port GetNodeName from f4sevr SDK |
| 13 | `4ad05e8` | Method | re: TESObjectREFR — port CreateRefHandle from f4sevr SDK |
| 14 | `1488ace` | Field-type | re: BIPOBJECT — refine `part` field type to BGSModelMaterialSwap* |

### Phase 3: Vtable + name-alias resolution + second VR layout fix (3 commits)

| # | Hash | Type | Subject |
|---|------|------|---------|
| 15 | `3be2d23` | Vtable rename | re: TESObjectREFR — rename vtable slot A0 to GetEquipData per f4sevr |
| 16 | `8b7f949` | Inline aliases | re: TESObjectREFR — add f4sevr-name aliases for renamed virtuals |
| 17 | `f5bddb9` | VR layout | re: MiddleHighProcessData — fix VR layout (insert 8-byte VR padding at 0x268) |

---

## Phase 3 findings (resolved)

### vtable slot 0xA0 — RESOLVED in commit 15

f4sevr SDK said slot 0xA0 is `ActorEquipData** GetEquipData(bool bFirstPerson)`. CommonLibF4VR had it as `void CollectPickNodes()`. Per user direction (f4sevr authoritative for VR), commit 15 renamed the virtual to `GetEquipData(bool) → BSTSmartPointer<BipedAnim>*`. CollectPickNodes had zero callers in CommonLibF4VR so the rename was non-breaking. The runtime vtable is unchanged (it comes from the EXE); only the source-level name and signature changed.

### vtable name aliases at 7B/8B/8C/91 — RESOLVED in commit 16

Same vtable slot, semantically equivalent function, different name in each interpretation. Commit 16 added inline non-virtual free-function aliases that forward to the existing virtuals so callers using f4sevr's names compile and dispatch correctly:

| Slot | f4sevr name | CommonLibF4VR name | Alias added |
|------|-------------|---------------------|-------------|
| 0x7B | `GetMarkerPosition(NiPoint3*)` | `GetLookingAtLocation() → NiPoint3` | `GetMarkerPosition(REFR&, NiPoint3*)` |
| 0x8B | `GetActorRootNode(bool)` | `Get3D(bool)` | `GetActorRootNode(REFR&, bool) → NiNode*` |
| 0x8C | `GetObjectRootNode()` | `Get3D()` | `GetObjectRootNode(REFR&) → NiNode*` |
| 0x91 | `GetActorRace()` | `GetVisualsRace()` | `GetActorRace(REFR&) → TESRace*` |

NiNode*-returning aliases use reinterpret_cast on Get3D's NiAVObject* return; NiNode is single-inheritance from NiAVObject so the bit-cast is sound.

### Inner-struct deep audit — Actor::MiddleProcess and friends

f4sevr's `Actor::MiddleProcess::Data08` has internal inconsistencies — its own field comments contradict its own static_asserts:
- comment says `lock @ 0x280` but unk00 array is sized 0x288 → lock actually at 0x288
- comment says `equipData @ 0x288` but the static_assert says 0x290

The original f4sevr SDK (`f4sevr_0_6_21/src/f4sevr/f4se/GameReferences.h:472`) asserts `equipData @ 0x288`, while the bundled-with-this-project f4sevr/Forms.h asserts `equipData @ 0x290` — an 8-byte shift somewhere. Probably a VR-specific addition that the bundled version made room for.

CommonLibF4VR's `MiddleHighProcessData` has `equippedItemsLock @ 0x280` and `equippedItems @ 0x288` (matching the original SDK, NOT the bundled VR-aware version). If the bundled f4sevr is right about VR, CommonLib's MiddleHighProcessData layout would be off by 8 bytes for VR.

**Decision: NOT changed.** The bundled f4sevr is internally inconsistent (comments vs asserts), and disassembling the actual VR EXE is needed to know ground truth. CommonLibF4VR's layout has zero callers of `equippedItems` in the codebase, so even if it's wrong, nothing breaks today. Documented here for future investigation.

### Other inner-struct comparisons

| Inner type | f4sevr layout | CommonLib layout | Verdict |
|---|---|---|---|
| `TESForm::Data` (sourceFiles target) | `{u64 entries, u64 size}` 16B | `TESFileContainer = {TESFileArray*}` 8B → array of `{entries, size}` | Bit-equivalent (both 1 level of indirection to {entries, size}); no change |
| `TESObjectREFR::LoadedData` | 5 unk u64 fields with `flags @ 0x20` | Fully named: handleList/data3D/currentWaterType/relevantWaterHeight/cachedRadius/flags/underwaterCount | CommonLib strictly more refined; only added `kFlag_PhysicsInitialized` constant in commit 9 |
| `Actor::ActorValueData` (8B per element) at 0x338 | `BSTArray<ActorValueData>` (24B) at 0x338 | `ActorValueStorage avStorage` (56B) at 0x338 | Different abstractions; CommonLib's ActorValueStorage may encompass both 0x338 and 0x350 BSTArrays. No change without disassembly |
| `Actor::Data350` (16B per element) at 0x350 | `BSTArray<Data350>` at 0x350 | (subsumed in avStorage) | Same as above |
| `PlayerCharacter::Objective`/`ObjectiveData` | small inner structs | `BGSInstancedQuestObjective` (10B, used in `objectives @ 0x7D9`) | Different type, similar purpose |
| `TESObjectARMO::InstanceData` | 6 unk fields + named (keywords/damageTypes/weight/value/health/armorRating) | Full naming (enchantments/materialSwaps/blockBashData/keywords/damageTypes/actorValues/weight/colorRemappingIndex/value/health/staggerRating/rating/index) | CommonLib strictly more refined; phase-1 commit 4 added offset asserts |
| `TESObjectARMO::ArmorAddons` | `{void* unk00, TESObjectARMA* armorAddon}` 16B | `ArmorAddon = {u16 index, TESObjectARMA* armorAddon}` 16B | Equivalent (CommonLib refines unk00 → u16 index + padding) |

No additional commits needed for the inner-struct audit. Phase 3 is complete.

### Small helpers audit (no gaps found)

- **`BGSTypedKeywordValueArray`** — CommonLib has it as a template `BGSTypedKeywordValueArray<KeywordType TYPE>` with `{T* array, u32 size}` (16B). Original SDK's `{void* unk00, void* unk08}` was the placeholder; CommonLib's named refinement is strictly better. No gap.
- **`DamageTypes`** struct — used inside `TESObjectARMO::InstanceData::damageTypes`. CommonLib uses `BSTTuple<TESForm*, BGSTypedFormValuePair::SharedVal>` (16B) for the same field; same memory layout, more typed. No gap.
- **`ValueModifier`** struct — declared in f4sevr Forms.h:19 but NEVER USED. Not a real gap.
- **`IAliasFunctor`** interface — declared in f4sevr Forms.h:25 but only referenced by a commented-out `DEFINE_MEMBER_FN_1(ForEachAlias, ...)`. Not a real gap.

### PlayerCamera state enum — naming disagreement at indices 7 & 8

| Index | f4sevr name | CommonLibF4VR name |
|-------|-------------|---------------------|
| 0 | kCameraState_FirstPerson | kFirstPerson |
| 1 | kCameraState_AutoVanity | kAutoVanity |
| 2 | kCameraState_VATS | kVATS |
| 3 | kCameraState_Free | kFree |
| 4 | kCameraState_IronSights | kIronSights |
| 5 | kCameraState_Transition | kPCTransition |
| 6 | kCameraState_TweenMenu | kTween |
| **7** | **kCameraState_ThirdPerson1** | **kAnimated** |
| **8** | **kCameraState_ThirdPerson2** | **k3rdPerson** |
| 9 | kCameraState_Furniture | kFurniture |
| 10 | kCameraState_Horse | kMount |
| 11 | kCameraState_Bleedout | kBleedout |
| 12 | kCameraState_Dialogue | kDialogue |

CommonLib's "Animated" (index 7) and "k3rdPerson" (index 8) are likely more accurate — Animated = scripted/cinematic third-person, k3rdPerson = player-controlled orbit. f4sevr's "ThirdPerson1/2" is the original f4se SDK guessing they were variants.

**No code change recommended** — CommonLib's naming is more meaningful. Documented here for reviewers who use either naming style.

### PlayerCharacter VR padding contents (0xB70-0xFE0)

The 0x470-byte VR-only block in PlayerCharacter (commit 5) is currently opaque (`std::byte vrPlayerStatePadding[0x470]`). Future work: RE the contents. Likely fields based on what FRIK and other VR mods access:
- HMD pose tracking
- Controller hand bone references (left/right wand nodes)
- IK rig data
- VR-specific render targets / scope state

Cross-reference candidates: `Modding-Reference/F4VR/Analysis/gold/FRIK_RE_REFERENCE.md`, `F4VR-CommonFramework_RE_REFERENCE.md`, and individual VR-aware mod analysis docs.

### Inner-struct: Actor::MiddleProcess vs CommonLib::MiddleHighProcessData — RESOLVED in commit 17

f4sevr's bundled `Actor::MiddleProcess::Data08` had its own field-offset comments contradicting its own static_asserts, and disagreed with both the original SDK and CommonLib about whether equipData was at 0x288 or 0x290. Resolved via IDA disassembly of `Fallout4VR.exe`: the actual VR layout has an 8-byte VR-only field inserted at offset 0x268 (between `animationVariableCache` and `subGraphIdleManagerRoots`), which shifts everything downstream by +8 in VR. CommonLib's flat layout was right; the VR-shifted offsets needed `#ifdef ENABLE_FALLOUT_VR` padding.

Full investigation writeup: [middlehigh-vr-shift-investigation.md](middlehigh-vr-shift-investigation.md).

---

## Other open follow-ups

- [ ] Push branch to `origin/f4sevr-port` when ready for review.
- [ ] Bump submodule pointer in F4VR-CommonFramework parent repo after submodule branch is pushed.
- [ ] Open PR (if external review wanted) or just merge to `master` of the fork (if self-managed).

---

## Future work — porting the rest of f4sevr (`src/f4sevr/` headers beyond Forms.h)

The current 17 commits cover **Forms.h** only — that was the scoping decision when this work started (Forms.h has the high-value RE: TESForm hierarchy, Actor, PlayerCharacter, Camera). The other f4sevr headers in `src/f4sevr/` are not covered. If you want full f4sevr → CommonLibF4VR coverage in the future, here's the prioritized list and approach.

### Priority A — Common.h (~840 lines of infrastructure)

**Headers:** `src/f4sevr/Common.h`

**Contents:** `Heap`, `NiPointer`, `tArray`, `tHashSet`, `StringCache`, `BSReadWriteLock`. CommonLibF4VR has full equivalents for all of these, widely used.

**Estimated work:** 1–2 hours of audit, likely 0–2 commits.

**Approach:** For each class, find the CommonLib equivalent (likely under different names, e.g., `tArray` → `BSTArray`, `tHashSet` → `BSTHashMap`, `NiPointer` is in both, `BSReadWriteLock` is in both). Confirm size matches. Only port if there's a genuine gap. Most likely outcome: zero changes needed — CommonLib's versions are strictly more developed.

**Why low priority:** Existing F4VR-CommonFramework code that uses `F4SEVR::Heap` etc. already works fine; CommonLib hasn't *broken* the f4sevr versions.

### Priority B — Papyrus interop headers (~7 files, ~1500 lines)

**Headers:**
- `PapyrusArgs.h/.cpp`
- `PapyrusInterfaces.h`
- `PapyrusNativeFunctionDef.inl`
- `PapyrusNativeFunctionDef_Base.inl`
- `PapyrusNativeFunctions.h`
- `PapyrusStruct.h/.cpp`
- `PapyrusUtils.h`
- `PapyrusVM.h`
- `PapyrusValue.h/.cpp`

**Contents:** Papyrus VM bindings — the interop layer mods use to register native functions callable from Papyrus scripts.

**Estimated work:** Likely substantial — Papyrus is its own subsystem. CommonLibF4VR has `RE/Bethesda/BSScript/...` which is the equivalent namespace but with very different abstractions (e.g., CommonLib uses `BSScript::IFunction`, `BSScript::Variable`, etc.). The two interpretations may not be byte-equivalent.

**Approach:**
1. First step: figure out if any code in the framework or downstream mods *uses* the f4sevr Papyrus types. Grep `F4SEVR::Papyrus*`. If the answer is "only the framework's own helpers", there's nothing to port — just keep using f4sevr's Papyrus headers as-is.
2. If downstream code uses these: case-by-case audit. Some Papyrus types (like `VMValue`) are well-RE'd on the CommonLib side; others may not be.

**Why this is the trickiest:** Papyrus interop has subtle ABI requirements (function-pointer signatures must exactly match what the VM expects). Getting it wrong = silent runtime crashes inside Papyrus calls. Don't port casually.

### Recipe for any f4sevr → CommonLibF4VR port (reusable)

Same workflow as the 17 commits we already shipped:

1. **Pick one class/struct.** Start with the smallest, most-used types first.
2. **Find the CommonLib equivalent** (likely under a different name; grep CommonLibF4VR headers for the f4sevr field names or signatures).
3. **Compare on three axes** — (1) layout (size + offset), (2) field types, (3) methods/enums/inner-types.
4. **Resolve disagreements** — if both interpretations agree on layout, prefer CommonLib's name. If they disagree on layout, prefer f4sevr (per project policy). If you can't tell, use IDA — see `middlehigh-vr-shift-investigation.md` for the recipe.
5. **Build-verify** with `cmake --build build --config Debug`. Use `static_assert(sizeof(...))` and `static_assert(offsetof(...))` as a compile-time firewall.
6. **One commit per class/cluster.** Use the `re: <Class> — <description>` commit-message style.
7. **Update the relevant section of this doc** to log what was done.

### What's outside f4sevr's scope entirely

f4sevr `src/f4sevr/` doesn't cover most of the game (it's a small subset of all the game's RE). Things like camera hooks, scaleform menus, havok physics, BSGraphics, audio, save/load — none of this is in f4sevr. CommonLibF4VR has all of it (much better RE'd than f4sevr). For *those* gaps, see `general-re-todo.md` instead.

---

## Methodology used

For every gap-fill commit:
1. Read the f4sevr declaration + comment block.
2. Read the original SDK implementation if it's a function-call method (`Modding-Reference/F4VR/manual-repos/gold/f4sevr_0_6_21/src/f4sevr/f4se/*.cpp`) — record any RVAs.
3. Search CommonLibF4VR for an equivalent method/field/enum (often with a different name).
4. If equivalent exists: document the alias in a comment, no code change.
5. If no equivalent: add to CommonLibF4VR. Inline accessors and named enums are zero-risk; function-call wrappers either go through existing CommonLib helpers (preferred) or use new RVAs.
6. Build (`cmake --build build --config Debug`) — must succeed. Static_asserts in commits 1-5 catch byte-level layout mismatches.
7. Commit with `re: <Class> — <gap description>`.

## Verification policy

Every commit was verified by `cmake --build build --config Debug` of F4VR-CommonFramework. The project sets `ENABLE_FALLOUT_VR=ON`, so VR-conditional code (the PlayerCharacter padding from commit 5) gets exercised. The static_asserts added throughout commits 1-9 form a compile-time layout firewall — any future struct edits that shift these field offsets will break the build.

## Disagreement policy applied

- f4sevr won on PlayerCharacter VR layout (commit 5) — material divergence.
- f4sevr won on field-type refinements (commits 6, 14) — added information.
- Where naming differs but layout/semantics agree, kept CommonLib's name as canonical and documented f4sevr's alias in a comment (commits 2, 7, 11).
- Where vtable slots agree but interpretations differ in unsafe ways (slot 0xA0), DEFERRED for investigation — see "High-risk" section above.

## Quick-resume snapshot

If session ends:
1. `cd external/CommonLibF4VR && git status && git log --oneline master..HEAD` — confirm 14 commits on `f4sevr-port`.
2. To continue, pick from "Phase 3" / "Other open follow-ups" sections above.
3. To review or merge: `git diff master..f4sevr-port -- '*.h' '*.cpp'` — full diff of changes.
4. The parent repo (`c:/Stuff/GitHub/Mine/F4VR-CommonFramework`) has uncommitted changes: this checklist + `.claude/settings.json` (permissions) + the submodule pointer bump. Decide what to commit when ready.
