#include "f4vr/F4VRSkelly.h"

#include "PlayerNodes.h"

namespace f4cf::f4vr
{
    namespace
    {
        // From the last bone of the index finger, which is inside the finger, along the bone to the fingertip.
        // Would be nice to know how long the bone is instead of magic numbers, didn't find a way so far.
        constexpr float FINGER_BONE_TO_TIP = 2.0f;
        constexpr float FINGER_BONE_TO_TIP_POWER_ARMOR = 3.0f;
    }

    /**
     * Map the bones of the player's skeleton by name. Bones are also mapped on the first read, and again
     * when the skeleton is replaced, so calling this is only needed to do the work at a chosen time.
     */
    void Skelly::initBoneTreeMap()
    {
        _boneTreeMap.clear();
        _boneTreeVec.clear();

        const auto rt = getFlattenedBoneTree();
        _boneTreeMapTree = rt;
        if (!rt || !rt->transforms) {
            return;
        }
        for (auto i = 0; i < rt->numTransforms; i++) {
            logger::trace("BoneTree Init -> Push {} into position {}", rt->transforms[i].name.c_str(), i);
            _boneTreeMap.insert({ rt->transforms[i].name.c_str(), i });
            _boneTreeVec.emplace_back(rt->transforms[i].name.c_str());
        }
        logger::debug("BoneTree Map Initialized with {} entries", _boneTreeMap.size());
    }

    const std::string& Skelly::getBoneName(const int index)
    {
        return _boneTreeVec[index];
    }

    RE::NiTransform Skelly::getBoneWorldTransform(const std::string& boneName)
    {
        const auto transform = findBoneWorldTransform(boneName);
        if (!transform) {
            logger::warn("Bone {} not found in bone tree map", boneName);
            return RE::NiTransform();
        }
        return *transform;
    }

    /**
     * The bone's world transform, as the flattened bone tree holds it, which is where the fingers are posed.
     * @return nothing if there is no skeleton or it has no bone by this name
     */
    std::optional<RE::NiTransform> Skelly::findBoneWorldTransform(const std::string& boneName)
    {
        if (!isBoneTreeMapCurrent()) {
            initBoneTreeMap();
        }
        const auto foundBone = _boneTreeMap.find(boneName);
        if (foundBone == _boneTreeMap.end()) {
            return std::nullopt;
        }
        return getFlattenedBoneTree()->transforms[foundBone->second].world;
    }

    /**
     * Whether the map was built for the skeleton the player has now. The skeleton is replaced on a save load
     * and on entering or leaving power armor, which has other bones.
     */
    bool Skelly::isBoneTreeMapCurrent()
    {
        const auto tree = getFlattenedBoneTree();
        if (tree != _boneTreeMapTree) {
            return false;
        }
        return !tree || !tree->transforms || tree->numTransforms == static_cast<int>(_boneTreeVec.size());
    }

    /**
     * Get the world position of the index fingertip of the given hand.
     * Make small adjustment as the finger bone position is the center of the finger.
     */
    RE::NiPoint3 Skelly::getIndexFingerTipWorldPosition(const vrcf::Hand& hand)
    {
        bool rightHand = false;
        switch (hand) {
        case vrcf::Hand::Primary:
            rightHand = !isLeftHandedMode();
            break;
        case vrcf::Hand::Offhand:
            rightHand = isLeftHandedMode();
            break;
        case vrcf::Hand::Right:
            rightHand = true;
            break;
        case vrcf::Hand::Left:
            rightHand = false;
            break;
        }
        const auto indexFinger = rightHand ? "RArm_Finger23" : "LArm_Finger23";
        const auto boneTransform = getBoneWorldTransform(indexFinger);
        const auto forward = boneTransform.rotate.Transpose() * (RE::NiPoint3(1, 0, 0));
        return boneTransform.translate + forward * (isInPowerArmor() ? FINGER_BONE_TO_TIP_POWER_ARMOR : FINGER_BONE_TO_TIP);
    }
}
