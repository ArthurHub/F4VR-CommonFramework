#pragma once

// ReSharper disable CppClangTidyClangDiagnosticReservedIdentifier
// ReSharper disable CppClangTidyBugproneReservedIdentifier

#include "MiscStructs.h"

class BSAnimationManager;

namespace f4cf::f4vr
{
    template <class T>
    class StackPtr
    {
    public:
        StackPtr()
        {
            p = nullptr;
        }

        T p;
    };

    inline REL::Relocation testin(REL::Offset(0x5a3b888));

    using _IsInAir = bool (*)(RE::Actor* actor);
    inline REL::Relocation<_IsInAir> IsInAir(REL::Offset(0x00DC3230));

    using _GetSitState = int (*)(RE::BSScript::Internal::VirtualMachine* registry, std::uint64_t stackID, RE::Actor* actor);
    inline REL::Relocation<_GetSitState> GetSitState(REL::Offset(0x40e410));

    using _IsSneaking = bool (*)(RE::Actor* a_actor);
    inline REL::Relocation<_IsSneaking> IsSneaking(REL::Offset(0x24d20));

    typedef RE::TESObjectWEAP* (*_GetEquippedWeapon)(RE::BSScript::Internal::VirtualMachine* registry, std::uint64_t stackID, RE::Actor* actor, std::uint64_t aiEquipIndex);
    inline REL::Relocation<_GetEquippedWeapon> GetEquippedWeapon(REL::Offset(0x140d790));

    /**
     * The papyrus Actor.SetRestrained method that prevents the player from moving, rotating, and shooting.
     */
    using _SetActorRestrained = bool (*)(RE::Actor* a_actor, bool a_restrict);
    inline REL::Relocation<_SetActorRestrained> SetActorRestrained(REL::ID(316742));

    using _AIProcess_getAnimationManager = void (*)(uint64_t aiProcess, StackPtr<BSAnimationManager*>& manager);
    inline REL::Relocation<_AIProcess_getAnimationManager> AIProcess_getAnimationManager(REL::Offset(0xec5400));

    using _BSAnimationManager_setActiveGraph = void (*)(BSAnimationManager* manager, int graph);
    inline REL::Relocation<_BSAnimationManager_setActiveGraph> BSAnimationManager_setActiveGraph(REL::Offset(0x1690240));

    inline REL::Relocation<uint64_t> EquippedWeaponData_vfunc(REL::Offset(0x2d7fcf8));

    using _NiNode_UpdateWorldBound = void (*)(RE::NiNode* node);
    inline REL::Relocation<_NiNode_UpdateWorldBound> NiNode_UpdateWorldBound(REL::Offset(0x1c18ab0));

    using _AIProcess_Set2DUpdateFlags = void (*)(RE::AIProcess* proc, uint64_t flags);
    inline REL::Relocation<_AIProcess_Set2DUpdateFlags> AIProcess_Set3DUpdateFlags(REL::Offset(0xec8ce0));

    using _CombatUtilities_IsActorUsingMelee = bool (*)(RE::Actor* a_actor);
    inline REL::Relocation<_CombatUtilities_IsActorUsingMelee> CombatUtilities_IsActorUsingMelee(REL::Offset(0x1133bb0));

    using _CombatUtilities_IsActorUsingMagic = bool (*)(RE::Actor* a_actor);
    inline REL::Relocation<_CombatUtilities_IsActorUsingMagic> CombatUtilities_IsActorUsingMagic(REL::Offset(0x1133c30));

    // ProcessLists singleton — raw offset because the bundled ProcessLists::GetSingleton()'s RelocationID
    // (1569706) has no entry in the shipped VR address-library database and cannot resolve on VR.
    inline REL::Relocation<RE::ProcessLists**> g_processLists(REL::Offset(0x5930968));

    // ProcessLists::GetActorsWithinRangeOfPoint(NiPoint3&, float, BSScrapArray<NiPointer<Actor>>&) —
    // enumerates all loaded actors within a radius of a world point. Not exposed by the bundled headers.
    using _ProcessLists_GetActorsWithinRangeOfPoint = void (*)(RE::ProcessLists*, RE::NiPoint3&, float, RE::BSScrapArray<RE::NiPointer<RE::Actor>>&);
    inline REL::Relocation<_ProcessLists_GetActorsWithinRangeOfPoint> ProcessLists_GetActorsWithinRangeOfPoint(REL::Offset(0xf8e040));

    // NOT declared here on purpose: Actor::RequestDetectionLevels (AddressLib 1368522, VR 0x140dfb3a0), the
    // variant that splits detection into visual and sound channels. It returns through a hidden result pointer
    // (MSVC sret) whose type the symbol does not name — assuming the 4-byte DetectionLevels and handing it a
    // 16-byte buffer crashed the game by overrunning the caller's stack frame. Use the bundled arg-simple
    // Actor::RequestDetectionLevel (f4vr::getDetectionLevel) unless the real return type is established first.

