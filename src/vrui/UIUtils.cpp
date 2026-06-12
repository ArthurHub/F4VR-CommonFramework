#include "UIUtils.h"

#include "ModBase.h"
#include "f4vr/PlayerNodes.h"
#include "vrcf/VRControllersHaptic.h"

namespace
{
    /**
     * Extract the float value from the nif node name in this format: "VRUI (W/H:4.5)"
     */
    float extractWidthToHeightValueFronName(const std::string& nifName)
    {
        const auto pos = nifName.find(':');
        if (pos == std::string::npos)
            return -1.0f;

        const auto end = nifName.find(')', pos);
        if (end == std::string::npos)
            return -1.0f;

        const auto num = nifName.substr(pos + 1, end - pos - 1);
        return std::stof(num);
    }
}

namespace f4cf::vrui
{
    std::string UIUtils::getDebugSphereNifName()
    {
        return g_mod->getName() + "\\1x1Sphere.nif";
    }

    std::string UIUtils::getEmptyButtonFrameNifName()
    {
        return g_mod->getName() + "\\UI_Common\\btn_empty.nif";
    }

    std::string UIUtils::getToggleButtonFrameNifName()
    {
        return g_mod->getName() + "\\UI_Common\\btn_border.nif";
    }

    /**
     * Get the size of a UI element based on its width-to-height ratio.
     * The way VR UI nifs are defined is that the height is always 2.0f units and the width is
     * the only one that changes if the shape is not square.
     * The reason height is 2.0f and not 1.0f is to easily make the center of the UI element at (0,0,0)
     */
    UISize UIUtils::getElementSize(const float widthToHeightRatio)
    {
        return UISize(2.0f * widthToHeightRatio, 2.0f);
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
     * Get a RE::NiNode that can be used in game UI for the given .nif file.
     * Why is just loading not enough?
     */
    std::tuple<RE::NiNode*, float> UIUtils::getUINodeFromNifFile(const std::string& path)
    {
        auto& normPath = path._Starts_with("Data") ? path : "Data/Meshes/" + path;
        const auto nifNode = f4vr::getClonedNiNodeForNifFile(normPath);
        const float widthHeightRatio = extractWidthToHeightValueFronName(nifNode->name.c_str());
        if (widthHeightRatio < 0) {
            logger::warn("UI node nif doesn't contain width-to-height ratio data! (Nif: {})", path.c_str());
        }
        nifNode->name = RE::BSFixedString(std::filesystem::path(path).stem().string());
        return { nifNode, widthHeightRatio };
    }

    /**
     * Find node by name in the given subtree of the given root node.
     */
    RE::NiNode* UIUtils::findNode(RE::NiNode* node, const char* nodeName)
    {
        return f4vr::findNode(node, nodeName);
    }
}
