#include "UIUtils.h"

#include "ModBase.h"
#include "f4vr/PlayerNodes.h"
#include "vrcf/VRControllersHaptic.h"

namespace f4cf::vrui
{
    /**
     * Get node of the primary hand wand to attach UI to.
     */
    RE::NiNode* UIUtils::getPrimaryWandAttachNode()
    {
        return f4vr::getVRPlayerNodes()->primaryUIAttachNode;
    }

    /**
     * Get node of the offhand wand to attach UI to.
     */
    RE::NiNode* UIUtils::getOffhandWandAttachNode()
    {
        return f4vr::getVRPlayerNodes()->secondaryUIOffsetNode;
    }

    /**
     * Get node of the HMD to attach UI to.
     */
    RE::NiNode* UIUtils::getHMDAttachNode()
    {
        return RE::NiNodeUtils::FindNode(f4vr::getVRPlayerNodes()->uprightHmdNode, "world_HMD_info.nif");
    }

    /**
     * Is the game played left-handed.
     */
    bool UIUtils::isLeftHandedMode()
    {
        return RE::VRSettings::IsLeftHandedMode();
    }

    /**
     * Trigger haptic on the hand that pressed, to indicate interaction with the VR UI.
     */
    void UIUtils::triggerInteractionHeptic(const bool primaryHand)
    {
        vrcf::VRHaptics.trigger(primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand, vrcf::HapticPattern::Click);
    }

    /**
     * Find node by name in the given subtree of the given root node.
     */
    RE::NiNode* UIUtils::findNode(RE::NiNode* node, const char* nodeName)
    {
        return RE::NiNodeUtils::FindNode(node, nodeName);
    }
}