    // NOT declared here on purpose: AIProcess::SetDetectionModifier(float) (VR 0x140e910d0), which writes
    // HighProcessData::detectionModifier (+0x488) and arms its timer (+0x48C). The name promises a detection
    // lever; on VR 1.2.72 it is inert. Verified two ways rather than assumed, after calling it in-game moved
    // no detection level:
    //   - a disassembly xref over .text finds only THREE instructions touching +0x488 — the write in this
    //     setter, the read in its getter at 0x140e91100, and a read in AIProcess::SaveGame (serialization);
    //   - and that getter has ZERO callers anywhere in the binary, which closes the loophole a field-only
    //     scan leaves open (a read through an accessor is invisible to it).
    // So the value is written, persisted into the save, and never consulted by the detection code. Its
    // sibling AIProcess::ModDetectionModifierTimer (VR 0x140e99a50) really does take void — it subtracts a
    // constant from the timer — but with nothing reading the modifier that is moot.
    //
    // The live equivalent is HighProcessData::lightLevel, declared just below — see
    // F4VR-ImmersiveFlashlight's docs/tech/npc-light-detection.md §3.3 for the full findings.

    // AIProcess::GetLightLevel() / AIProcess::SetLightLevel(float) — HighProcessData::lightLevel (+0x490),
    // the actor's cached illumination, which is the quantity the engine's own detection maths asks about.
    // VR 0x140e9a3a0 / 0x140e9a3c0, raw offsets: neither has a row in the VR address library.
    //
    // Signatures verified by disassembly rather than taken from the symbol name — the note above is why that
    // now happens before anything here is called. The getter is `mov rax,[rcx+0x10]; test; jz; movss
    // xmm0,[rax+0x490]; ret`, its jz path returning 0.0 for an actor with no high process data; the setter is
    // the same shape writing xmm1. Neither touches lightLevelTimeStamp (+0x49C).
    //
    // Unlike SetDetectionModifier this one IS wired into detection: the getter is called from
    // GatherDetectionFormulaData (VR 0x140e1c820, addrlib 421115), which fills the DetectionFormulaData that
    // AiFormulas::CalculateDetectionFormula consumes, and from Actor::CalculateNormalizedLightLevel.
    //
    // The engine's own lighting refresh owns this field — it is the setter's only other caller — so a value
    // written here is transient. Re-apply per frame, and read it back if you need to know whether it survived.
    using _AIProcess_GetLightLevel = float (*)(RE::AIProcess* a_process);
    inline REL::Relocation<_AIProcess_GetLightLevel> AIProcess_GetLightLevel(REL::Offset(0xe9a3a0));

    using _AIProcess_SetLightLevel = void (*)(RE::AIProcess* a_process, float a_value);
    inline REL::Relocation<_AIProcess_SetLightLevel> AIProcess_SetLightLevel(REL::Offset(0xe9a3c0));

    // Actor::IsInActiveCombat() — in combat AND the combat group has not already ended. Use instead of the
    // Actor::IsInCombat() virtual (vtable 0xFE), which was measured to always return false on the VR runtime;
    // the bundled headers declare that slot arg-less while the VR SDK declares it taking two more arguments.
    // AddressLib 84790 resolves to VR 0x140e50350.
    using _Actor_IsInActiveCombat = bool (*)(RE::Actor* a_actor);
    inline REL::Relocation<_Actor_IsInActiveCombat> Actor_IsInActiveCombat(REL::ID(84790));

    // AIProcess::EnterCombat(Actor* self, Actor* target, Actor*) — make the actor owning this process enter
    // combat with the target; the trailing actor is passed null. AddressLib 1514649 resolves to VR
    // 0x140e8f700. This is the call the engine's own Papyrus SendAssaultAlarm implementation makes right after
    // Actor::AttackAlarm, i.e. the one that actually engages combat rather than merely recording intent.
    //
    // Two other routes are declared below and reachable via f4vr::StartCombatMethod. All three are CONFIRMED
    // WORKING on VR 1.2.72, each verified against an actor not already fighting the target — the precondition
    // that matters, since every one of them is a no-op on an actor already in combat.
    using _AIProcess_EnterCombat = void (*)(RE::AIProcess* a_process, RE::Actor* a_actor, RE::Actor* a_target, RE::Actor* a_null);
    inline REL::Relocation<_AIProcess_EnterCombat> AIProcess_EnterCombat(REL::ID(1514649));

