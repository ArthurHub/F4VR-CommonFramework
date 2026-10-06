#pragma once

#include "UIContainer.h"
#include "UIDevLayout.h"
#include "UISkeletonHandler.h"
#include "UIUtils.h"
#include "vrcf/VRControllersManager.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace f4cf::vrui
{
    /**
     * How smoothly a root that stays in the world follows the hand that moves it, unless the mod sets another
     * time: see UIManager::setWorldMoveSmoothing.
     */
    inline constexpr float WORLD_MOVE_SMOOTH_SECONDS = 0.05f;

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
        void setWorldMoveButton(std::optional<vr::EVRButtonId> button);
        void setWorldMoveSmoothing(float seconds);

    private:
        /**
         * A hand that holds a root in the world and moves it, see holdWorldElement.
         */
        struct WorldHold
        {
            bool primaryHand = true;

            // the point the root is held at, from the hand's wand and from the root's place in the world
            RE::NiPoint3 wandPoint;
            RE::NiPoint3 worldPoint;

            // how the root's place is turned from the wand
            RE::NiMatrix3 rotation;
        };

        /**
         * What is kept of a hand's move button from one frame to the next, see updateWorldMove.
         */
        struct WorldMoveHand
        {
            // the button was down in the frame before
            bool down = false;

            // the button is down from a press that took hold of a root in the world: the hand's wand is
            // hidden from the game until the button is released
            bool moving = false;
        };

        /**
         * A root that stays where it was put in the world, see attachPresetToWorldAtHMD.
         */
        struct WorldElement
        {
            std::shared_ptr<UIElement> element;

            // the root's place in the world: the world transform its attach node had when the root was put there,
            // and from then on where a hand moved it
            RE::NiTransform world;

            // the root is put where the HMD is again when the player is further than this from it. 0 for never.
            float recenterDistance = 0.0f;

            // the hand that holds the root and moves it, while one does
            std::optional<WorldHold> hold;
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
        void updateWorldMove(const UIFrameUpdateContext& context);
        std::vector<WorldElement>::iterator findWorldElement(const UIElement* element);
        void holdWorldElement(WorldElement& worldElement, bool primaryHand, const RE::NiPoint3& position) const;
        static void letGoWorldElement(WorldElement& worldElement, std::string_view reason);
        static float distanceFromPlayer(const WorldElement& worldElement);
        void moveHeldWorldElement(RE::NiTransform& world, const WorldHold& hold, float deltaSeconds) const;
        void dumpUITree() const;
        static void dumpUITreeRecursive(UIElement* element, std::string padding);

        std::vector<std::shared_ptr<UIElement>> _rootElements;

        // used to release child elements in a safe way (on the next frame update)
        std::vector<std::shared_ptr<UIElement>> _releaseSafeList;

        // reads the player's skeleton: the fingertip that presses the UI, and the place of the roots attached with the wrist preset
        UISkeletonHandler _skeletonHandler;

        // the roots that stay in the world, which get their base transform every frame and are recentered when left
        std::vector<WorldElement> _worldElements;

        // when the roots that stay in the world were last updated
        std::chrono::steady_clock::time_point _worldElementsUpdateTime;

        // the button a hand moves the roots that stay in the world with, nothing for roots that are not moved
        std::optional<vr::EVRButtonId> _worldMoveButton;

        // the time such a root takes to come about two thirds of the way to where the hand that moves it has it
        float _worldMoveSmoothSeconds = WORLD_MOVE_SMOOTH_SECONDS;

        WorldMoveHand _primaryMoveHand;
        WorldMoveHand _offhandMoveHand;

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
