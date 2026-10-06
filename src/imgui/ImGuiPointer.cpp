#include "ImGuiPointer.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>

#include <imgui.h>

#include "../common/MatrixUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../render/PrimitiveDrawRenderer.h"
#include "../vrcf/VRControllersManager.h"

namespace f4cf::imgui
{
    namespace
    {
        // the mark is a disc of this many triangles
        constexpr int MARK_SEGMENTS = 20;

        // the distance from the head at which the mark has the radius of the style
        constexpr float MARK_SIZE_DISTANCE = 100.0f;

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
     * Called before ImGui::NewFrame(), so the widgets of this frame see it. The pointer is drawn from here too.
     * With no target nothing is read from the game.
     */
    void Pointer::update(const std::vector<internal::PointerTarget>& targets)
    {
        _state = {};
        if (targets.empty()) {
            release();
            draw(nullptr);
            return;
        }

        const HandPointer primary = pointHand(true, targets);
        const HandPointer offhand = pointHand(false, targets);
        const internal::PointerOwner owner =
            _ownership.update({ .onCanvas = primary.target != nullptr, .down = primary.sample.down }, { .onCanvas = offhand.target != nullptr, .down = offhand.sample.down });
        if (owner == internal::PointerOwner::None) {
            release();
            draw(nullptr);
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
        draw(&hand);
    }

    /**
     * Whether ImGui uses the pointer in the frame that has just begun.
     */
    void Pointer::setWanted(const bool wanted)
    {
        _state.wantsPointer = wanted;
    }

    /**
     * No ImGui frame is built in this frame, so the pointer is on nothing and is not drawn.
     */
    void Pointer::clearState()
    {
        _state = {};
        draw(nullptr);
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
     * A hand's pointer from its wand: the ray from the wand's UI node, moved and turned by the style's
     * offset, and the wand's trigger.
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
        const RE::NiTransform ray = common::MatrixUtils::localToWorldTransform(wand->world, rightHand ? _style.rayOffset : mirrorLeftToRight(_style.rayOffset));
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

    /**
     * Draw the pointer of the given hand, whose ray is on a canvas: the ray and the mark of the style. With no
     * hand, or a style that is not drawn, the layer is given nothing, once.
     *
     * The ray is a ribbon turned to face the head, since a line is drawn one pixel wide whatever is asked. It
     * runs from the ray's start for the style's longest length, or up to the mark when the canvas is nearer.
     * It fades in and out over the style's length at its two ends, or over half of the ray when it is shorter
     * than both. Each fade is one piece, clear at the ray's end and the ray's color at its other side.
     *
     * The mark is a disc that lies on the canvas, with its border as a ring around it. The size of both is
     * given at a set distance from the head and grows with the distance, so the mark looks the same size
     * wherever the canvas is.
     */
    void Pointer::draw(const HandPointer* hand)
    {
        const auto* nodes = hand && _style.drawn ? f4vr::getVRPlayerNodes() : nullptr;
        if (!nodes || !nodes->hmdNode) {
            // the layer is touched only to take away what it draws, so it is never built if it never draws
            if (_drawn) {
                _drawn = false;
                pointerLayer().publish({});
            }
            return;
        }
        const RE::NiPoint3 head = nodes->hmdNode->world.translate;
        const RE::NiPoint3& origin = hand->sample.origin;
        const RE::NiPoint3& direction = hand->sample.direction;
        render::PrimitiveDraw frame;

        // the ribbon's width runs across both the ray and the line of sight to it; a ray that points
        // straight from the head has no such direction, and shows as its mark alone
        const float length = (std::min)(hand->hit.distance, _style.rayMaxLength);
        RE::NiPoint3 across;
        if (length > 0.0f && _style.rayWidth > 0.0f && _style.rayColor.a > 0.0f &&
            common::MatrixUtils::tryVec3Norm(common::MatrixUtils::vec3Cross(direction, origin - head), across)) {
            const RE::NiPoint3 half = across * (_style.rayWidth * 0.5f);
            const render::Color& solid = _style.rayColor;
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

        if (_style.markSize > 0.0f) {
            const RE::NiPoint3 center = origin + direction * hand->hit.distance;
            const float scale = common::MatrixUtils::vec3Len(center - head) / MARK_SIZE_DISTANCE;
            const float radius = _style.markSize * scale;
            const float inside = radius - std::clamp(_style.markBorderWidth * scale, 0.0f, radius);

            // the directions from the center to the points around the mark, the last one the first again
            const RE::NiPoint3 right = common::MatrixUtils::vec3Norm(hand->target->topRight - hand->target->topLeft);
            const RE::NiPoint3 down = common::MatrixUtils::vec3Norm(hand->target->bottomLeft - hand->target->topLeft);
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
                    frame.addQuad(center + around[i] * inside,
                        center + around[i] * radius,
                        center + around[i + 1] * radius,
                        center + around[i + 1] * inside,
                        _style.markBorderColor);
                }
            }
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
}