    // Actor::StartCombat(Actor* target, Actor* commandingActor) — immediate, on the calling thread. The
    // parameter reading is pinned by the Papyrus binding the PDB names
    // GameScript::mem_Actor_StartCombat(IVirtualMachine*, uint, Actor* self, Actor* target, bool commandAlly),
    // matching `Actor.StartCombat(Actor akTarget, bool abCommandAlly)`: the member takes the target plus the
    // actor doing the commanding, null for anyone aggroing on its own account. (That binding would be the
    // safer thing to call, but it has no row in the VR address library at all.) AddressLib 765218 resolves to
    // VR 0x140e4fe00 — status 2, the least-verified of the three.
    using _Actor_StartCombat = void (*)(RE::Actor* a_actor, RE::Actor* a_target, RE::Actor* a_commandingActor);
    inline REL::Relocation<_Actor_StartCombat> Actor_StartCombat(REL::ID(765218));

    // TaskQueueInterface::QueueActorStartCombat(Actor& combatant, Actor& target, bool commandAlly) — queued
    // rather than immediate, so the engine applies it on its own schedule instead of mid-frame. AddressLib
    // 179777 resolves to VR 0x140daa620 (status 4), but note its queue singleton is AddressLib 7491, which the
    // VR address library lists at status 2 under an auto-generated name (`DAT_…`) — a task written into a
    // global that nobody drains would fail silently, so this is the shakiest chain of the three despite the
    // function itself being well mapped. The trailing bool is `abCommandAlly` per the Papyrus signature above.
    using _TaskQueueInterface_QueueActorStartCombat = void (*)(RE::TaskQueueInterface*, RE::Actor*, RE::Actor*, bool);
    inline REL::Relocation<_TaskQueueInterface_QueueActorStartCombat> TaskQueueInterface_QueueActorStartCombat(REL::ID(179777));

    using _AttackBlockHandler_IsPlayerThrowingWeapon = bool (*)();
    inline REL::Relocation<_AttackBlockHandler_IsPlayerThrowingWeapon> AttackBlockHandler_IsPlayerThrowingWeapon(REL::Offset(0xfcbcd0));

    using _Actor_CanThrow = bool (*)(RE::Actor* a_actor, uint32_t equipIndex);
    inline REL::Relocation<_Actor_CanThrow> Actor_CanThrow(REL::Offset(0xe52050));
    inline REL::Relocation<uint32_t> g_equipIndex(REL::Offset(0x3706d2c));

    using _IAnimationGraphManagerHolder_NotifyAnimationGraph = void (*)(RE::IAnimationGraphManagerHolder* a_holder, const RE::BSFixedString& event);
    inline REL::Relocation<_IAnimationGraphManagerHolder_NotifyAnimationGraph> IAnimationGraphManagerHolder_NotifyAnimationGraph(REL::Offset(0x80e7f0));

    using _TESObjectREFR_UpdateAnimation = void (*)(RE::TESObjectREFR* obj, float a_delta);
    inline REL::Relocation<_TESObjectREFR_UpdateAnimation> TESObjectREFR_UpdateAnimation(REL::Offset(0x419b50));

    inline REL::Relocation<RE::NiNode*> worldRootCamera1(REL::Offset(0x6885c80));

    // loadNif native func
    //typedef int(*_loadNif)(const char* path, uint64_t parentNode, uint64_t flags);
    using _loadNif = int (*)(uint64_t path, uint64_t mem, uint64_t flags);
    inline REL::Relocation<_loadNif> loadNif(REL::Offset(0x1d0dee0));
    //REL::Relocation<_loadNif> loadNif(REL::Offset(0x1d0dd80));

    using _cloneNode = RE::NiNode* (*)(const RE::NiNode* node, NiCloneProcess* obj);
    inline REL::Relocation<_cloneNode> cloneNode(REL::Offset(0x1c13ff0));

    using _addNode = RE::NiNode* (*)(uint64_t attachNode, const RE::NiAVObject* node);
    inline REL::Relocation<_addNode> addNode(REL::Offset(0xada20));

    using _BSFadeNode_UpdateGeomArray = void* (*)(RE::NiNode* node, int somevar);
    inline REL::Relocation<_BSFadeNode_UpdateGeomArray> BSFadeNode_UpdateGeomArray(REL::Offset(0x27a9690));

    using _BSFadeNode_UpdateDownwardPass = void* (*)(RE::NiNode* node, RE::NiUpdateData* updateData, int somevar);
    inline REL::Relocation<_BSFadeNode_UpdateDownwardPass> BSFadeNode_UpdateDownwardPass(REL::Offset(0x27a8db0));

    using _BSFadeNode_MergeWorldBounds = void* (*)(RE::NiNode* node);
    inline REL::Relocation<_BSFadeNode_MergeWorldBounds> BSFadeNode_MergeWorldBounds(REL::Offset(0x27a9930));

