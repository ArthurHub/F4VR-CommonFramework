#include "ImGuiPointer.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <string_view>

#include <imgui.h>

#include "../common/MatrixUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../render/PrimitiveDrawRenderer.h"
#include "../vrcf/VRControllersManager.h"
#include "../vrcf/VRControllersSuppressor.h"

namespace f4cf::imgui
{
    namespace
    {
        // the owner the wands are hidden from the game under (vrcf::VRControllersSuppress)
        constexpr std::string_view SUPPRESS_KEY = "ImGuiPointer";

        // A press ends when its trigger is back under this, of 1 for a full pull. The game takes a pull from
        // how far the trigger is pulled too, at a threshold of its own, so the wand is given back to it
        // only with the trigger near its rest.
        constexpr float TRIGGER_REST = 0.2f;

        // how far past the edges of its canvas a press follows the ray, in sizes of the canvas
        constexpr float PRESS_REACH = 1.0f;

        // the thumbstick scrolls from this far off its center, of 1 for a full push
        constexpr float SCROLL_DEAD_ZONE = 0.2f;

        // the lines of text ImGui scrolls for one step of the mouse wheel
        constexpr float LINES_PER_WHEEL_STEP = 5.0f;

        // the mark is a disc of this many triangles
        constexpr int MARK_SEGMENTS = 20;

        // the part of the ray's opacity that the ray of the hand that does not own the pointer has
        constexpr float OTHER_RAY_OPACITY = 0.3f;

        /**
         * The layer the pointer is drawn in: over the panels it points at, and not hidden by the world.
         * Function-local static, so a mod with no interactive canvas never builds it.
         */
        render::PrimitiveDrawRenderer& pointerLayer()
        {
            static render::PrimitiveDrawRenderer instance("ImGuiPointer", render::DRAW_ORDER_POINTERS, false);
            return instance;
        }

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
     * Where the ray is on the hand and how the pointer is drawn, all of it at once: start from style() to
     * change one part.
     */
    void Pointer::setStyle(const PointerStyle& style)
    {
        _style = style;
    }

    /**
     * Give ImGui the pointer for the frame that is about to begin: where the pointing hand's ray meets an
     * interactive canvas, as ImGui's mouse position, and that hand's trigger as the left mouse button.
     * Called before ImGui::NewFrame(), so the widgets of this frame see it, and with no target in a frame
     * that builds no ImGui frame. The pointer is drawn from here too, and the wand of a hand that operates
     * the UI is hidden from the game.
     * With no target and no press held, nothing is read from the game.
     */
    void Pointer::update(const std::vector<internal::PointerTarget>& targets)
    {
        _state = {};

        HandPointer primary;
        HandPointer offhand;
        if (!targets.empty() || _primaryHold.latch.pressing() || _offhandHold.latch.pressing()) {
            primary = pointHand(true, targets);
            offhand = pointHand(false, targets);
        } else {
            _primaryHold = {};
            _offhandHold = {};
        }
        suppressWands(_primaryHold.latch.operates(), _offhandHold.latch.operates());

        const internal::PointerOwner owner =
            _ownership.update({ .onCanvas = primary.target != nullptr, .down = primary.pressed }, { .onCanvas = offhand.target != nullptr, .down = offhand.pressed });
        if (owner == internal::PointerOwner::None) {
            release();
            draw(nullptr, nullptr);
            return;
        }

        // a press follows the ray past the edges of its canvas, but not without end
        const HandPointer& hand = owner == internal::PointerOwner::Primary ? primary : offhand;
        const float x = std::clamp(hand.hit.u, -PRESS_REACH, 1.0f + PRESS_REACH) * static_cast<float>(hand.target->canvas->pixelWidth());
        const float y = std::clamp(hand.hit.v, -PRESS_REACH, 1.0f + PRESS_REACH) * static_cast<float>(hand.target->canvas->pixelHeight());

        // the position before the button, so a press lands where the pointer is now
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(hand.target->displayX + x, hand.target->displayY + y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, hand.pressed);
        if (const float wheel = wheelFromThumbstick(owner == internal::PointerOwner::Primary, io.DeltaTime); wheel != 0.0f) {
            io.AddMouseWheelEvent(0.0f, wheel);
        }
        _given = true;

        _state.canvas = hand.target->canvas;
        _state.x = x;
        _state.y = y;
        _state.primaryHand = owner == internal::PointerOwner::Primary;
        _state.down = hand.pressed;
        _state.rayOrigin = hand.sample.origin;
        _state.hitPosition = hand.sample.origin + hand.sample.direction * hand.hit.distance;

        // the other hand has a ray to draw while it is on a canvas too
        const HandPointer& other = owner == internal::PointerOwner::Primary ? offhand : primary;
        draw(&hand, other.target ? &other : nullptr);
    }

