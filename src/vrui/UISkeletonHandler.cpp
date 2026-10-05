#include "UISkeletonHandler.h"

#include "ModBase.h"
#include "UIElement.h"
#include "UIUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRSkelly.h"
#include "f4vr/PlayerNodes.h"
#include "f4vr/SphereStyle.h"

using namespace common;

namespace f4cf::vrui
{
    namespace
    {
        // The hand bone is this close to its controller only when an arm is drawn there, as FRIK does
        constexpr float HAND_TO_WAND_MAX_DISTANCE = 15.0f;

        // How wide the sphere that marks the fingertip is. The sphere mesh is one unit wide, so this is also its scale.
        constexpr float FINGER_TIP_MARKER_WIDTH = 1.0f;
        constexpr auto FINGER_TIP_MARKER_NAME = "VRUI_FingerTipMarker";
        constexpr auto FINGER_TIP_MARKER_STYLE = "gold-medium";

        /**
         * The tip of the index finger of the primary hand or the offhand, as the player's skeleton has it.
         * @return nothing while the hand bone is not at the controller, as no hand is drawn there
         */
        std::optional<RE::NiPoint3> findIndexFingerTip(const bool primaryHand, const RE::NiPoint3& wandPosition)
        {
            const bool rightHand = primaryHand != UIUtils::isLeftHandedMode();
            const auto handBone = f4vr::Skelly::findBoneWorldTransform(std::string(rightHand ? f4vr::SkellyBones::RArm_Hand : f4vr::SkellyBones::LArm_Hand));
            if (!handBone || MatrixUtils::vec3Len(handBone->translate - wandPosition) >= HAND_TO_WAND_MAX_DISTANCE) {
                return std::nullopt;
            }
            return f4vr::Skelly::getIndexFingerTipWorldPosition(primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand);
        }

        /**
         * The hand a node moves with: true for the primary hand, false for the offhand, nothing for a node
         * that is not under either controller.
         */
        std::optional<bool> findHandOfNode(const RE::NiNode* node)
        {
            const auto nodes = f4vr::getVRPlayerNodes();
            for (; node; node = node->parent) {
                if (node == nodes->primaryWandNode || node == nodes->primaryUIAttachNode) {
                    return true;
                }
                if (node == nodes->secondaryWandNode || node == nodes->secondaryUIOffsetNode) {
                    return false;
                }
            }
            return std::nullopt;
        }

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

