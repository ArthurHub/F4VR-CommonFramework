#pragma once

#include "UIElement.h"

namespace f4cf::vrui
{
    struct UIUtils
    {
        static RE::NiNode* getPrimaryWandAttachNode();
        static RE::NiNode* getOffhandWandAttachNode();
        static RE::NiNode* getHMDAttachNode();
        static bool isLeftHandedMode();
        static void triggerInteractionHeptic(bool primaryHand);
        static RE::NiNode* findNode(RE::NiNode* node, const char* nodeName);
    };
}