    using _NiNode_UpdateTransformsAndBounds = void* (*)(RE::NiNode* node, RE::NiUpdateData* updateData);
    inline REL::Relocation<_NiNode_UpdateTransformsAndBounds> NiNode_UpdateTransformsAndBounds(REL::Offset(0x1c18ce0));

    using _NiNode_UpdateDownwardPass = void* (*)(RE::NiNode* node, RE::NiUpdateData* updateData, uint64_t unk, int somevar);
    inline REL::Relocation<_NiNode_UpdateDownwardPass> NiNode_UpdateDownwardPass(REL::Offset(0x1c18620));

    using _BSGraphics_Utility_CalcBoneMatrices = void* (*)(RE::BSSubIndexTriShape* node, uint64_t counter);
    inline REL::Relocation<_BSGraphics_Utility_CalcBoneMatrices> BSGraphics_Utility_CalcBoneMatrices(REL::Offset(0x1dabc60));

    using _TESObjectCELL_GetLandHeight = uint64_t (*)(RE::TESObjectCELL* cell, const RE::NiPoint3* coord, const float* height);
    inline REL::Relocation<_TESObjectCELL_GetLandHeight> TESObjectCell_GetLandHeight(REL::Offset(0x039b230));

    using _Actor_SwitchRace = void (*)(RE::Actor* a_actor, RE::TESRace* a_race, bool param3, bool param4);
    inline REL::Relocation<_Actor_SwitchRace> Actor_SwitchRace(REL::Offset(0xe07850));

    using _Actor_Reset3D = void (*)(RE::Actor* a_actor, double param2, uint64_t param3, bool param4, uint64_t param5);
    inline REL::Relocation<_Actor_Reset3D> Actor_Reset3D(REL::Offset(0xddad60));

    using _PowerArmor_ActorInPowerArmor = bool (*)(RE::Actor* a_actor);
    inline REL::Relocation<_PowerArmor_ActorInPowerArmor> PowerArmor_ActorInPowerArmor(REL::Offset(0x9bf5d0));

    using _PowerArmor_SwitchToPowerArmor = bool (*)(RE::Actor* a_actor, RE::TESObjectREFR* a_refr, uint64_t a_char);
    inline REL::Relocation<_PowerArmor_SwitchToPowerArmor> PowerArmor_SwitchToPowerArmor(REL::Offset(0x9bfbc0));

    using _AIProcess_Update3DModel = void (*)(RE::AIProcess* proc, RE::Actor* a_actor, uint64_t flags, uint64_t someNum);
    inline REL::Relocation<_AIProcess_Update3DModel> AIProcess_Update3DModel(REL::Offset(0x0e3c9c0));

    using _PowerArmor_SwitchFromPowerArmorFurnitureLoaded = void (*)(RE::Actor* a_actor, uint64_t somenum);
    inline REL::Relocation<_PowerArmor_SwitchFromPowerArmorFurnitureLoaded> PowerArmor_SwitchFromPowerArmorFurnitureLoaded(REL::Offset(0x9c1450));

    inline REL::Relocation<uint64_t> g_frameCounter(REL::Offset(0x65a2b48));
    inline REL::Relocation<std::uint64_t*> cloneAddr1(REL::Offset(0x36ff560));
    inline REL::Relocation<std::uint64_t*> cloneAddr2(REL::Offset(0x36ff564));

    using _TESObjectREFR_GetWorldSpace = RE::TESWorldSpace* (*)(RE::TESObjectREFR* a_refr);
    inline REL::Relocation<_TESObjectREFR_GetWorldSpace> TESObjectREFR_GetWorldSpace(REL::Offset(0x3f75a0));

    // using _TESDataHandler_CreateReferenceAtLocation = void* (*)(DataHandler* dataHandler, void* newRefr, f4vr::NEW_REFR_DATA* refrData);
    // inline REL::Relocation<_TESDataHandler_CreateReferenceAtLocation> TESDataHandler_CreateReferenceAtLocation(REL::Offset(0x11bd80));

    using _Actor_GetCurrentWeapon = RE::TESObjectWEAP* (*)(RE::Actor* a_actor, RE::TESObjectWEAP* weap, RE::BGSEquipIndex idx);
    inline REL::Relocation<_Actor_GetCurrentWeapon> Actor_GetCurrentWeapon(REL::Offset(0xe50da0));

    using _Actor_GetCurrentAmmo = RE::TESAmmo* (*)(RE::Actor* a_actor, RE::BGSEquipIndex idx);
    inline REL::Relocation<_Actor_GetCurrentAmmo> Actor_GetCurrentAmmo(REL::Offset(0xe05ba0));

