#pragma once

#include "UIContainer.h"
#include "UISkeletonHandler.h"
#include "UIUtils.h"

#include <functional>
#include <vector>

namespace f4cf::vrui
{
    class UIManager
    {
    public:
        using HandPointingHandler = std::function<void(bool primaryHand, bool toPoint)>;

        void onFrameStart();
        void onFrameUpdate();
        void setHandPointingHandler(HandPointingHandler handler);
        void attachElement(const std::shared_ptr<UIElement>& element, RE::NiNode* attachNode);
        void detachElement(const std::shared_ptr<UIElement>& element, bool releaseSafe);

        void attachPresetToPrimaryWandTop(const std::shared_ptr<UIElement>& element, RE::NiPoint3 offset);
        void attachPresetToPrimaryWandLeft(const std::shared_ptr<UIElement>& element, RE::NiPoint3 offset);
        void attachPresetToOffhandWandTop(const std::shared_ptr<UIElement>& element, RE::NiPoint3 offset);
        void attachPresetToOffhandWandRight(const std::shared_ptr<UIElement>& element, RE::NiPoint3 offset);
        void attachPresetToOffhandWrist(const std::shared_ptr<UIElement>& element, const RE::NiTransform& offset);
        void attachPresetToHMDBottom(const std::shared_ptr<UIElement>& element);
        void enableDevLayoutViaConfig() const;

    private:
        // Used by an element to find the fingertip that presses it, see UIElement::getInteractionFingerTip
        friend class UIElement;

        UIInteractionFinger getInteractionFingerTip(const RE::NiNode* attachNode, const RE::NiPoint3& worldPosition, const std::optional<bool> keepPrimaryHand)
        {
            return _skeletonHandler.getInteractionFingerTip(attachNode, worldPosition, keepPrimaryHand);
        }

        void updateHandPointing(bool primaryHand, const std::optional<bool>& isPressableClose);
        void setHandPointing(bool primaryHand, bool toPoint) const;
        void readDevLayoutFromConfig() const;
        void dumpUITree() const;
        static void dumpUITreeRecursive(UIElement* element, std::string padding);

        std::vector<std::shared_ptr<UIElement>> _rootElements;

        // used to release child elements in a safe way (on the next frame update)
        std::vector<std::shared_ptr<UIElement>> _releaseSafeList;

        // reads the player's skeleton: the fingertip that presses the UI, and the place of the roots attached with the wrist preset
        UISkeletonHandler _skeletonHandler;

        // the hands pointed from here, to release one whose finger is no longer tested
        bool _primaryHandPointing = false;
        bool _offhandPointing = false;

        // the frame update ran in this frame, so another call in the same frame does nothing
        bool _frameUpdated = false;

        // points a hand and releases it in place of the framework's own way, which goes through FRIK's API
        HandPointingHandler _handPointingHandler;
    };

    // Not a fan of globals but it may be easiest to refactor code right now
    extern UIManager* g_uiManager;

    inline void initUIManager()
    {
        if (g_uiManager) {
            throw std::exception("UI manager already initialized");
        }
        g_uiManager = new UIManager();
    }
}
