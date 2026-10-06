#pragma once

#include "UIContainer.h"
#include "UIDevLayout.h"
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
        void attachPresetToWorldAtHMD(const std::shared_ptr<UIElement>& element, float recenterDistance = 0.0f);
        void recenterWorldElement(const std::shared_ptr<UIElement>& element);

    private:
        /**
         * A root that stays where it was put in the world, see attachPresetToWorldAtHMD.
         */
        struct WorldElement
        {
            std::shared_ptr<UIElement> element;

            // the world transform its attach node had when the root was put there
            RE::NiTransform world;

            // the root is put where the HMD is again when the player is further than this from it. 0 for never.
            float recenterDistance = 0.0f;
        };

        // Used by an element to find the fingertip that presses it, see UIElement::getInteractionFingerTip,
        // and how the finger stands to it, see UIElement::updateFinger
        friend class UIElement;

        UIInteractionFinger getInteractionFingerTip(const RE::NiNode* attachNode, const RE::NiPoint3& worldPosition, const std::optional<bool> keepPrimaryHand)
        {
            return _skeletonHandler.getInteractionFingerTip(attachNode, worldPosition, keepPrimaryHand);
        }

        bool isHandFromBehind(const bool primaryHand) const
        {
            return primaryHand ? _primaryHandFromBehind : _offhandFromBehind;
        }

        void updateHandPointing(bool primaryHand, const std::optional<UIFingerProximity>& fingerProximity);
        void setHandPointing(bool primaryHand, bool toPoint) const;
        void updateWorldElements();
        void dumpUITree() const;
        static void dumpUITreeRecursive(UIElement* element, std::string padding);

        std::vector<std::shared_ptr<UIElement>> _rootElements;

        // used to release child elements in a safe way (on the next frame update)
        std::vector<std::shared_ptr<UIElement>> _releaseSafeList;

        // reads the player's skeleton: the fingertip that presses the UI, and the place of the roots attached with the wrist preset
        UISkeletonHandler _skeletonHandler;

        // the roots that stay in the world, which get their base transform every frame and are recentered when left
        std::vector<WorldElement> _worldElements;

        // tunes the placement of the attached roots through a file, while the config's debug flag is on
        UIDevLayout _devLayout;

        // the hands pointed from here, to release one whose finger is no longer tested
        bool _primaryHandPointing = false;
        bool _offhandPointing = false;

        // the hands that came to the UI from behind it, which do not start to point as they come out in front
        bool _primaryHandFromBehind = false;
        bool _offhandFromBehind = false;

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