    /**
     * Keep a root, already attached to the offhand controller's node, on the wrist.
     */
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
     * Before that, hide the fingertip markers that were not placed in the last frame.
     * Call before the roots are laid out and drawn.
     */
    void UISkeletonHandler::onFrameUpdate()
    {
        for (const auto marker : { &_primaryFingerTipMarker, &_offhandFingerTipMarker }) {
            if (!marker->placed) {
                hideFingerTipMarker(*marker);
            }
            marker->placed = false;
        }
        _primaryFingerTip.reset();
        _offhandFingerTip.reset();

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
     * The point that presses the UI for the given hand: the tip of its index finger, or the controller while
     * no hand is drawn there, which needs a mod like FRIK. It is found once a frame, the first time it is asked for.
     * The point is marked with a small sphere while it is the controller, since there is no finger to see,
     * and always when the config asks for it: [Debug] bVRUIShowFingerTip.
     */
    RE::NiPoint3 UISkeletonHandler::getIndexFingerTipWorldPosition(const bool primaryHand)
    {
        auto& found = primaryHand ? _primaryFingerTip : _offhandFingerTip;
        if (found) {
            return *found;
        }

        const auto wandNode = primaryHand ? f4vr::getPrimaryHandWandNode() : f4vr::getOffhandWandNode();
        const auto fingerTip = findIndexFingerTip(primaryHand, wandNode->world.translate);
        const RE::NiPoint3 position = fingerTip.value_or(wandNode->world.translate);

        if (!fingerTip || g_mod->getConfig()->debug.vruiShowFingerTip) {
            // under the UI node of the hand, where the framework attaches all it draws on a hand: a mesh attached to the
            // controller node itself is drawn in an interior cell but not outside
            const auto uiNode = primaryHand ? UIUtils::getPrimaryWandAttachNode() : UIUtils::getOffhandWandAttachNode();
            placeFingerTipMarker(primaryHand ? _primaryFingerTipMarker : _offhandFingerTipMarker, uiNode, position);
        }
        found = position;
        return position;
    }

    /**
     * The fingertip to test a pressable UI against: of the two index fingers, the one nearer to the UI.
     * A UI attached to a hand is only pressed by the other hand: its own hand's finger is always near it
     * and cannot reach it.
     * A pressable that is already being pressed asks for the same hand again, so the press does not jump to
     * the other finger when that one comes nearer.
     * @param attachNode the game node the UI is attached to
     * @param uiWorldPosition where the pressable is in the world
     * @param keepPrimaryHand the hand to stay with: true for the primary hand, false for the offhand
     */
    UIInteractionFinger UISkeletonHandler::getInteractionFingerTip(const RE::NiNode* attachNode, const RE::NiPoint3& uiWorldPosition, const std::optional<bool> keepPrimaryHand)
    {
        if (const auto attachedToPrimaryHand = findHandOfNode(attachNode)) {
            const bool primaryHand = !*attachedToPrimaryHand;
            return { getIndexFingerTipWorldPosition(primaryHand), primaryHand };
        }
        if (keepPrimaryHand) {
            return { getIndexFingerTipWorldPosition(*keepPrimaryHand), *keepPrimaryHand };
        }

        const RE::NiPoint3 primary = getIndexFingerTipWorldPosition(true);
        const RE::NiPoint3 offhand = getIndexFingerTipWorldPosition(false);
        if (MatrixUtils::vec3Len(primary - uiWorldPosition) <= MatrixUtils::vec3Len(offhand - uiWorldPosition)) {
            return { primary, true };
        }
        return { offhand, false };
    }

    /**
     * Hide the fingertip markers. Call while no UI is shown, as nothing asks for a fingertip then.
     */
    void UISkeletonHandler::hideFingerTipMarkers()
    {
        hideFingerTipMarker(_primaryFingerTipMarker);
        hideFingerTipMarker(_offhandFingerTipMarker);
    }

    /**
     * Show the marker at the given world position, as a child of the given node.
     * The sphere mesh is loaded the first time a marker is needed; if that fails it is not tried again.
     */
    void UISkeletonHandler::placeFingerTipMarker(FingerTipMarker& marker, RE::NiNode* attachNode, const RE::NiPoint3& position)
    {
        if (!marker.node && !marker.loadTried) {
            marker.loadTried = true;
            const auto style = f4vr::findSphereStylePreset(FINGER_TIP_MARKER_STYLE).value_or(f4vr::getDefaultSphereStyle());
            try {
                marker.node.reset(f4vr::getClonedNiNodeForNifFileSetName(style.nifOrDefault(), FINGER_TIP_MARKER_NAME));
            } catch (const std::exception& ex) {
                logger::error("UI fingertip marker: failed to load sphere NIF '{}': {}", style.nifOrDefault(), ex.what());
            }
            if (marker.node) {
                marker.node->collisionObject.reset();
                f4vr::applySphereStyle(marker.node.get(), style);
            }
        }
        if (!marker.node) {
            return;
        }

        if (marker.node->parent != attachNode) {
            attachNode->AttachChild(marker.node.get(), true);
        }

        RE::NiTransform world;
        world.translate = position;
        world.rotate = MatrixUtils::getIdentityMatrix();
        world.scale = FINGER_TIP_MARKER_WIDTH;
        marker.node->local = MatrixUtils::worldToLocalTransform(attachNode->world, world);
        // without this the marker is drawn where it was placed in the last frame
        f4vr::updateTransformsDown(marker.node.get(), true);
        marker.placed = true;
    }

    /**
     * The marker stays attached and is shrunk to nothing, as the NIF widgets are hidden.
     */
    void UISkeletonHandler::hideFingerTipMarker(FingerTipMarker& marker)
    {
        marker.placed = false;
        if (marker.node && marker.node->parent && marker.node->local.scale != 0.0f) {
            marker.node->local.scale = 0.0f;
            f4vr::updateTransformsDown(marker.node.get(), true);
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
