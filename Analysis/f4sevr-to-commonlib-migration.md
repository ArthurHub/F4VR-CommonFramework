# Migrating from `F4SEVR::*` (Forms.h) to `RE::*` (CommonLibF4VR)

The framework's `src/f4sevr/Forms.h` was a vendored copy of types from the f4sevr SDK. After auditing each class against CommonLibF4VR (see [f4sevr-port-checklist.md](f4sevr-port-checklist.md)), all of `Forms.h`'s types now have CommonLibF4VR equivalents that are layout-correct for VR. The framework no longer ships `Forms.h`, and downstream mods must move off `F4SEVR::` types from that header.

This guide is the porting recipe.

> **In scope:** every type and field declared in `src/f4sevr/Forms.h`.
>
> **Not in scope:** `F4SEVR::` symbols from `src/f4sevr/Common.h` (e.g. `F4SEVR::StringCache`, `F4SEVR::Heap`, `F4SEVR::tArray`) and the Papyrus headers (`F4SEVR::NativeFunction*`, `F4SEVR::VMArray`, `F4SEVR::VMVariable`, `F4SEVR::execPapyrusGlobalFunction`, `F4SEVR::getGameVM`, `F4SEVR::StaticFunctionTag`). Those headers still ship as before — only `Forms.h` was removed.

---

## Type rename table

Replace every occurrence (case-sensitive) on the left with the form on the right. For most types it is a pure rename; subtleties are flagged in the **Notes** column and detailed below.