    using _Actor_GetWeaponEquipIndex = void (*)(RE::Actor* a_actor, RE::BGSEquipIndex* idx, BGSObjectInstance* instance);
    inline REL::Relocation<_Actor_GetWeaponEquipIndex> Actor_GetWeaponEquipIndex(REL::Offset(0xe50e70));

    using _TESObjectREFR_Set3D = void (*)(RE::TESObjectREFR* a_refr, RE::NiAVObject* a_obj, bool unk);
    inline REL::Relocation<_TESObjectREFR_Set3D> TESObjectREFR_Set3D(REL::Offset(0x3ece40));

    using _TESObjectREFR_Set3DSimple = void (*)(RE::TESObjectREFR* a_refr, RE::NiAVObject* a_obj, bool unk);
    inline REL::Relocation<_TESObjectREFR_Set3DSimple> TESObjectREFR_Set3DSimple(REL::Offset(0x3edb20));

    using _TESObjectREFR_DropAddon3DReplacement = void (*)(RE::TESObjectREFR* a_refr, RE::NiAVObject* a_obj);
    inline REL::Relocation<_TESObjectREFR_DropAddon3DReplacement> TESObjectREFR_DropAddon3DReplacement(REL::Offset(0x3e9c70));

    using _BSPointerHandleManagerInterface_GetSmartPointer = void (*)(void* a_handle, void* a_refr);
    inline REL::Relocation<_BSPointerHandleManagerInterface_GetSmartPointer> BSPointerHandleManagerInterface_GetSmartPointer(REL::Offset(0xab60));

    using _TESObjectCell_AttachReference3D = void (*)(RE::TESObjectCELL* a_cell, RE::TESObjectREFR* a_refr, bool somebool, bool somebool2);
    inline REL::Relocation<_TESObjectCell_AttachReference3D> TESObjectCell_AttachReference3D(REL::Offset(0x3c8310));

    using _TESObjectREFR_AttachToParentRef3D = void (*)(RE::TESObjectREFR* a_refr);
    inline REL::Relocation<_TESObjectREFR_AttachToParentRef3D> TESObjectREFR_AttachToParentRef3D(REL::Offset(0x46b380));

    using _TESObjectREFR_AttachAllChildRef3D = void (*)(RE::TESObjectREFR* a_refr);
    inline REL::Relocation<_TESObjectREFR_AttachAllChildRef3D> TESObjectREFR_AttachAllChildRef3D(REL::Offset(0x46b8d0));

    using _TESObjectREFR_InitHavokForCollisionObject = void (*)(RE::TESObjectREFR* a_refr);
    inline REL::Relocation<_TESObjectREFR_InitHavokForCollisionObject> TESObjectREFR_InitHavokForCollisionObject(REL::Offset(0x3eee60));

    using _bhkUtilFunctions_MoveFirstCollisionObjectToRoot = void (*)(RE::NiAVObject* root, RE::NiAVObject* child);
    inline REL::Relocation<_bhkUtilFunctions_MoveFirstCollisionObjectToRoot> bhkUtilFunctions_MoveFirstCollisionObjectToRoot(REL::Offset(0x1e17050));

    using _bhkUtilFunctions_SetLayer = void (*)(RE::NiAVObject* root, std::uint32_t collisionLayer);
    inline REL::Relocation<_bhkUtilFunctions_SetLayer> bhkUtilFunctions_SetLayer(REL::Offset(0x1e16180));

    using _bhkWorld_RemoveObject = void (*)(RE::NiAVObject* root, bool a_bool, bool a_bool2);
    inline REL::Relocation<_bhkWorld_RemoveObject> bhkWorld_RemoveObject(REL::Offset(0x1df9480));

    using _bhkWorld_SetMotion = void (*)(RE::NiAVObject* root, RE::hknpMotionPropertiesId::Preset preset, bool bool1, bool bool2, bool bool3);
    inline REL::Relocation<_bhkWorld_SetMotion> bhkWorld_SetMotion(REL::Offset(0x1df95b0));

    using _bhkNPCollisionObject_AddToWorld = void (*)(RE::bhkNPCollisionObject* a_obj, RE::bhkWorld* a_world);
    inline REL::Relocation<_bhkNPCollisionObject_AddToWorld> bhkNPCollisionObject_AddToWorld(REL::Offset(0x1e07be0));

    using _TESObjectCell_GetbhkWorld = RE::bhkWorld* (*)(RE::TESObjectCELL* a_cell);
    inline REL::Relocation<_TESObjectCell_GetbhkWorld> TESObjectCell_GetbhkWorld(REL::Offset(0x39b070));