    /**
     * Whether ImGui uses the pointer in the frame that has just begun.
     */
    void Pointer::setWanted(const bool wanted)
    {
        _state.wantsPointer = wanted;
    }

    /**
     * A canvas is destroyed: nothing must keep pointing to it. A press held on it has no canvas to come
     * back to, also when another canvas is created at its address.
     */
    void Pointer::onCanvasRemoved(const Canvas* canvas)
    {
        if (_state.canvas == canvas) {
            _state = {};
        }
        if (_primaryHold.canvas == canvas) {
            _primaryHold.canvas = nullptr;
        }
        if (_offhandHold.canvas == canvas) {
            _offhandHold.canvas = nullptr;
        }
    }

    /**
     * A hand's pointer from its wand: the ray from the wand's UI node, moved and turned by the style's
     * offset, and the wand's trigger.
     * The UI node is at the wand's node, turned the way the hand aims a weapon: the node a weapon hangs on
     * and the game's own aim node of the offhand are turned the same. The wand's node itself points 59
     * degrees above that.
     * The trigger is down from its click. For a hand that presses the UI it stays down until it is back
     * near its rest, which is later than the end of the click.
     */
    internal::PointerSample Pointer::sampleWand(const bool primaryHand, const bool pressing) const
    {
        const auto* nodes = f4vr::getVRPlayerNodes();
        const auto* wand = nodes ? (primaryHand ? nodes->primaryUIAttachNode : nodes->secondaryUIOffsetNode) : nullptr;
        if (!wand) {
            return {};
        }
        const bool rightHand = primaryHand != f4vr::isLeftHandedMode();
        const RE::NiTransform ray = common::MatrixUtils::localToWorldTransform(wand->world, rightHand ? _style.rayOffset : mirrorLeftToRight(_style.rayOffset));
        const vrcf::Hand hand = primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand;
        return {
            .valid = true,
            .origin = ray.translate,
            .direction = ray.rotate.Transpose() * RE::NiPoint3(0.0f, 1.0f, 0.0f), // the codebase's local->world convention
            .down = vrcf::VRControllers.isPressHeldDown(hand, vr::k_EButton_SteamVR_Trigger) ||
                    (pressing && vrcf::VRControllers.getAxisValue(hand, vrcf::Axis::Trigger).x > TRIGGER_REST),
        };
    }

    /**
     * A hand in this frame: its pointer, the target it is on, and whether it presses it.
     * The hand is on the nearest of the targets its ray is on. While it holds a press it stays on the canvas
     * the press began on, wherever its ray meets that canvas's surface: its plane, or the cylinder of a curved
     * canvas. When that canvas is no longer a target, or the ray no longer meets its surface, the press has no
     * canvas to come back to: the hand is on no target, so the UI gets the release, and it still operates the
     * UI until its trigger is released.
     * A hand that does not operate the UI is on no target, and a hand that may not point has no pointer.
     */
    Pointer::HandPointer Pointer::pointHand(const bool primaryHand, const std::vector<internal::PointerTarget>& targets)
    {
        HandHold& hold = primaryHand ? _primaryHold : _offhandHold;
        const bool wasPressing = hold.latch.pressing();

        HandPointer hand;
        if (_hands == PointerHands::Both || (_hands == PointerHands::Primary) == primaryHand) {
            hand.sample = sampleWand(primaryHand, wasPressing);
        }

        if (wasPressing) {
            const auto target = std::ranges::find(targets, hold.canvas, &internal::PointerTarget::canvas);
            const auto hit =
                target != targets.end() && hand.sample.valid
                    ? internal::intersectSurfaceFront(hand.sample.origin, hand.sample.direction, target->topLeft, target->topRight, target->bottomLeft, target->curveRadius)
                    : std::nullopt;
            if (hit) {
                hand.target = &*target;
                hand.hit = *hit;
            } else {
                hold.canvas = nullptr;
            }
        } else if (hand.sample.valid) {
            for (const auto& target : targets) {
                const auto hit = internal::intersectQuadFront(hand.sample.origin, hand.sample.direction, target.topLeft, target.topRight, target.bottomLeft, target.curveRadius);
                if (hit && (!hand.target || hit->distance < hand.hit.distance)) {
                    hand.target = &target;
                    hand.hit = *hit;
                }
            }
        }

        hold.latch.update({ .onCanvas = hand.target != nullptr, .down = hand.sample.down });
        if (!hold.latch.operates()) {
            hand.target = nullptr;
        } else if (hold.latch.pressing()) {
            // a press begins on a target
            if (!wasPressing) {
                hold.canvas = hand.target->canvas;
            }
            hand.pressed = hand.target != nullptr;
        }
        return hand;
    }

