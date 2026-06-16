#pragma once

#include "UIElement.h"

namespace f4cf::vrui
{
    struct UIUtils
    {
        static std::string getDebugSphereNifName();
        static std::string getEmptyButtonFrameNifName();
        static std::string getToggleButtonFrameNifName();
        static std::string getDisabledOverlayNifName();

        static RE::NiNode* getPrimaryWandAttachNode();
        static RE::NiNode* getHMDAttachNode();
        static bool isLeftHandedMode();
        static void triggerInteractionHeptic();
        static void setNodeVisibility(RE::NiNode* node, bool visible, float originalScale);
        static std::tuple<RE::NiNode*, UISize> getUINodeFromNifFile(const std::string& path);
        static RE::NiNode* findNode(RE::NiNode* node, const char* nodeName);
    };
}
