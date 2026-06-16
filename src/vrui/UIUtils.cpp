#include "UIUtils.h"

#include "ModBase.h"
#include "f4vr/PlayerNodes.h"
#include "vrcf/VRControllersHaptic.h"

namespace
{
    /**
     * Extract the element size (in framework units) from the nif root-node name, which the
     * atlas packer writes as "VRUI (W:2 H:2.1)" (100px = 1 unit). Returns UISize(-1, -1) when
     * the name carries no size.
     */
    f4cf::vrui::UISize extractElementSizeFromName(const std::string& nifName)
    {
        const auto wPos = nifName.find("W:");
        const auto hPos = nifName.find("H:");
        if (wPos == std::string::npos || hPos == std::string::npos)
            return { -1.0f, -1.0f };

        try {
            return { std::stof(nifName.substr(wPos + 2)), std::stof(nifName.substr(hPos + 2)) };
        } catch (...) {
            return { -1.0f, -1.0f };
        }
    }
}

namespace f4cf::vrui
{
    std::string UIUtils::getDebugSphereNifName()
    {
        return "1x1Sphere.nif";
    }

    std::string UIUtils::getEmptyButtonFrameNifName()
    {
        return "ui-common\\btn-empty.nif";
    }

    std::string UIUtils::getToggleButtonFrameNifName()
    {
        return "ui-common\\btn-border.nif";
    }

    std::string UIUtils::getDisabledOverlayNifName()
    {
        return "ui-common\\btn-disable-overlay.nif";
    }

    /**
     * Get node of the primary hand wand to attach UI to.
     */
    RE::NiNode* UIUtils::getPrimaryWandAttachNode()
    {
        return f4vr::getPlayerNodes()->primaryUIAttachNode;
    }

    /**
     * Get node of the HMD to attach UI to.
     */
    RE::NiNode* UIUtils::getHMDAttachNode()
    {
        return findNode(f4vr::getPlayerNodes()->UprightHmdNode, "world_HMD_info.nif");
    }

    /**
     * Is the game played left-handed.
     */
    bool UIUtils::isLeftHandedMode()
    {
        return f4vr::isLeftHandedMode();
    }

    /**
     * Trigger haptic on the offhand to indicate interaction with the VR UI.
     */
    void UIUtils::triggerInteractionHeptic()
    {
        vrcf::VRHaptics.trigger(vrcf::Hand::Offhand, vrcf::HapticPattern::Click);
    }

    /**
     * Update the node flags to show/hide it.
     */
    void UIUtils::setNodeVisibility(RE::NiNode* node, const bool visible, const float originalScale)
    {
        node->local.scale = visible ? originalScale : 0;

        // TODO: try to understand why it's not working for our nifs.
        //if (visible)
        //	node->flags &= 0xfffffffffffffffe; // show
        //else
        //	node->flags |= 0x1; // hide
        //RE::NiAVObject::NiUpdateData* ud = nullptr;
        //node->UpdateWorldData(ud);
    }

    /**
     * Get a RE::NiNode that can be used in game UI for the given .nif file, together with the
     * element's size (in framework units) read from the nif root-node name. The quad geometry
     * is centred on the origin, so this size is symmetric about the element's centre.
     */
    std::tuple<RE::NiNode*, UISize> UIUtils::getUINodeFromNifFile(const std::string& path)
    {
        const auto nifNode = f4vr::getClonedNiNodeForNifFile(path);
        UISize size = extractElementSizeFromName(nifNode->name.c_str());
        if (size.width < 0 || size.height < 0) {
            logger::warn("UI node nif doesn't contain size data! (Nif: {})", path.c_str());
            size = UISize(2.0f, 2.0f); // sane fallback so layout and bounds stay usable
        }
        nifNode->name = RE::BSFixedString(std::filesystem::path(path).stem().string());
        return { nifNode, size };
    }

    /**
     * Find node by name in the given subtree of the given root node.
     */
    RE::NiNode* UIUtils::findNode(RE::NiNode* node, const char* nodeName)
    {
        return f4vr::findNode(node, nodeName);
    }
}