    using _Actor_GetAmmoClipPercentage = float (*)(RE::Actor* a_actor, RE::BGSEquipIndex a_idx);
    inline REL::Relocation<_Actor_GetAmmoClipPercentage> Actor_GetAmmoClipPercentage(REL::Offset(0xddf6c0));

    using _Actor_GetCurrentAmmoCount = float (*)(RE::Actor* a_actor, RE::BGSEquipIndex a_idx);
    inline REL::Relocation<_Actor_GetCurrentAmmoCount> Actor_GetCurrentAmmoCount(REL::Offset(0xddf690));

    using _Actor_SetCurrentAmmoCount = float (*)(RE::Actor* a_actor, RE::BGSEquipIndex a_idx, int a_count);
    inline REL::Relocation<_Actor_SetCurrentAmmoCount> Actor_SetCurrentAmmoCount(REL::Offset(0xddf790));

    using _ExtraDataList_setAmmoCount = void (*)(RE::ExtraDataList* a_list, int a_count);
    inline REL::Relocation<_ExtraDataList_setAmmoCount> ExtraDataList_setAmmoCount(REL::Offset(0x980d0));

    using _ExtraDataList_setCount = void (*)(RE::ExtraDataList* a_list, int a_count);
    inline REL::Relocation<_ExtraDataList_setCount> ExtraDataList_setCount(REL::Offset(0x88fe0));

    using _ExtraDataListExtraDataList = void (*)(RE::ExtraDataList* a_list);
    inline REL::Relocation<_ExtraDataListExtraDataList> ExtraDataListExtraDataList(REL::Offset(0x81360));

    using _ExtraDataListSetPersistentCell = void (*)(RE::ExtraDataList* a_list, int a_int);
    inline REL::Relocation<_ExtraDataListSetPersistentCell> ExtraDataListSetPersistentCell(REL::Offset(0x87bc0));

    // using _MemoryManager_Allocate = void* (*)(Heap* manager, uint64_t size, uint32_t someint, bool somebool);
    // inline REL::Relocation<_MemoryManager_Allocate> MemoryManager_Allocate(REL::Offset(0x1b91950));

    using _togglePipboyLight = void* (*)(RE::Actor* a_actor);
    inline REL::Relocation<_togglePipboyLight> togglePipboyLight(REL::Offset(0xf27720));

    using _isPipboyLightOn = bool* (*)(RE::Actor* a_actor);
    inline REL::Relocation<_isPipboyLightOn> isPipboyLightOn(REL::Offset(0xf27790));

    using _isPlayerRadioEnabled = uint64_t (*)();
    inline REL::Relocation<_isPlayerRadioEnabled> isPlayerRadioEnabled(REL::Offset(0xd0a9d0));

    using _getPlayerRadioFreq = float (*)();
    inline REL::Relocation<_getPlayerRadioFreq> getPlayerRadioFreq(REL::Offset(0xd0a740));

    using _ClearAllKeywords = void (*)(RE::TESForm* form);
    inline REL::Relocation<_ClearAllKeywords> ClearAllKeywords(REL::Offset(0x148270));

    using _AddKeyword = void (*)(RE::TESForm* form, RE::BGSKeyword* keyword);
    inline REL::Relocation<_AddKeyword> AddKeyword(REL::Offset(0x148310));

    // using _ControlMap_SaveRemappings = void(*)(InputManager* a_mgr);
    // inline REL::Relocation<_ControlMap_SaveRemappings> ControlMap_SaveRemappings(REL::Offset(0x1bb3f00));

    using _ForceGamePause = void (*)(RE::MenuControls* a_mgr);
    inline REL::Relocation<_ForceGamePause> ForceGamePause(REL::Offset(0x1323370));

    using _AIProcess_ClearMuzzleFlashes = void* (*)(RE::AIProcess* middleProcess);
    inline REL::Relocation<_AIProcess_ClearMuzzleFlashes> AIProcess_ClearMuzzleFlashes(REL::Offset(0xecc710));

    using _AIProcess_CreateMuzzleFlash = void* (*)(RE::AIProcess* middleProcess, uint64_t projectile, RE::Actor* actor);
    inline REL::Relocation<_AIProcess_CreateMuzzleFlash> AIProcess_CreateMuzzleFlash(REL::Offset(0xecc570));

    // using _SettingCollectionList_GetPtr = RE::Setting * (*)(SettingCollectionList* list, const char* name);
    // inline REL::Relocation<_SettingCollectionList_GetPtr> SettingCollectionList_GetPtr(REL::Offset(0x501500));

    using _BSFlattenedBoneTree_GetBoneIndex = int (*)(RE::NiAVObject* a_tree, RE::BSFixedString* a_name);
    inline REL::Relocation<_BSFlattenedBoneTree_GetBoneIndex> BSFlattenedBoneTree_GetBoneIndex(REL::Offset(0x1c20c80));

