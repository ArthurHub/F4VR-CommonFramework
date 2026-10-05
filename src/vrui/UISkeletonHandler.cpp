#include "UISkeletonHandler.h"

#include "UIUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRSkelly.h"
#include "f4vr/PlayerNodes.h"

using namespace common;

namespace f4cf::vrui
{
    namespace
    {
        // The hand bone is this close to the offhand controller only when an arm is drawn there, as FRIK does
        constexpr float HAND_TO_WAND_MAX_DISTANCE = 15.0f;

        // From the forearm bone at the wrist joint to a UI lying on the inner wrist, found in the game:
        // the offset along the bone's axes, whose x runs along the forearm toward the hand, and the turn,
        // as degrees around x, y and z. Power armor has a thicker arm and another forearm bone.
        // The values are the left arm's; the right arm's are their mirror.
        const RE::NiTransform WRIST_UI_TRANSFORM = MatrixUtils::getTransform(-2.0f, 1.5f, 4.5f, 130.0f, 0.0f, 0.0f);
        const RE::NiTransform WRIST_UI_TRANSFORM_POWER_ARMOR = MatrixUtils::getTransform(0.0f, 4.5f, 7.5f, 130.0f, 0.0f, 0.0f);

        /**
         * The left arm's transform as the right arm needs it. The right forearm bone is the left one's mirror
         * across its own z, so the offset's z is reversed, and the UI is turned half way around the bone's y
         * to face out of the wrist and read left to right again.
         */
        RE::NiTransform toRightArm(RE::NiTransform transform)
        {
            transform.translate.z = -transform.translate.z;
            for (int row = 0; row < 3; ++row) {
                transform.rotate.entry[row][0] = -transform.rotate.entry[row][0];
                transform.rotate.entry[row][2] = -transform.rotate.entry[row][2];
            }
            return transform;
        }

        // Where the UI sits by the offhand controller while no arm is drawn there, from the controller's UI node,
        // found in the game. The values are the left hand's; the right hand's are their mirror, left to right.
        const RE::NiTransform WAND_UI_TRANSFORM = MatrixUtils::getTransform(1.0f, -9.0f, -8.0f, 20.0f, 0.0f, -90.0f);

        RE::NiTransform mirrorLeftToRight(RE::NiTransform transform)
        {
            transform.translate.x = -transform.translate.x;
            transform.rotate.entry[0][1] = -transform.rotate.entry[0][1];
            transform.rotate.entry[0][2] = -transform.rotate.entry[0][2];
            transform.rotate.entry[1][0] = -transform.rotate.entry[1][0];
            transform.rotate.entry[2][0] = -transform.rotate.entry[2][0];
            return transform;
        }

        const RE::NiTransform RIGHT_WAND_UI_TRANSFORM = mirrorLeftToRight(WAND_UI_TRANSFORM);

        const RE::NiTransform RIGHT_WRIST_UI_TRANSFORM = toRightArm(WRIST_UI_TRANSFORM);
        const RE::NiTransform RIGHT_WRIST_UI_TRANSFORM_POWER_ARMOR = toRightArm(WRIST_UI_TRANSFORM_POWER_ARMOR);

        RE::NiTransform identityTransform()
        {
            RE::NiTransform transform;
            transform.translate = RE::NiPoint3(0, 0, 0);
            transform.rotate = MatrixUtils::getIdentityMatrix();
            transform.scale = 1.0f;
            return transform;
        }
    }

    void UISkeletonHandler::addWristElement(const std::shared_ptr<UIElement>& element)
    {
        _wristElements.push_back(element);
    }

    void UISkeletonHandler::removeWristElement(const std::shared_ptr<UIElement>& element)
    {
        if (std::erase(_wristElements, element) > 0) {
            element->setBaseTransform(identityTransform());
        }
        if (_wristElements.empty()) {
            // nothing follows the arm any more, so the skeleton is not kept alive past its replacement
            _skeletonRoot.reset();
            _offhandForearmBone.reset();
            _offhandHandBone.reset();
        }
    }

