#pragma once

#include "vrcf/VRControllersManager.h"

namespace f4cf::f4vr
{
    struct Skelly
    {
        static void initBoneTreeMap();
        static const std::string& getBoneName(int index);
        static RE::NiTransform getBoneWorldTransform(const std::string& boneName);
        static std::optional<RE::NiTransform> findBoneWorldTransform(const std::string& boneName);
        static RE::NiPoint3 getIndexFingerTipWorldPosition(const vrcf::Hand& hand);

    private:
        static bool isBoneTreeMapCurrent();

        inline static std::map<std::string, int> _boneTreeMap;
        inline static std::vector<std::string> _boneTreeVec;

        // the bone tree the map was built for, only compared and never read through
        inline static const void* _boneTreeMapTree = nullptr;
    };
}
