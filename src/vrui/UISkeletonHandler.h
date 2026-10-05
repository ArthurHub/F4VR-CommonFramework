#pragma once

#include <memory>
#include <optional>
#include <vector>

namespace f4cf::vrui
{
    class UIElement;

    /**
     * The fingertip a UI is pressed with, and the hand it belongs to.
     */
    struct UIInteractionFinger
    {
        RE::NiPoint3 position;
        bool primaryHand;
    };

    /**
     * What the UI reads from the player's skeleton. The bones are found by name, and again when the skeleton
     * is replaced.
     *
     * The first thing read is the tip of the index finger that presses the UI, so a mod does not have to supply it.
     *
     * The second is the inner wrist of the offhand arm, the work behind UIManager's wrist preset.
     * A root stays attached to the node of the offhand controller. Every frame it is given a base transform
     * that moves it from that node onto the forearm, and its own position and rotation apply from there.
     * That needs an arm drawn at the controller, as FRIK does: while the hand bone of the offhand arm is not at
     * the controller, the base is a fixed place by the controller.
     * Only the forearm decides the placement, so bending the wrist does not move the root.
     * Nothing is attached to the skeleton, so a replaced skeleton only means finding the bones again.
     */
    class UISkeletonHandler
    {
    public:
        void addWristElement(const std::shared_ptr<UIElement>& element);
        void removeWristElement(const std::shared_ptr<UIElement>& element);
        void onFrameUpdate();
        UIInteractionFinger getInteractionFingerTip(const RE::NiNode* attachNode, const RE::NiPoint3& uiWorldPosition, std::optional<bool> keepPrimaryHand);
        void hideFingerTipMarkers();

    private:
        // The small sphere that marks where a hand presses the UI
        struct FingerTipMarker
        {
            RE::NiPointer<RE::NiNode> node;
            bool loadTried = false;
            // placed in this frame, so still needed
            bool placed = false;
        };

        RE::NiPoint3 getIndexFingerTipWorldPosition(bool primaryHand);
        static void placeFingerTipMarker(FingerTipMarker& marker, RE::NiNode* attachNode, const RE::NiPoint3& position);
        static void hideFingerTipMarker(FingerTipMarker& marker);

        bool findOffhandArmBones();
        std::optional<RE::NiTransform> getWristWorldTransform(bool leftArm) const;

        std::vector<std::shared_ptr<UIElement>> _wristElements;

        // The offhand arm's bones, and the skeleton and handedness they were found for
        RE::NiPointer<RE::NiNode> _skeletonRoot;
        RE::NiPointer<RE::NiNode> _offhandForearmBone;
        RE::NiPointer<RE::NiNode> _offhandHandBone;
        bool _bonesLeftHanded = false;
        bool _inPowerArmor = false;

        FingerTipMarker _primaryFingerTipMarker;
        FingerTipMarker _offhandFingerTipMarker;

        // the fingertips found in this frame
        std::optional<RE::NiPoint3> _primaryFingerTip;
        std::optional<RE::NiPoint3> _offhandFingerTip;
    };
}