| `F4SEVR::` (old)                       | `RE::` (new)                                  | Notes                                  |
|----------------------------------------|-----------------------------------------------|----------------------------------------|
| `F4SEVR::TESForm`                      | `RE::TESForm`                                 | direct rename                          |
| `F4SEVR::TESObject`                    | `RE::TESObject`                               | direct rename                          |
| `F4SEVR::TESBoundObject`               | `RE::TESBoundObject`                          | direct rename                          |
| `F4SEVR::TESObjectREFR`                | `RE::TESObjectREFR`                           | see "Field renames" below              |
| `F4SEVR::TESObjectREFR::LoadedData`    | `RE::LOADED_REF_DATA`                         | not a nested type; `rootNode`→`data3D` |
| `F4SEVR::Actor`                        | `RE::Actor`                                   | see "Field renames" below              |
| `F4SEVR::Actor::ActorFlags`            | `RE::ActorFlagBits` (free enum)               | bit constants are now free enum        |
| `F4SEVR::Actor::MiddleProcess`         | `RE::AIProcess`                               | see "Equipment access" below           |
| `F4SEVR::Actor::MiddleProcess::Data08` | `RE::MiddleHighProcessData`                   | see "Equipment access" below           |
| `F4SEVR::Actor::MiddleProcess::Data08::EquipData` | `RE::EquippedItem`                 | layout differs — see below             |
| `F4SEVR::Actor::ActorValueData`        | (subsumed in `RE::ActorValueStorage`)         | no direct rename; rework via API       |
| `F4SEVR::PlayerCharacter`              | `RE::PlayerCharacter`                         | see "Field renames" below              |
| `F4SEVR::PlayerCamera`                 | `RE::PlayerCamera`                            | direct rename, `cameraNode`→`cameraRoot` |
| `F4SEVR::TESCamera`                    | `RE::TESCamera`                               | `cameraNode`→`cameraRoot` (NiPointer)  |
| `F4SEVR::TESCameraState`               | `RE::TESCameraState`                          | direct rename                          |
| `F4SEVR::ThirdPersonState`             | `RE::ThirdPersonState`                        | direct rename                          |
| `F4SEVR::ActorState`                   | `RE::ActorState`                              | `IsWeaponDrawn()`→`GetWeaponMagicDrawn()` |
| `F4SEVR::EquippedItemData`             | `RE::EquippedItemData`                        | direct rename, more refined            |
| `F4SEVR::EquippedWeaponData`           | `RE::EquippedWeaponData`                      | layout fully named — see below         |
| `F4SEVR::ActorEquipData`               | `RE::BipedAnim`                               | layout fully named — see below         |
| `F4SEVR::ActorEquipData::SlotData`     | `RE::BIPOBJECT`                               | one-to-one element of the slot array   |
| `F4SEVR::TESObjectARMO`                | `RE::TESObjectARMO`                           | components are *base classes*, not members |
| `F4SEVR::TESObjectARMO::InstanceData`  | `RE::TESObjectARMO::InstanceData`             | fully named in CommonLib               |
| `F4SEVR::TESObjectARMO::ArmorAddons`   | `RE::TESObjectARMO::ArmorAddon`               | direct rename                          |
| `F4SEVR::TESObjectARMA`                | `RE::TESObjectARMA`                           | direct rename                          |
| `F4SEVR::BGSTextureSet`                | `RE::BGSTextureSet`                           | direct rename                          |
| `F4SEVR::BGSListForm`                  | `RE::BGSListForm`                             | direct rename                          |
| `F4SEVR::BSTextureSet`                 | `RE::BSTextureSet`                            | direct rename                          |
| `F4SEVR::BGSEquipType`                 | `RE::BGSEquipType`                            | direct rename                          |
| `F4SEVR::BGSBipedObjectForm`           | `RE::BGSBipedObjectForm`                      | direct rename                          |
| `F4SEVR::BGSBlockBashData`             | `RE::BGSBlockBashData`                        | direct rename                          |
| `F4SEVR::BGSDestructibleObjectForm`    | `RE::BGSDestructibleObjectForm`               | direct rename                          |
| `F4SEVR::BGSPickupPutdownSounds`       | `RE::BGSPickupPutdownSounds`                  | direct rename                          |
| `F4SEVR::BGSAttachParentArray`         | `RE::BGSAttachParentArray`                    | direct rename                          |
| `F4SEVR::BGSInstanceNamingRulesForm`   | `RE::BGSInstanceNamingRulesForm`              | direct rename                          |
| `F4SEVR::BGSTypedKeywordValueArray`    | `RE::BGSTypedKeywordValueArray<TYPE>`         | now a template                         |
| `F4SEVR::TESEnchantableForm`           | `RE::TESEnchantableForm`                      | direct rename                          |
| `F4SEVR::TESFullName`                  | `RE::TESFullName`                             | direct rename                          |
| `F4SEVR::TESRaceForm`                  | `RE::TESRaceForm`                             | direct rename                          |
| `F4SEVR::TESBipedModelForm`            | `RE::TESBipedModelForm`                       | direct rename                          |
| `F4SEVR::BaseFormComponent`            | `RE::BaseFormComponent`                       | direct rename                          |
| `F4SEVR::BSInputEventReceiver`         | `RE::BSInputEventReceiver`                    | direct rename                          |
| `F4SEVR::IMovementInterface`           | `RE::IMovementInterface`                      | direct rename                          |
| `F4SEVR::IMovementState`               | `RE::IMovementState`                          | direct rename                          |
| `F4SEVR::MagicTarget`                  | `RE::MagicTarget`                             | direct rename                          |
| `F4SEVR::DamageTypes`                  | `RE::BSTTuple<TESForm*, BGSTypedFormValuePair::SharedVal>` | same memory layout |
| `F4SEVR::ValueModifier`                | (no equivalent — was unused in f4sevr)        | delete dead code                       |
| `F4SEVR::IAliasFunctor`                | (no equivalent — was unused in f4sevr)        | delete dead code                       |

---

## Field renames inside types

### `RE::TESObjectREFR`

| Old (`F4SEVR::TESObjectREFR`)           | New (`RE::TESObjectREFR`)                       |
|-----------------------------------------|-------------------------------------------------|
| `parentCell`                            | `parentCell` (same)                             |
| `unk80…unkB0` opaque region             | `data` (`OBJ_REFR` struct with named position/rotation) |
| `unkF0` (`LoadedData*`)                 | `loadedData` (`LOADED_REF_DATA*`)               |
| `unkF0->rootNode` (`NiNode*`)           | `loadedData->data3D` (`NiPointer<NiAVObject>`) — call `.get()->IsNode()` |
| `inventoryList`                         | `inventoryList` (same)                          |
| `extraDataList`                         | `extraList` (`BSTSmartPointer<ExtraDataList>`)  |
| `LoadedData::kFlag_PhysicsInitialized`  | `LOADED_REF_DATA::kFlag_PhysicsInitialized`     |
| virtual `GetMarkerPosition(NiPoint3*)` (slot 0x7B) | `GetLookingAtLocation()` returns `NiPoint3` — or use the `RE::GetMarkerPosition(refr, out)` free-function alias |
| virtual `GetActorRootNode(bool)` (0x8B) | `Get3D(bool)` returns `NiAVObject*` — or `RE::GetActorRootNode(refr, firstPerson)` returns `NiNode*` |
| virtual `GetObjectRootNode()` (0x8C)    | `Get3D()` — or `RE::GetObjectRootNode(refr)`    |
| virtual `GetActorRace()` (0x91)         | `GetVisualsRace()` — or `RE::GetActorRace(refr)` |
| virtual `GetEquipData(bool)` (0xA0)     | `GetEquipData(bool)` returns `BSTSmartPointer<BipedAnim>*` |
| `CreateRefHandle()` member              | inline `RE::CreateRefHandle(refr)` free helper  |