    /**
     * Give every wrist root its base for this frame: the move from the controller's node onto the forearm while
     * the hand bone of the offhand arm is at the offhand controller, nothing otherwise.
     */
    void UISkeletonHandler::onFrameUpdate()
    {
        if (_wristElements.empty()) {
            return;
        }

        const bool leftHanded = UIUtils::isLeftHandedMode();
        const auto attachNode = UIUtils::getOffhandWandAttachNode();

        const bool armAtController = findOffhandArmBones() && MatrixUtils::vec3Len(_offhandHandBone->world.translate - attachNode->world.translate) < HAND_TO_WAND_MAX_DISTANCE;
        const auto wrist = armAtController ? getWristWorldTransform(!leftHanded) : std::nullopt;
        const RE::NiTransform base = wrist ? MatrixUtils::worldToLocalTransform(attachNode->world, *wrist) : leftHanded ? RIGHT_WAND_UI_TRANSFORM : WAND_UI_TRANSFORM;

        for (const auto& element : _wristElements) {
            element->setBaseTransform(base);
        }
    }

    /**
     * Find the forearm and hand bones of the offhand arm by name: the left arm, or the right one in left-handed mode.
     * They are kept until the skeleton is replaced, on a save load or on entering or leaving power armor, or
     * the handedness changes.
     * The forearm bone is the third one, next to the wrist, which turns with the forearm as the hand rolls.
     * Power armor has no third, so there it is the first.
     * @return false if the skeleton or either bone is missing
     */
    bool UISkeletonHandler::findOffhandArmBones()
    {
        const auto skeletonRoot = f4vr::getRootNode();
        const bool leftHanded = UIUtils::isLeftHandedMode();
        if (skeletonRoot != _skeletonRoot.get() || leftHanded != _bonesLeftHanded) {
            _skeletonRoot.reset(skeletonRoot);
            _bonesLeftHanded = leftHanded;
            // the skeleton is replaced on entering and leaving power armor, so this is read with the bones
            _inPowerArmor = f4vr::isInPowerArmor();
            auto forearm = f4vr::findNode(skeletonRoot, (leftHanded ? f4vr::SkellyBones::RArm_ForeArm3 : f4vr::SkellyBones::LArm_ForeArm3).data());
            if (!forearm) {
                forearm = f4vr::findNode(skeletonRoot, (leftHanded ? f4vr::SkellyBones::RArm_ForeArm1 : f4vr::SkellyBones::LArm_ForeArm1).data());
            }
            _offhandForearmBone.reset(forearm);
            _offhandHandBone.reset(f4vr::findNode(skeletonRoot, (leftHanded ? f4vr::SkellyBones::RArm_Hand : f4vr::SkellyBones::LArm_Hand).data()));
        }
        return _offhandForearmBone && _offhandHandBone;
    }

    /**
     * Where a UI lies on the wrist, as a world transform: at the wrist joint, which is where the hand bone starts
     * and does not move when the wrist bends, turned as the forearm bone is and then by the fixed turn from the
     * bone's axes to the UI's.
     */
    std::optional<RE::NiTransform> UISkeletonHandler::getWristWorldTransform(const bool leftArm) const
    {
        RE::NiTransform wrist;
        wrist.translate = _offhandHandBone->world.translate;
        wrist.rotate = _offhandForearmBone->world.rotate;
        wrist.scale = 1.0f;
        if (leftArm) {
            return MatrixUtils::localToWorldTransform(wrist, _inPowerArmor ? WRIST_UI_TRANSFORM_POWER_ARMOR : WRIST_UI_TRANSFORM);
        }
        return MatrixUtils::localToWorldTransform(wrist, _inPowerArmor ? RIGHT_WRIST_UI_TRANSFORM_POWER_ARMOR : RIGHT_WRIST_UI_TRANSFORM);
    }
}