    /**
     * Hide from the game the whole wand of each hand that operates the UI: every button and every axis, so
     * nothing done on a canvas reaches the game or another mod. The mod's own reads still see the wand.
     * It is set in every frame a hand operates the UI: that costs a lookup while nothing changes, and
     * brings the suppression back after a session load has reset it. With no hand it is released, once.
     */
    void Pointer::suppressWands(const bool primary, const bool offhand)
    {
        if (!primary && !offhand) {
            if (_suppressing) {
                _suppressing = false;
                vrcf::VRControllersSuppress.release(SUPPRESS_KEY);
            }
            return;
        }
        _suppressing = true;
        vrcf::VRControllersSuppress.setAllSuppressed(SUPPRESS_KEY, vrcf::Hand::Primary, primary);
        vrcf::VRControllersSuppress.setAllSuppressed(SUPPRESS_KEY, vrcf::Hand::Offhand, offhand);
    }

    /**
     * ImGui's mouse wheel for this frame, from the thumbstick of the hand that owns the pointer: pushed up it
     * scrolls up, as a wheel turned away does. The speed follows how far the thumbstick is pushed past a
     * dead zone around its center, up to the style's speed at a full push.
     * The game does not get the thumbstick: the wand of the hand that points is hidden from it.
     */
    float Pointer::wheelFromThumbstick(const bool primaryHand, const float deltaSeconds) const
    {
        if (_style.scrollSpeed <= 0.0f) {
            return 0.0f;
        }
        const float push = vrcf::VRControllers.getThumbstickValue(primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand).y;
        return internal::scrollFromThumbstick(push, SCROLL_DEAD_ZONE) * _style.scrollSpeed / LINES_PER_WHEEL_STEP * deltaSeconds;
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

    /**
     * Draw the pointer: the ray and the mark of the hand that owns it, and the ray of the other hand while
     * that one is on a canvas too. With no owner, or a style that is not drawn, the layer is given nothing,
     * once.
     * The other hand's ray is fainter and has no mark: it shows that the hand points at the UI, and that the
     * pointer is not its own.
     */
    void Pointer::draw(const HandPointer* owner, const HandPointer* other)
    {
        const auto* nodes = owner && _style.drawn ? f4vr::getVRPlayerNodes() : nullptr;
        if (!nodes || !nodes->hmdNode) {
            // the layer is touched only to take away what it draws, so it is never built if it never draws
            if (_drawn) {
                _drawn = false;
                pointerLayer().publish({});
            }
            return;
        }
        const RE::NiPoint3 head = nodes->hmdNode->world.translate;
        render::PrimitiveDraw frame;
        addRay(frame, head, *owner, 1.0f);
        addMark(frame, *owner);
        if (other) {
            addRay(frame, head, *other, OTHER_RAY_OPACITY);
        }

        if (frame.empty() && !_drawn) {
            return;
        }
        _drawn = !frame.empty();
        if (_drawn) {
            pointerLayer().ensureInstalled();
        }
        pointerLayer().publish(std::move(frame));
    }

    /**
     * Add the ray of a hand that is on a canvas, at the given part of the style's opacity.
     * The ray is a ribbon turned to face the head, since a line is drawn one pixel wide whatever is asked. It
     * runs from the ray's start for the style's longest length, or up to the canvas when that is nearer.
     * It fades in and out over the style's length at its two ends, or over half of the ray when it is shorter
     * than both. Each fade is one piece, clear at the ray's end and the ray's color at its other side.
     */
    void Pointer::addRay(render::PrimitiveDraw& frame, const RE::NiPoint3& head, const HandPointer& hand, const float opacity) const
    {
        const RE::NiPoint3& origin = hand.sample.origin;
        const RE::NiPoint3& direction = hand.sample.direction;

        // the ribbon's width runs across both the ray and the line of sight to it; a ray that points
        // straight from the head has no such direction, and is not drawn
        const float length = (std::min)(hand.hit.distance, _style.rayMaxLength);
        RE::NiPoint3 across;
        if (!(length > 0.0f && _style.rayWidth > 0.0f && _style.rayColor.a > 0.0f) ||
            !common::MatrixUtils::tryVec3Norm(common::MatrixUtils::vec3Cross(direction, origin - head), across)) {
            return;
        }
        const RE::NiPoint3 half = across * (_style.rayWidth * 0.5f);
        const render::Color solid{ _style.rayColor.r, _style.rayColor.g, _style.rayColor.b, _style.rayColor.a * opacity };
        const render::Color clear{ solid.r, solid.g, solid.b, 0.0f };
        const auto addPiece = [&](const float from, const float to, const render::Color& fromColor, const render::Color& toColor) {
            const RE::NiPoint3 start = origin + direction * from;
            const RE::NiPoint3 end = origin + direction * to;
            frame.addQuad(start - half, start + half, end + half, end - half, fromColor, fromColor, toColor, toColor);
        };

        const float fade = std::clamp(_style.rayFade, 0.0f, length * 0.5f);
        if (fade > 0.0f) {
            addPiece(0.0f, fade, clear, solid);
            addPiece(length - fade, length, solid, clear);
        }
        if (length > fade * 2.0f) {
            addPiece(fade, length - fade, solid, solid);
        }
    }

    /**
     * Add the mark of a hand, where its ray meets its canvas: a disc that lies on the canvas, with its border
     * as a ring around it. On a curved canvas it is flat, and lies on the canvas where the ray meets it.
     * Its size is the style's, in the world, whatever the distance to it. So it keeps its size on the canvas
     * as the player comes nearer or steps back.
     */
    void Pointer::addMark(render::PrimitiveDraw& frame, const HandPointer& hand) const
    {
        const float radius = _style.markSize;
        if (!(radius > 0.0f)) {
            return;
        }
        const RE::NiPoint3 center = hand.sample.origin + hand.sample.direction * hand.hit.distance;
        const float inside = radius - std::clamp(_style.markBorderWidth, 0.0f, radius);

        // the directions from the center to the points around the mark, the last one the first again
        const internal::PointerTarget& target = *hand.target;
        const RE::NiPoint3 right = target.curveRadius > 0.0f ? internal::CurvedQuad(target.topLeft, target.topRight, target.bottomLeft, target.curveRadius).rightAt(hand.hit.u)
                                                             : common::MatrixUtils::vec3Norm(target.topRight - target.topLeft);
        const RE::NiPoint3 down = common::MatrixUtils::vec3Norm(target.bottomLeft - target.topLeft);
        std::array<RE::NiPoint3, MARK_SEGMENTS + 1> around;
        for (int i = 0; i <= MARK_SEGMENTS; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i % MARK_SEGMENTS) / static_cast<float>(MARK_SEGMENTS);
            around[i] = right * std::cos(angle) + down * std::sin(angle);
        }

        // the border is beside the disc, not over it, so its opacity is its own
        if (inside > 0.0f && _style.markColor.a > 0.0f) {
            for (int i = 0; i < MARK_SEGMENTS; ++i) {
                frame.addTriangle(center, center + around[i] * inside, center + around[i + 1] * inside, _style.markColor);
            }
        }
        if (inside < radius && _style.markBorderColor.a > 0.0f) {
            for (int i = 0; i < MARK_SEGMENTS; ++i) {
                frame.addQuad(center + around[i] * inside, center + around[i] * radius, center + around[i + 1] * radius, center + around[i + 1] * inside, _style.markBorderColor);
            }
        }
    }
}