### `RE::Actor`

| Old (`F4SEVR::Actor`)                  | New (`RE::Actor`)                              |
|----------------------------------------|------------------------------------------------|
| `actorFlags` (uint32 at 0x2D0)         | `niFlags.flags` (`NiTFlags<uint32, Actor>`)    |
| `Actor::ActorFlags::kFlag_Teammate`    | `RE::kActorFlag_Teammate` (free enum constant) |
| `IsPlayerTeammate()` member            | `RE::IsPlayerTeammate(actor)` free function    |
| `GetEquippedExtraData(slot, out)` member | `RE::GetEquippedExtraData(actor, slot, out)` free function |
| `actorState.IsWeaponDrawn()`           | `GetWeaponMagicDrawn()` (Actor inherits ActorState) |
| `actorState` (member of type `ActorState`) | `Actor` inherits `ActorState` directly — access fields via `actor->weaponState` etc. |
| `middleProcess` (`MiddleProcess*` at 0x300) | `currentProcess` (`AIProcess*` at 0x300)  |
| `middleProcess->unk08`                 | `currentProcess->middleHigh` (`MiddleHighProcessData*`) |
| `equipData` (`ActorEquipData*` at 0x428) | `biped` (`BSTSmartPointer<BipedAnim>` at 0x428) |
| `race`                                 | `race` (same)                                  |
| `unk350` (`BSTArray<Data350>`)         | now folded into `avStorage` (`ActorValueStorage`) |
| `actorValueData`                       | now folded into `avStorage`                    |

### `RE::PlayerCharacter`

| Old (`F4SEVR::PlayerCharacter`)         | New (`RE::PlayerCharacter`)                              |
|-----------------------------------------|----------------------------------------------------------|
| `firstPersonSkeleton` (`NiNode*`)       | `firstPerson3D` (`NiPointer<NiNode>`) — call `.get()`    |
| `playerEquipData` (`ActorEquipData*`)   | `firstPersonBipedAnim` (`BSTSmartPointer<BipedAnim>`)    |
| `tints`                                 | `tintingData` (`BGSCharacterTint::Entries*`)             |
| `objData` (`BSTArray<ObjectiveData>`)   | `objectives` (`BSTArray<BGSInstancedQuestObjective>`)    |

### `RE::PlayerCamera` / `RE::TESCamera`

| Old (`F4SEVR::TESCamera` / `PlayerCamera`)        | New (`RE::TESCamera` / `RE::PlayerCamera`)             |
|---------------------------------------------------|--------------------------------------------------------|
| `cameraNode` (`NiNode*`)                          | `cameraRoot` (`NiPointer<NiNode>`)                     |
| `cameraState` (`TESCameraState*`)                 | `currentState` (`BSTSmartPointer<TESCameraState>`)     |
| `cameraStates[N]` (raw `TESCameraState*` array)   | `cameraStates[N]` (`BSTSmartPointer<TESCameraState>`)  |
| `kCameraState_FirstPerson`, etc.                  | `RE::CameraState::kFirstPerson`, etc.                  |
| `kCameraState_ThirdPerson1` / `…_ThirdPerson2`    | `kAnimated` / `k3rdPerson` (CommonLib uses semantic names) |
| `GetCameraStateId(state)` member                  | `GetCameraStateId(state)` member (also in CommonLib)   |

#### Camera singleton

```cpp
// before — raw offset
static REL::Relocation<F4SEVR::PlayerCamera**> g_playerCamera(REL::Offset(0x5930608));
F4SEVR::PlayerCamera* cam = *g_playerCamera.get();

// after — use the typed singleton
RE::PlayerCamera* cam = RE::PlayerCamera::GetSingleton();
```

### `RE::TESObjectARMO`

In F4SEVR, form components were declared as **members** (`fullName`, `keywordForm`, `bipedObject`, etc.). In CommonLibF4VR they are **base classes** of `TESObjectARMO` (multiple inheritance), so the member-access dot is gone:

```cpp
// before
armor->keywordForm.numKeywords
armor->keywordForm.keywords[i]
armor->bipedObject.data.parts
armor->fullName.name

// after
armor->numKeywords
armor->keywords[i]
armor->bipedObjects.parts            // BGSBipedObjectForm now exposes Data inline as bipedObjects
armor->fullName                      // BGSLocalizedString — call .c_str() or .data()
```

For one-shot keyword lookups, prefer the API:

```cpp
armor->HasKeywordID(formID);
armor->ForEachKeyword([&](RE::BGSKeyword* kw) { ... return RE::BSContainer::ForEachResult::kContinue; });
```

### `RE::EquippedWeaponData`

f4sevr's `EquippedWeaponData` had everything past `vtable` typed as `unkXX`. CommonLib names every field:

```cpp
// before
ewd->ammo;        // 10
ewd->unk18;       // 18
ewd->unk20;       // 20
ewd->unk28;       // 28
ewd->object;      // 30  (NiAVObject*)

// after
ewd->ammo;          // 10
ewd->ammoCount;     // 18
ewd->aimModel;      // 20  (AimModel*)
ewd->muzzleFlash;   // 28  (MuzzleFlash*)
ewd->fireNode;      // 30  (NiAVObject*)
// ...plus attackState, fireLocations, sound handles, etc.
```

---

## Equipment access — the biggest behavioral change

f4sevr's "current equipped item" lived behind a single pointer:

```cpp
// f4sevr — single EquipData per actor
F4SEVR::Actor::MiddleProcess::Data08::EquipData* equipData = player->middleProcess->unk08->equipData;
TESForm*               item            = equipData->item;          // 00
TBO_InstanceData*      instance        = equipData->instanceData;  // 08
BGSEquipSlot*          slot            = equipData->equipSlot;     // 10
F4SEVR::EquippedWeaponData* weaponData = equipData->equippedData;  // 20
```

CommonLib has the correct shape of the underlying memory: the field at this offset is a **`BSTArray<EquippedItem>`**, not a single struct. f4sevr's "equipData" was effectively reading element [0] of that array (a BSTArray's data pointer is its first member, which is why the f4sevr layout happened to work):

```cpp
// CommonLib — array
RE::AIProcess*               process    = actor->currentProcess;
RE::MiddleHighProcessData*   middleHigh = process ? process->middleHigh : nullptr;
if (!middleHigh || middleHigh->equippedItems.empty()) { /* none equipped */ }

const RE::EquippedItem& slot0 = middleHigh->equippedItems[0];
RE::TESForm*                  item     = slot0.item.object;        // BGSObjectInstance::object
BSTSmartPointer<TBO_InstanceData> instance = slot0.item.instanceData;
const RE::BGSEquipSlot*       equipSlot = slot0.equipSlot;
RE::BGSEquipIndex             equipIdx  = slot0.equipIndex;
RE::EquippedItemData*         data      = slot0.data.get();        // base type — cast for weapon/armor specific data
```

### Behavior caveat

Reading `equippedItems[0]` reproduces the f4sevr "first slot" semantics, but **`equippedItems` is an array** — index 0 is not guaranteed to be the weapon. If your code wants the equipped weapon specifically, iterate and filter by `equipSlot` or by RTTI on `data`:

```cpp
for (auto& slot : middleHigh->equippedItems) {
    if (auto* weapon = skyrim_cast<RE::EquippedWeaponData*>(slot.data.get())) {
        // found the weapon entry
    }
}
```

