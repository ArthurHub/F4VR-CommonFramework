#pragma once

#include "BSFlattenedBoneTree.h"
#include "F4VRUtils.h"

namespace f4cf::f4vr
{
    inline RE::PlayerCharacter* getPlayer()
    {
        return RE::PlayerCharacter::GetSingleton();
    }

    /**
     * The VR node table at PlayerCharacter + 0x6E0, typed by CommonLibF4 as a real PlayerCharacter member,
     * so the offsets are checked by the compiler rather than by a cast.
     */
    inline RE::VRPlayerNodes* getVRPlayerNodes()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        return player ? &player->vrNodes : nullptr;
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
