#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "UIElement.h"

namespace f4cf::vrui
{
    /**
     * What the UI reads from the player's skeleton. The bones are found by name, and again when the skeleton
     * is replaced.
     *
     * Today that is the inner wrist of the offhand arm, the work behind UIManager's wrist preset.
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
        /**
         * Keep a root, already attached to the offhand controller's node, on the wrist.
         */
        void addWristElement(const std::shared_ptr<UIElement>& element);
        void removeWristElement(const std::shared_ptr<UIElement>& element);

        /**
         * Place every wrist root for this frame. Call before the roots are laid out and drawn.
         */
        void onFrameUpdate();

    private:
        bool findOffhandArmBones();
        std::optional<RE::NiTransform> getWristWorldTransform(bool leftArm) const;

        std::vector<std::shared_ptr<UIElement>> _wristElements;

        // The offhand arm's bones, and the skeleton and handedness they were found for
        RE::NiPointer<RE::NiNode> _skeletonRoot;
        RE::NiPointer<RE::NiNode> _offhandForearmBone;
        RE::NiPointer<RE::NiNode> _offhandHandBone;
        bool _bonesLeftHanded = false;
        bool _inPowerArmor = false;
    };
}