For the body-slot inspection patterns (e.g. checking what's in armor slot 3 for power-armor detection), use `Actor::biped` instead — it's the canonical biped-anim object with its own per-slot array. f4sevr's `equipData->slots[N].item` maps to:

```cpp
// before
TESForm* form = player->equipData->slots[0x03].item;
// or:        player->equipData->slots[0x03].instanceData;
// or:        player->equipData->slots[0x03].extraData;

// after — slots[] became object[] of BIPOBJECTs holding a BGSObjectInstance
auto biped = player->biped.get();
if (!biped) return;

RE::TESForm*               form     = biped->object[0x03].parent.object;
BSTSmartPointer<TBO_InstanceData> inst = biped->object[0x03].parent.instanceData;
RE::BGSObjectInstanceExtra* extra    = biped->object[0x03].modExtra;
RE::TESObjectARMA*         arma     = biped->object[0x03].armorAddon;     // typed in CommonLib
RE::BGSModelMaterialSwap*  matSwap  = biped->object[0x03].part;
RE::BGSTextureSet*         tex      = biped->object[0x03].skinTexture;
NiPointer<RE::NiAVObject>  partClone = biped->object[0x03].partClone;
```

The slot indices are unchanged — they're still `BIPED_OBJECT` enum values 0…43.

---

## Calling `GetFullName()` on an arbitrary `TESForm*`

f4sevr declared `GetFullName()` as a virtual on `TESForm`. CommonLib only declares it on `TESFullName` (which most named forms inherit). For a `TESForm*` whose concrete type isn't known at compile time, use the static helper:

```cpp
// before
const char* name = someForm->GetFullName();

// after
std::string_view name = RE::TESFullName::GetFullName(*someForm);
```

If you know the concrete type inherits `TESFullName` (e.g. `TESObjectWEAP`, `TESObjectARMO`, `TESNPC`), you can also `static_cast` and call the virtual directly.

---

## Singleton replacements

| Old                                                                | New                                  |
|--------------------------------------------------------------------|--------------------------------------|
| `reinterpret_cast<F4SEVR::PlayerCharacter*>(RE::PlayerCharacter::GetSingleton())` | `RE::PlayerCharacter::GetSingleton()` |
| `*REL::Relocation<F4SEVR::PlayerCamera**>(REL::Offset(0x5930608))` | `RE::PlayerCamera::GetSingleton()`   |

---

## Headers and includes

If you used to include the framework's `f4sevr/Forms.h` directly, drop the include — the file is gone:

```cpp
// before
#include "f4sevr/Forms.h"

// after — nothing; CommonLibF4VR types are pulled in by your existing RE/* headers
```

CommonLibF4VR equivalents live under:

- `RE/Bethesda/Actor.h`              — `Actor`, `ActorState`, `AIProcess`, `MiddleHighProcessData`, `EquippedItem`, `EquippedItemData`, `EquippedWeaponData`, `IsPlayerTeammate`, `GetEquippedExtraData`
- `RE/Bethesda/PlayerCharacter.h`    — `PlayerCharacter`
- `RE/Bethesda/TESCamera.h`          — `TESCamera`, `PlayerCamera`, `TESCameraState`, `ThirdPersonState`, `CameraState`
- `RE/Bethesda/TESObjectREFRs.h`     — `TESObjectREFR`, `LOADED_REF_DATA`, `BGSObjectInstance`, `BipedAnim`, `BIPOBJECT`, `BIPED_OBJECT`, the `GetActorRootNode`/`GetObjectRootNode`/`GetActorRace`/`GetMarkerPosition`/`CreateRefHandle` aliases
- `RE/Bethesda/TESBoundObjects.h`    — `TESBoundObject`, `TESObjectARMO`, `TESObjectARMA`, `BGSTextureSet`
- `RE/Bethesda/TESForms.h`           — `TESForm`, `TESObject`, `BGSListForm`, etc.
- `RE/Bethesda/FormComponents.h`     — `BaseFormComponent`, `BGSKeywordForm`, `TESFullName`, `TESRaceForm`, `TESEnchantableForm`, `BGSEquipType`, `BGSBipedObjectForm`, `BGSDestructibleObjectForm`, `BGSPickupPutdownSounds`, `BGSBlockBashData`, `BGSAttachParentArray`, `BGSInstanceNamingRulesForm`, `TESBipedModelForm`

---

## Mechanical migration recipe

1. Open your mod and grep for `F4SEVR::` (case-sensitive).
2. For each match: if the type is in the rename table above, rename it. If the type is from `Common.h` or a Papyrus header, leave it alone — those headers still ship.
3. After renaming, `git grep '\->actorFlags\|\->middleProcess\|\->unk08\|\->equipData\|\->firstPersonSkeleton\|\->playerEquipData\|\->cameraNode\|\->keywordForm\.\|\->unkF0\|->rootNode'` to find member accesses that need the field-name updates.
4. Hand-fix any equipment walks per the "Equipment access" section.
5. Drop the `#include "f4sevr/Forms.h"` line if you have one.
6. Build — the static_asserts in CommonLibF4VR will catch any byte-level layout mistakes you make at compile time.

---

## Related docs

- [f4sevr-port-checklist.md](f4sevr-port-checklist.md) — the audit log of what was actually ported into CommonLibF4VR (17 commits on the `f4sevr-port` branch), and the open follow-ups.
- [middlehigh-vr-shift-investigation.md](middlehigh-vr-shift-investigation.md) — IDA writeup of the VR-only 8-byte shift in `MiddleHighProcessData`.
