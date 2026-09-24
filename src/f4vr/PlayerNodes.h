#pragma once

#include "BSFlattenedBoneTree.h"
#include "F4VRUtils.h"

namespace f4cf::f4vr
{
    // part of PlayerCharacter object but making useful struct below since not mapped in F4SE
    struct PlayerNodes
    {
        RE::NiNode* playerworldnode; //0x06E0
        RE::NiNode* roomnode; //0x06E8
        RE::NiNode* primaryWandNode; //0x06F0
        RE::NiNode* primaryWandandTouchPad; //0x06F8
        RE::NiNode* primaryUIAttachNode; //0x0700
        RE::NiNode* primaryWeapontoWeaponNode; //0x0708
        RE::NiNode* primaryWeaponKickbackRecoilNode; //0x0710
        RE::NiNode* primaryWeaponOffsetNOde; //0x0718
        RE::NiNode* primaryWeaponScopeCamera; //0x0720
        RE::NiNode* primaryVertibirdMinigunOffNOde; //0x0728
        RE::NiNode* primaryMeleeWeaponOffsetNode; //0x0730
        RE::NiNode* primaryUnarmedPowerArmorWeaponOffsetNode; //0x0738
        RE::NiNode* primaryWandLaserPointer; //0x0740
        RE::NiNode* PrimaryWandLaserPointerAdjuster; //0x0748
        RE::NiNode* unk750; //0x0750
        RE::NiNode* PrimaryMeleeWeaponOffsetNode; //0x0758
        RE::NiNode* SecondaryMeleeWeaponOffsetNode; //0x0760
        RE::NiNode* SecondaryWandNode; //0x0768
        RE::NiNode* Point002Node; //0x0770
        RE::NiNode* WorkshopPalletNode; //0x0778
        RE::NiNode* WorkshopPallenSlide; //0x0780
        RE::NiNode* SecondaryUIOffsetNode; //0x0788
        RE::NiNode* SecondaryMeleeWeaponOffsetNode2; //0x0790
        RE::NiNode* SecondaryUnarmedPowerArmorWeaponOffsetNode; //0x0798
        RE::NiNode* SecondaryAimNode; //0x07A0
        RE::NiNode* PipboyParentNode; //0x07A8
        RE::NiNode* PipboyRoot_nif_only_node; //0x07B0
        RE::NiNode* ScreenNode; //0x07B8
        RE::NiNode* PipboyLightParentNode; //0x07C0
        RE::NiNode* unk7c8; //0x07C8
        RE::NiNode* ScopeParentNode; //0x07D0
        RE::NiNode* unk7d8; //0x07D8
        RE::NiNode* HmdNode; //0x07E0
        RE::NiNode* OffscreenHmdNode; //0x07E8
        RE::NiNode* UprightHmdNode; //0x07F0
        RE::NiNode* UprightHmdLagNode; //0x07F8
        RE::NiNode* BlackSphereNode; //0x0800
        RE::NiNode* HeadLightParentNode; //0x0808
        RE::NiNode* unk810; //0x0810
        RE::NiNode* WeaponLeftNode; //0x0818
        RE::NiNode* unk820; //0x0820
        RE::NiNode* LockPickParentNode; //0x0828
    };

    // Guards the cast in getPlayerNodes() below: this struct must not describe more bytes than the one
    // it aliases. RE::VRPlayerNodes in CommonLibF4 is where this table is documented and maintained.
    static_assert(sizeof(PlayerNodes) <= sizeof(RE::VRPlayerNodes));

    inline RE::PlayerCharacter* getPlayer()
    {
        return RE::PlayerCharacter::GetSingleton();
    }

