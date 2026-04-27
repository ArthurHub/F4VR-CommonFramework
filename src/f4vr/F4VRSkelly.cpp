#include "f4vr/F4VRSkelly.h"

#include "PlayerNodes.h"

namespace f4cf::f4vr
{
    void Skelly::initBoneTreeMap()
    {
        _boneTreeMap.clear();
        _boneTreeVec.clear();

        const auto rt = getFlattenedBoneTree();
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
        const auto foundBone = _boneTreeMap.find(boneName);
        if (foundBone == _boneTreeMap.end()) {
            logger::warn("Bone {} not found in bone tree map", boneName);
            return RE::NiTransform();
        }
        return getFlattenedBoneTree()->transforms[foundBone->second].world;
    }

    /**
     * Get the world position of the offhand index fingertip .
     * Make small adjustment as the finger bone position is the center of the finger.
     * Would be nice to know how long the bone is instead of magic numbers, didn't find a way so far.
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
        return boneTransform.translate + forward * (isInPowerArmor() ? 3 : 1.8f);
    }
}
