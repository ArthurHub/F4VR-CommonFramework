#include "ImGuiPointer.h"

#include <cfloat>

#include <imgui.h>

#include "../common/MatrixUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../vrcf/VRControllersManager.h"

namespace f4cf::imgui
{
    namespace
    {
        /**
         * The left hand's offset from the right hand's: the same, mirrored left to right.
         */
        RE::NiTransform mirrorLeftToRight(RE::NiTransform transform)
        {
            transform.translate.x = -transform.translate.x;
            transform.rotate.entry[0][1] = -transform.rotate.entry[0][1];
            transform.rotate.entry[0][2] = -transform.rotate.entry[0][2];
            transform.rotate.entry[1][0] = -transform.rotate.entry[1][0];
            transform.rotate.entry[2][0] = -transform.rotate.entry[2][0];
            return transform;
        }
    }

    /**
     * The default offset was found in the game: the ray starts a little in front of the wand, and points 18
     * degrees above the way the hand aims a weapon and 5 degrees inward, which is where the hand points.
     */
    Pointer::Pointer()
        : _offset(common::MatrixUtils::getTransform(-2.0f, 3.0f, -1.0f, -18.0f, 0.0f, -5.0f))
    {}

    /**
     * Function-local static: a mod may ask for the pointer from a static initializer, before this translation
     * unit's own statics are constructed.
     */
    Pointer& Pointer::get()
    {
        static Pointer instance;
        return instance;
    }

    /**
     * Which hands can point at interactive canvases. Both by default.
     */
    void Pointer::setHands(const PointerHands hands)
    {
        _hands = hands;
    }

    /**
     * Where a hand's ray is, from the UI node of its wand (primaryUIAttachNode, secondaryUIOffsetNode): the
     * ray starts at the offset's position and runs along its +Y. It replaces the default. An offset of
     * nothing starts the ray at the wand and points it the way the hand aims a weapon.
     * The offset is given for the right hand and mirrored for the left hand: its x position, and its turns
     * around y and z. Its scale is not used.
     */
    void Pointer::setOffset(const RE::NiTransform& offset)
    {
        _offset = offset;
        _offset.scale = 1.0f;
    }

    /**
     * Give ImGui the pointer for the frame that is about to begin: where the pointing hand's ray meets an
     * interactive canvas, as ImGui's mouse position, and that hand's trigger as the left mouse button.
     * Called before ImGui::NewFrame(), so the widgets of this frame see it.
     * With no target nothing is read from the game.
     */
    void Pointer::update(const std::vector<internal::PointerTarget>& targets)
    {
        _state = {};
        if (targets.empty()) {
            release();
            return;
        }

        const HandPointer primary = pointHand(true, targets);
        const HandPointer offhand = pointHand(false, targets);
        const internal::PointerOwner owner =
            _ownership.update({ .onCanvas = primary.target != nullptr, .down = primary.sample.down }, { .onCanvas = offhand.target != nullptr, .down = offhand.sample.down });
        if (owner == internal::PointerOwner::None) {
            release();
            return;
        }

        const HandPointer& hand = owner == internal::PointerOwner::Primary ? primary : offhand;
        const float x = hand.hit.u * static_cast<float>(hand.target->canvas->pixelWidth());
        const float y = hand.hit.v * static_cast<float>(hand.target->canvas->pixelHeight());

        // the position before the button, so a press lands where the pointer is now
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(hand.target->displayX + x, hand.target->displayY + y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, hand.sample.down);
        _given = true;

        _state.canvas = hand.target->canvas;
        _state.x = x;
        _state.y = y;
        _state.primaryHand = owner == internal::PointerOwner::Primary;
        _state.down = hand.sample.down;
        _state.rayOrigin = hand.sample.origin;
        _state.hitPosition = hand.sample.origin + hand.sample.direction * hand.hit.distance;
    }

    /**
     * Whether ImGui uses the pointer in the frame that has just begun.
     */
    void Pointer::setWanted(const bool wanted)
    {
        _state.wantsPointer = wanted;
    }

    /**
     * No ImGui frame is built in this frame, so the pointer is on nothing.
     */
    void Pointer::clearState()
    {
        _state = {};
    }

    /**
     * A canvas is destroyed: the state must not keep pointing to it.
     */
    void Pointer::onCanvasRemoved(const Canvas* canvas)
    {
        if (_state.canvas == canvas) {
            _state = {};
        }
    }

    /**
     * A hand's pointer from its wand: the ray from the wand's UI node, moved and turned by the offset, and
     * the wand's trigger.
     * The UI node is at the wand's node, turned the way the hand aims a weapon: the node a weapon hangs on
     * and the game's own aim node of the offhand are turned the same. The wand's node itself points 59
     * degrees above that.
     */
    internal::PointerSample Pointer::sampleWand(const bool primaryHand) const
    {
        const auto* nodes = f4vr::getVRPlayerNodes();
        const auto* wand = nodes ? (primaryHand ? nodes->primaryUIAttachNode : nodes->secondaryUIOffsetNode) : nullptr;
        if (!wand) {
            return {};
        }
        const bool rightHand = primaryHand != f4vr::isLeftHandedMode();
        const RE::NiTransform ray = common::MatrixUtils::localToWorldTransform(wand->world, rightHand ? _offset : mirrorLeftToRight(_offset));
        return {
            .valid = true,
            .origin = ray.translate,
            .direction = ray.rotate.Transpose() * RE::NiPoint3(0.0f, 1.0f, 0.0f), // the codebase's local->world convention
            .down = vrcf::VRControllers.isPressHeldDown(primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand, vr::k_EButton_SteamVR_Trigger),
        };
    }

    /**
     * A hand's pointer and the nearest of the targets its ray is on. A hand that may not point has neither.
     */
    Pointer::HandPointer Pointer::pointHand(const bool primaryHand, const std::vector<internal::PointerTarget>& targets) const
    {
        HandPointer hand;
        if (_hands != PointerHands::Both && (_hands == PointerHands::Primary) != primaryHand) {
            return hand;
        }
        hand.sample = sampleWand(primaryHand);
        if (!hand.sample.valid) {
            return hand;
        }
        for (const auto& target : targets) {
            const auto hit = internal::intersectQuadFront(hand.sample.origin, hand.sample.direction, target.topLeft, target.topRight, target.bottomLeft);
            if (hit && (!hand.target || hit->distance < hand.hit.distance)) {
                hand.target = &target;
                hand.hit = *hit;
            }
        }
        return hand;
    }

    /**
     * Tell ImGui the pointer is gone, so nothing stays hovered or pressed. Once, until it is given a pointer
     * again.
     */
    void Pointer::release()
    {
        if (!_given) {
            return;
        }
        _given = false;
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    }
}