    /**
     * The VR node table, typed by CommonLibF4 as a real PlayerCharacter member, so the offsets are
     * checked by the compiler rather than by a cast. Prefer this over getPlayerNodes().
     */
    inline RE::VRPlayerNodes* getVRPlayerNodes()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        return player ? &player->vrNodes : nullptr;
    }

    /**
     * The same table under the older member names, kept so existing callers still build.
     * New code should use getVRPlayerNodes(), whose struct is the one that is maintained.
     */
    inline PlayerNodes* getPlayerNodes()
    {
        return reinterpret_cast<PlayerNodes*>(getVRPlayerNodes());
    }

    inline RE::NiNode* getWorldRootNode()
    {
        const auto player = getPlayer();
        if (!player || !player->loadedData || !player->loadedData->data3D) {
            return nullptr;
        }
        return player->loadedData->data3D->IsNode();
    }

    inline RE::NiNode* getRootNode()
    {
        const auto root = getWorldRootNode();
        if (!root || root->children.empty()) {
            return nullptr;
        }
        return root->children[0] ? root->children[0]->IsNode() : nullptr;
    }

    // is it the 3rd-person bone tree?
    inline BSFlattenedBoneTree* getFlattenedBoneTree()
    {
        const auto rootNode = getRootNode();
        return rootNode ? reinterpret_cast<BSFlattenedBoneTree*>(rootNode) : nullptr;
    }

    inline RE::NiNode* getFirstPersonSkeleton()
    {
        const auto player = getPlayer();
        return player ? player->firstPerson3D.get() : nullptr;
    }

    inline BSFlattenedBoneTree* getFirstPersonBoneTree()
    {
        const auto fpSkeleton = getFirstPersonSkeleton();
        if (!fpSkeleton || fpSkeleton->children.empty()) {
            return nullptr;
        }
        return fpSkeleton->children[0] ? reinterpret_cast<BSFlattenedBoneTree*>(fpSkeleton->children[0]->IsNode()) : nullptr;
    }

    inline RE::EquippedWeaponData* getEquippedWeaponData()
    {
        const auto* process = getPlayer()->currentProcess;
        const auto* middleHigh = process ? process->middleHigh : nullptr;
        if (!middleHigh || middleHigh->equippedItems.empty()) {
            return nullptr;
        }
        return static_cast<RE::EquippedWeaponData*>(middleHigh->equippedItems[0].data.get());
    }

    inline RE::NiNode* getCommonNode()
    {
        return findNode(getRootNode(), "COM");
    }

    inline RE::NiNode* getWeaponNode()
    {
        return findNode(getFirstPersonSkeleton(), "Weapon");
    }

    inline RE::NiNode* getPrimaryWandNode()
    {
        return findNode(getVRPlayerNodes()->primaryUIAttachNode, "world_primaryWand.nif");
    }

    /**
     * The throwable weapon is attached to the melee node but only exists if the player is actively throwing the weapon.
     * @return found throwable node or nullptr if not
     */
    inline RE::NiNode* getThrowableWeaponNode()
    {
        const auto meleeNode = getVRPlayerNodes()->primaryMeleeWeaponOffsetNode;
        return !meleeNode->children.empty() ? meleeNode->children[0]->IsNode() : nullptr;
    }

    inline RE::PlayerCamera* getPlayerCamera()
    {
        return RE::PlayerCamera::GetSingleton();
    }

    inline RE::NiPoint3 getCameraPosition()
    {
        return getPlayerCamera()->cameraRoot->world.translate;
    }

    inline RE::NiNode* getLeftHandNode()
    {
        return isLeftHandedMode() ? getVRPlayerNodes()->primaryWandNode : getVRPlayerNodes()->secondaryWandNode;
    }

    inline RE::NiNode* getRightHandNode()
    {
        return isLeftHandedMode() ? getVRPlayerNodes()->secondaryWandNode : getVRPlayerNodes()->primaryWandNode;
    }

    inline RE::NiNode* getOffhandWandNode()
    {
        return getVRPlayerNodes()->secondaryWandNode;
    }

    inline RE::NiNode* getPrimaryHandWandNode()
    {
        return getVRPlayerNodes()->primaryWandNode;
    }
}