    using _BSFlattenedBoneTree_GetBoneNode = RE::NiNode* (*)(RE::NiAVObject* a_tree, RE::BSFixedString* a_name);
    inline REL::Relocation<_BSFlattenedBoneTree_GetBoneNode> BSFlattenedBoneTree_GetBoneNode(REL::Offset(0x1c21070));

    using _BSFlattenedBoneTree_GetBoneNodeFromPos = RE::NiNode* (*)(RE::NiAVObject* a_tree, int a_pos);
    inline REL::Relocation<_BSFlattenedBoneTree_GetBoneNodeFromPos> BSFlattenedBoneTree_GetBoneNodeFromPos(REL::Offset(0x1c21030));

    using _BSFlattenedBoneTree_UpdateBoneArray = void* (*)(RE::NiAVObject* node);
    inline REL::Relocation<_BSFlattenedBoneTree_UpdateBoneArray> BSFlattenedBoneTree_UpdateBoneArray(REL::Offset(0x1c214b0));

    // Native function that takes the 1st person skeleton weapon node and calculates the skeleton from upper-arm down based off the offsetNode
    using _Update1StPersonArm = void* (*)(const RE::PlayerCharacter* pc, RE::NiNode** weapon, RE::NiNode** offsetNode);
    inline REL::Relocation<_Update1StPersonArm> Update1StPersonArm(REL::Offset(0xef6280));

    // Taken from FRIK hooks
    //REL::Relocation<uintptr_t> hookBeforeRenderer(REL::Offset(0xd844bc));   // this hook didn't work as only a few nodes get moved
    inline REL::Relocation hookBeforeRenderer(REL::Offset(0x1C21156));
    // This hook is in member function REL::Offset(0x33) for BSFlattenedBoneTree right before it updates it's own data buffer of all the skeleton world transforms.   I think that buffer is what actually gets rendered

    inline REL::Relocation hookAnimationVFunc(REL::Offset(0xf2f0a8)); // This is PostUpdateAnimationGraphManager virtual function that updates the player skeleton below the hmd.

    inline REL::Relocation hookPlayerUpdate(REL::Offset(0xf1004c));

    inline REL::Relocation hookBoneTreeUpdate(REL::Offset(0xd84ee4));

    inline REL::Relocation hookEndUpdate(REL::Offset(0xd84f2c));
    inline REL::Relocation hookMainDrawCandidate(REL::Offset(0xd844bc));
    inline REL::Relocation hookMainDrawandUi(REL::Offset(0xd87ace));

    using _hookedFunc = void (*)(uint64_t param1, uint64_t param2, uint64_t param3);
    inline REL::Relocation<_hookedFunc> hookedFunc(REL::Offset(0x1C18620));

    using _hookedPosPlayerFunc = void (*)(double param1, double param2, double param3);
    inline REL::Relocation<_hookedPosPlayerFunc> hookedPosPlayerFunc(REL::Offset(0x2841530));

    using _hookedMainDrawCandidateFunc = void (*)(uint64_t param1, uint64_t param2, uint64_t param3, uint64_t param4);
    inline REL::Relocation<_hookedMainDrawCandidateFunc> hookedMainDrawCandidateFunc(REL::Offset(0xd831f0));

    using _hookedf10ed0 = void (*)(uint64_t pc);
    inline REL::Relocation<_hookedf10ed0> hookedf10ed0(REL::Offset(0xf10ed0));

    using _hookedda09a0 = void (*)(uint64_t parm);
    inline REL::Relocation<_hookedda09a0> hookedda09a0(REL::Offset(0xda09a0));

    using _hooked1c22fb0 = void (*)(uint64_t a, uint64_t b);
    inline REL::Relocation<_hooked1c22fb0> hooked1c22fb0(REL::Offset(0x1c22fb0));

    using _main_update_player = void (*)(uint64_t rcx, uint64_t rdx);
    inline REL::Relocation<_main_update_player> main_update_player(REL::Offset(0x1c22fb0));
    inline REL::Relocation hook_MainUpdatePlayer(REL::Offset(0x0f0ff6a));

    using _hookMultiBoundCullingFunc = void (*)();
    inline REL::Relocation<_hookMultiBoundCullingFunc> hookMultiBoundCullingFunc(REL::Offset(0x0d84930));
    inline REL::Relocation hookMultiBoundCulling(REL::Offset(0x0d8445d));

    using _smoothMovementHook = void (*)(uint64_t rcx);
    inline REL::Relocation<_smoothMovementHook> smoothMovementHook(REL::Offset(0x1ba7ba0));
    inline REL::Relocation hook_smoothMovementHook(REL::Offset(0xd83ec4));

