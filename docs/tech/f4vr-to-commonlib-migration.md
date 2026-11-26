# Migrating to 0.6: the game RE moved to CommonLibF4VR

Framework 0.6 has no game addresses of its own. Everything that was a fact about `Fallout4VR.exe` (its
functions, globals, settings, structs, hook sites and the scene-graph helpers) is now in CommonLibF4VR, as
members of the classes they belong to or in a few new headers. What stays in `f4cf::f4vr` is the framework's
own convenience and policy: player and weapon state, menus, Scaleform, activation spheres, player turning.

CommonLibF4VR tags each address with how it is known to work on VR (`// VR: tested`, `// VR: checked`,
`// VR: broken (why)`), and its `VRAddressCheck` checks them against the game; see its
[README](../../external/CommonLibF4VR/README.md#what-works-on-vr).

Everything below is in `RE/Fallout.h`, which a mod's PCH already includes, so the only include to change is
dropping the removed headers:

| Removed header | Its replacement |
|---|---|
| `f4vr/F4VROffsets.h` | the CommonLibF4VR members in the tables below |
| `f4vr/MiscStructs.h` | `RE::BGSObjectInstance`, `RE::NiCloningProcess`, `RE::NEW_REFR_DATA`, `RE::MuzzleFlash` |
| `f4vr/BSFlattenedBoneTree.h` | `RE/Bethesda/BSFlattenedBoneTree.h` |
| `f4vr/CollisionLayers.h` | `RE/Bethesda/CollisionLayers.h` |

## Helpers

| `f4vr::` (0.5) | CommonLibF4VR (0.6) | Notes |
|---|---|---|
| `isLeftHandedMode()` | `RE::VRSettings::IsLeftHandedMode()` | |
| `isComfortSneakMode()` | `RE::VRSettings::IsComfortSneak()` | |
| `useWandDirectionalMovement()` | `RE::VRSettings::IsWandDirectionalMovement()` | |
| `PipboyPlacement::Wrist` / `InFront` / `Projected` | `RE::VRSettings::PipboyPlacement::kWrist` / `kInFront` / `kProjected` | same values |
| `getPipboyPlacement()`, `setPipboyPlacement(p)` | `RE::VRSettings::GetPipboyPlacement()`, `SetPipboyPlacement(p)` | the setter still runs the game's handlers and saves `Fallout4Prefs.ini` |
| `isPipboyOnWrist()` | `RE::VRSettings::IsPipboyOnWrist()` | |
| `isSwimming(player)`, `isUnderwater(player)` | `actor->IsSwimming()`, `actor->IsUnderwater()` | any actor; the helpers took the player by default |
| `getProcessLists()` | `RE::ProcessLists::GetSingleton()` | its ID resolves on VR |
| `getActorsWithinRangeOfPoint(p, r, out)` | `RE::ProcessLists::GetSingleton()->GetActorsWithinRangeOfPoint(p, r, out)` | null-check the singleton |
| `getActorFacingAngleTo(a, p)`, `isActorFacing(a, p, deg)` | `a->GetFacingAngleTo(p)`, `a->IsFacing(p, deg)` | |
| `isInActiveCombat(a)` | `a->IsInActiveCombat()` | |
| `getDetectionLevel(o, t)` | `o->RequestDetectionLevel(t)` | the helper returned -1 for an observer without `currentProcess->high`: check that first |
| `getLightLevel(a)`, `setLightLevel(a, v)` | `a->currentProcess->GetActorLightLevel()`, `SetActorLightLevel(v)` | null-check `currentProcess`; both treat missing high process data as the helpers did |
| `getFirstChild(o)`, `get1StChildNode(o)` | `RE::NiNodeUtils::GetFirstChild(o)` | |
| `find1StChildNode(o, name)` | `RE::NiNodeUtils::FindNode(o, name, 1)` | |
| `findAVObject`, `findAVObjectStartsWith` | `RE::NiNodeUtils::FindObject`, `FindObjectStartsWith` | same depth rule; no depth means the whole tree |
| `findNode`, `findNodeStartsWith` | `RE::NiNodeUtils::FindNode`, `FindNodeStartsWith` | |
| `isNodeVisible`, `setNodeVisibility`, `setNodeVisibilityDeep` | `RE::NiNodeUtils::IsVisible`, `SetVisible`, `SetVisibleDeep` | |
| `updateDown(o, self, ignore)` | `RE::NiNodeUtils::UpdateWorldDataDown(o, includeRoot, skipName)` | the name to skip is a `std::string_view`: pass `{}`, not `nullptr`, for none |
| `updateDownTo`, `updateUpTo` | `RE::NiNodeUtils::UpdateWorldDataDownTo`, `UpdateWorldDataUpTo` | |
| `updateTransforms(o)` | `RE::NiNodeUtils::UpdateTransformFromParent(o)` | |
| `updateTransformsDown(o, self, ignore)` | `RE::NiNodeUtils::UpdateTransformsDown(o, includeRoot, skipName)` | as `UpdateWorldDataDown` |
| `getFlattenedBoneTree()`, `getFirstPersonBoneTree()` | `player->GetBoneTree()`, `player->GetFirstPersonBoneTree()` | `RE::PlayerCharacter` members |
| `getLeftHandNode()`, `getRightHandNode()` | `player->GetLeftHandWandNode()`, `GetRightHandWandNode()` | by the player's `vrState.leftHandedMode` |
| `getCollisionLayer`, `getCollisionLayerName`, `parseCollisionLayer` | `RE::CollisionLayers::GetLayer`, `GetName`, `Parse` | |
| `collisionLayerBit`, `collisionLayerMask`, `parseCollisionLayerMask`, `isCollisionLayerInMask` | `RE::CollisionLayers::LayerBit`, `LayerMask`, `ParseMask`, `IsInMask` | `ParseMask` hands back the tokens it can't read instead of logging them |
| `SkellyBones::X` | `RE::SkeletonBones::X` | |
| `f4vr::BSFlattenedBoneTree` | `RE::BSFlattenedBoneTree` | `GetBoneIndex` and `GetBoneNode` take a `BSFixedString`; a bone's `unk8c` is `unk84` |

The helpers that stay, such as `isInPowerArmor`, `getEquippedWeapon`, `isJumpingOrInAir`, `startCombat` and
`getClonedNiNodeForNifFile`, keep their names and now call CommonLibF4VR underneath.

## `F4VROffsets.h` entries

Each entry was a `REL::Relocation` to call as a function or read as a global. Its replacement is a member or
function with the game's own signature, called as such.

| `F4VROffsets.h` (0.5) | CommonLibF4VR (0.6) |
|---|---|
| `IsInAir`, `IsSneaking` | `Actor::IsJumping`, `Actor::IsSneaking` |
| `GetEquippedWeapon` (Papyrus native) | `Actor::GetCurrentWeapon(BGSEquipIndex)`, whose `.object` the native returned |
| `SetActorRestrained` | `Actor::SetRestrained` |
| `Actor_IsInActiveCombat`, `Actor_StartCombat`, `Actor_CanThrow` | `Actor::IsInActiveCombat`, `StartCombat`, `CanThrow` |
| `Actor_SwitchRace`, `Actor_Reset3D`, `Actor_ReEquipAll` | `Actor::SwitchRace`, `Reset3D`, `ReEquipAll` |
| `Actor_GetCurrentWeapon`, `Actor_GetWeaponEquipIndex`, `Actor_GetCurrentAmmo` | `Actor::GetCurrentWeapon`, `GetWeaponEquipIndex`, `GetCurrentAmmo` |
| `Actor_GetAmmoClipPercentage`, `Actor_GetCurrentAmmoCount`, `Actor_SetCurrentAmmoCount` | `Actor::GetAmmoClipPercentage`, `GetCurrentAmmoCount`, `SetCurrentAmmoCount` |
| `AIProcess_EnterCombat`, `AIProcess_GetLightLevel`, `AIProcess_SetLightLevel` | `AIProcess::EnterCombat`, `GetActorLightLevel`, `SetActorLightLevel` |
| `AIProcess_getAnimationManager`, `AIProcess_Set3DUpdateFlags` | `AIProcess::GetAnimationGraphManager`, `Set3DUpdateFlag` |
| `AIProcess_ClearMuzzleFlashes`, `AIProcess_CreateMuzzleFlash` | `AIProcess::ClearMuzzleFlashes`, `CreateMuzzleFlash` |
| `BSAnimationManager_setActiveGraph` | `BSAnimationGraphManager::SetActiveGraph` |
| `CombatUtilities_IsActorUsingMelee`, `CombatUtilities_IsActorUsingMagic` | `CombatUtilities::IsActorUsingMelee`, `IsActorUsingMagic` |
| `g_processLists`, `ProcessLists_GetActorsWithinRangeOfPoint` | `ProcessLists::GetSingleton`, `GetActorsWithinRangeOfPoint` |
| `TaskQueueInterface_QueueActorStartCombat` | `TaskQueueInterface::QueueActorStartCombat` |
| `AttackBlockHandler_IsPlayerThrowingWeapon` | `AttackBlockHandler::IsPlayerThrowingWeapon` |
| `IAnimationGraphManagerHolder_NotifyAnimationGraph` | `IAnimationGraphManagerHolder::NotifyAnimationGraphImpl` (virtual) |
| `TESObjectREFR_UpdateAnimation`, `TESObjectREFR_Set3D` | `TESObjectREFR::UpdateAnimation`, `Set3D` (virtuals) |
| `TESObjectREFR_Set3DSimple`, `TESObjectREFR_DropAddon3DReplacement`, `TESObjectREFR_GetWorldSpace` | `TESObjectREFR::Set3DSimple`, `DropAddOn3DReplacement`, `GetWorldSpace` |
| `TESObjectREFR_AttachToParentRef3D`, `TESObjectREFR_AttachAllChildRef3D`, `TESObjectREFR_InitHavokForCollisionObject` | `TESObjectREFR::AttachToParentRef3D`, `AttachAllChildRef3D`, `InitHavokForCollisionObject` |
| `TESObjectREFR_SetupAnimationUpdateDataForRefernce` | `TESObjectREFR::SetupAnimationUpdateDataForReference` |
| `TESObjectCell_GetLandHeight`, `TESObjectCell_AttachReference3D`, `TESObjectCell_GetbhkWorld` | `TESObjectCELL::GetLandHeight`, `AttachReference3D`, `GetbhkWorld` |
| `TESDataHandler_CreateReferenceAtLocation` (commented out) | `TESDataHandler::CreateReferenceAtLocation` |
| `PowerArmor_ActorInPowerArmor`, `PowerArmor_SwitchToPowerArmor`, `PowerArmor_SwitchFromPowerArmorFurnitureLoaded` | `PowerArmor::ActorInPowerArmor`, `SwitchToPowerArmor`, `SwitchFromPowerArmorFurnitureLoaded` |
| `togglePipboyLight`, `isPipboyLightOn`, `PlayerCharacter_ShowPipboyLight` | `PlayerCharacter::TogglePipBoyLight`, `IsPipboyLightOn`, `ShowPipboyLight` |
| `Update1StPersonArm`, `PlayerCharacter_UpdateVRUI` | `PlayerCharacter::Update1stPersonArm`, `UpdateVRUI` (VR only) |
| `PLAYER_VR_UI_ANCHOR_YAW`, `_HEAD_YAW`, `_LAG_TIMER`, `_RECOVERY_SPEED` | `PlayerCharacter::vrState.uiAnchorYaw`, `uiHeadYaw`, `uiLagTimer`, `uiRecoverySpeed` |
| `PLAYER_VR_UI_ROOT_1`, `PLAYER_VR_UI_ROOT_2` | `PlayerCharacter::vrNodes.hmdNode`, `uprightHmdNode` |
| `ExtraDataListExtraDataList`, `ExtraDataList_setAmmoCount`, `ExtraData_SetMultiBoundRef` | `ExtraDataList()` (constructor), `ExtraDataList::SetAmmoCount`, `SetMultiBoundRef` |
| `ExtraDataList_setCount`, `ExtraDataListSetPersistentCell` | `ExtraDataList::SetPersistentCell`, `SetCount`: the two old names had each other's address |
| `ClearAllKeywords`, `AddKeyword` | `BGSKeywordForm::ClearAllKeywords`, `AddKeyword` |
| `ForceGamePause` | `MenuControls::ForcePauseGame` |
| `ControlMap_SaveRemappings` (commented out) | `ControlMap::SaveRemappings` |
| `SettingCollectionList_GetPtr` (commented out) | `SettingCollectionList::GetPtr` |
| `SettingHandlerMap_SettingUpdated` | `SettingHandlerMap::SettingUpdated` |
| `isPlayerRadioEnabled`, `getPlayerRadioFreq` | `RadioManager::QPlayerRadioEnabled`, `QCurrentPlayerFreq` |
| `loadNif` | `BSModelDB::Demand` |
| `cloneNode`, `cloneAddr1`, `cloneAddr2` | `NiObject::Clone` with an `NiCloningProcess`, whose hash maps carry their own sentinels |
| `NiNode_UpdateWorldBound`, `NiNode_UpdateTransformsAndBounds`, `NiNode_UpdateDownwardPass`, `BSFadeNode_UpdateDownwardPass` | `NiAVObject::UpdateWorldBound`, `UpdateTransformAndBounds`, `UpdateDownwardPass` (virtuals) |
| `BSFadeNode_UpdateGeomArray`, `BSFadeNode_MergeWorldBounds` | `BSFadeNode::UpdateGeomArray`, `MergeWorldBounds` |
| `BSGraphics_Utility_CalcBoneMatrices` | `BSGraphics::Utility::CalcBoneMatrices` (VR only) |
| `BSFlattenedBoneTree_GetBoneIndex`, `_GetBoneNode`, `_GetBoneNodeFromPos`, `_UpdateBoneArray` | `BSFlattenedBoneTree::GetBoneIndex`, `GetBoneNode` (two overloads), `UpdateBoneArray` |
| `bhkWorld_RemoveObject`, `bhkWorld_SetMotion` | `bhkWorld::RemoveObjects`, `SetMotion`, now static as in the game |
| `bhkUtilFunctions_MoveFirstCollisionObjectToRoot`, `bhkUtilFunctions_SetLayer` | `bhkUtilFunctions::MoveFirstCollisionObjectToRoot`, `SetLayer` |
| `BSLight_SetShape`, `BSLight_SetCameraFrustum` | `BSLight::SetShape`, `SetCameraFrustum` (VR only) |
| `BSLight_vtbl`, `BSShadowFrustumLight_vtbl` | `VTABLE::BSLight`, `VTABLE::BSShadowFrustumLight` |
| `EquippedWeaponData_vfunc` | `VTABLE::EquippedWeaponData` |
| `LoadTextureByPath` | `BSShaderManager::GetTexture`, into an `NiPointer<NiTexture>` that holds the one reference |
| `BSPointerHandleManagerInterface_GetSmartPointer` | `BSPointerHandleManagerInterface::GetSmartPointer` |
| `MemoryManager_Allocate` (commented out) | `MemoryManager::Allocate` |
| `g_vrWorldData`, `VR_WORLD_DATA_ROTATION_OFFSET`, `VRWorld_GetEulerAngles`, `VRWorld_SetYaw` | `VRWorldData::GetSingleton`, `GetRoomRotation`, `GetYaw`, `SetYaw` (VR only) |
| `vrRenderCameraGlobals` and the `VR_RENDER_CAMERA_*` offsets | `VRRenderCamera::GetEyeViewProjection(eye)`, `GetEyePosAdjust(eye)` (VR only) |
| the raw reads of `bLeftHandedMode`, `bComfortSneak`, the Pip-Boy and the turning settings | `VRSettings::` accessors (VR only) |

### Hook sites

The places mods patch a call are `RE::VRHookSites` (VR only), each with the call's address, the function it
calls, and `Verify()`, which checks the bytes are still that call before anything is written:

| `F4VROffsets.h` (0.5) | `RE::VRHookSites::` (0.6) |
|---|---|
| `hookAnimationVFunc` | `PlayerPostAnimGraph` (a patch site) |
| `hookPlayerUpdate` | `PlayerUpdateVirtualCall458` (a patch site) |
| `hook_MainUpdatePlayer`, `main_update_player`, `hooked1c22fb0` | `PlayerUpdate3D` |
| `hookBeforeRenderer`, `hookedFunc` | `BoneTreeUpdateDownwardPass` |
| `hookBoneTreeUpdate`, `hookedf10ed0` | `MainUpdateBoneTreeUpdate` |
| `hookEndUpdate`, `hookedda09a0` | `MainUpdateEnd` |
| `hookMainDrawCandidate`, `hookedMainDrawCandidateFunc` | `MainUpdateSwap` |
| `hookMainDrawandUi`, `hookedPosPlayerFunc` | `DrawWorldAndUIDraw` |
| `hookMultiBoundCulling`, `hookMultiBoundCullingFunc` | `MainUpdateMultiBoundCulling` |
| `hook_smoothMovementHook`, `smoothMovementHook` | `OnIdleVRMovement` |
| `hookSomeRandomFunc`, `someRandomFunc` (the framework's frame tick) | `MainUpdateFrameTick` |
| `hookActor_ReEquipAllExit` | `PowerArmorExitReEquipAll` |
| `hookExtraData_SetMultiBoundRef` | `PowerArmorEnterSetMultiBoundRef` |
| `hookActor_GetCurrentWeaponForGunReload` | `GunReloadGetCurrentWeapon` |
| `hookActor_SetupAnimationUpdateDataForRefernce` | `PlayerAnimationUpdateData` |
| `PipboyManager_InitPipboy_IsPipboyLightOnCall` | `InitPipboyIsPipboyLightOn` |
| `wandMesh` | `PrimaryWandMeshPath` (a patch site) |

### Not carried over

- `GetSitState` and `AIProcess_Update3DModel`: both addresses were in the middle of another function.
- `testin`, and `addNode` (it was `NiPointer<NiNode>::operator=`).
- `worldRootCamera1`, `g_frameCounter` and `g_equipIndex`: globals nothing used and nobody identified.
- `bhkNPCollisionObject_AddToWorld` (VR `0x141E07BE0`, ID 795695): CommonLibF4VR has no
  `bhkNPCollisionObject` class to put it on yet.
- From `MiscStructs.h`: the form IDs `PipboyAA`, `PipboyArmor`, `MiningHelmet` and `PALightKW`, which are
  `Fallout4.esm` data rather than engine facts.

`RE::MuzzleFlash` is `EquippedWeaponData::muzzleFlash`'s type. The old struct's `fireNode` (+10) is its `flash3D`,
the flash's own 3D, and its `projectileNode` (+18) is its `fireNode`, the projectile's fire node.

## Recipe

1. Bump the framework submodule to 0.6, then build: the removed names fail to compile, and each failure is
   a row above.
2. `git grep -n "F4VROffsets\|MiscStructs\|f4vr/BSFlattenedBoneTree\|f4vr/CollisionLayers"` and drop those
   includes.
3. For a call passing `nullptr` as the name to skip in `updateDown` / `updateTransformsDown`, pass `{}`.