    using _someRandomFunc = void (*)(uint64_t rcx);
    inline REL::Relocation<_someRandomFunc> someRandomFunc(REL::Offset(0xd3c820));
    inline REL::Relocation hookSomeRandomFunc(REL::Offset(0xd8405e));

    using _Actor_ReEquipAll = void (*)(RE::Actor* a_actor);
    inline REL::Relocation<_Actor_ReEquipAll> Actor_ReEquipAll(REL::Offset(0xddf050));
    inline REL::Relocation hookActor_ReEquipAllExit(REL::Offset(0xf01528));

    using _ExtraData_SetMultiBoundRef = void (*)(std::uint64_t rcx, std::uint64_t rdx);
    inline REL::Relocation<_ExtraData_SetMultiBoundRef> ExtraData_SetMultiBoundRef(REL::Offset(0x91320));
    inline REL::Relocation hookExtraData_SetMultiBoundRef(REL::Offset(0xf00dc6));

    inline REL::Relocation hookActor_GetCurrentWeaponForGunReload(REL::Offset(0xf3027c));

    using _TESObjectREFR_SetupAnimationUpdateDataForRefernce = uint64_t (*)(uint64_t rcx, float* rdx);
    inline REL::Relocation<_TESObjectREFR_SetupAnimationUpdateDataForRefernce> TESObjectREFR_SetupAnimationUpdateDataForRefernce(REL::Offset(0x4189c0));
    inline REL::Relocation hookActor_SetupAnimationUpdateDataForRefernce(REL::Offset(0xf0fbdf));

    inline REL::Relocation wandMesh(REL::Offset(0x2d686d8));

    // VR render camera globals block (VR 1.2.72) — holds the per-eye view-projection matrices and the
    // world->VR-origin posAdjust vectors the engine used to render the current frame. Read by the
    // debug-draw overlay (f4cf::debug) so drawn geometry projects exactly where the game drew it
    // (HMD pose already baked in). If overlay geometry reads as garbage after a runtime change this
    // is the first suspect. Source: ROCK DebugBodyOverlay.cpp:1224-1242 (ROCK 0.6) via reference
    // library knowledge-base/debug_draw_overlay.md section 5.3; the sub-offsets are from the same site.
    inline REL::Relocation<std::uintptr_t*> vrRenderCameraGlobals(REL::Offset(0x6235AC8));
    // [vrRenderCameraGlobals + 0x25D0] -> camera data block pointer
    constexpr std::uintptr_t VR_RENDER_CAMERA_DATA_OFFSET = 0x25D0;
    // [cameraData + 0xD0 / + 0x2E0] -> eye 0 / eye 1 column-major 4x4 view-projection matrix
    constexpr std::uintptr_t VR_RENDER_CAMERA_EYE0_VIEW_PROJ_OFFSET = 0xD0;
    constexpr std::uintptr_t VR_RENDER_CAMERA_EYE1_VIEW_PROJ_OFFSET = 0x2E0;
    // [vrRenderCameraGlobals + 0x2590 / + 0x25A0] -> eye 0 / eye 1 CURRENT-frame posAdjust float3.
    // The engine copies +0x2590/+0x25A0/+0x25B0/+0x25C0 as one four-slot family, where
    // +0x25B0/+0x25C0 hold the PREVIOUS-frame origins used by temporal reprojection. Reading
    // +0x25C0 as the right-eye origin lags one frame of room translation and stutters the right
    // eye under stick locomotion. ROCK 0.1.0 had that bug and fixed it in 0.6; this framework
    // inherited the pre-fix value via the knowledge-base doc, which was written from ROCK 0.1.0.
    constexpr std::uintptr_t VR_RENDER_CAMERA_EYE0_POS_ADJUST_OFFSET = 0x2590;
    constexpr std::uintptr_t VR_RENDER_CAMERA_EYE1_POS_ADJUST_OFFSET = 0x25A0;
    static_assert(VR_RENDER_CAMERA_EYE1_POS_ADJUST_OFFSET - VR_RENDER_CAMERA_EYE0_POS_ADJUST_OFFSET == 0x10,
        "Eye origins are adjacent 16-byte slots; a 0x30 delta means a previous-frame origin crept back in.");

    // Load a specific texture by file path like "data\Textures\Effects\Gobos\FlashlightGobo01.DDS"
    // call with: unk1=1, unk2=0, unk3=0, unk4=0
    // the texture is cashed by the game so this can be used to load textures that are later used by the game by replacing the caching key (path)
    typedef void (*_LoadTextureByPath)(const char* filePath, bool unk1, RE::NiTexture*& texture, int32_t unk2, uint64_t unk3, uint64_t unk4);
    inline REL::Relocation<_LoadTextureByPath> LoadTextureByPath(REL::Offset(0x027F5520));
}
