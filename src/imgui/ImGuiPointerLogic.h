#pragma once

#include <cstdint>
#include <optional>

// The pointer's logic, apart from the game: plain std only, so it is unit tested.
namespace f4cf::imgui::internal
{
    /**
     * Where a ray meets a quad: as fractions of the quad's width and height from its top left corner, and
     * how far along the ray, in lengths of the ray's direction.
     */
    struct QuadHit
    {
        float u = 0.0f;
        float v = 0.0f;
        float distance = 0.0f;
    };

    /**
     * Where a ray meets the plane of a quad from the quad's front, if it does.
     * The quad is a rectangle given by three of its corners, as seen from its front. The place is given as
     * on the quad, and is under 0 or over 1 where the ray passes beside the quad. A ray that comes from
     * behind the plane, runs along it or starts past it does not meet it.
     * Vec is any type with x, y and z.
     */
    template <class Vec>
    std::optional<QuadHit> intersectPlaneFront(const Vec& origin, const Vec& direction, const Vec& topLeft, const Vec& topRight, const Vec& bottomLeft)
    {
        struct Vec3
        {
            float x;
            float y;
            float z;
        };

        const auto subtract = [](const auto& a, const auto& b) {
            return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
        };
        const auto dot = [](const auto& a, const auto& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        };

        const Vec3 right = subtract(topRight, topLeft);
        const Vec3 down = subtract(bottomLeft, topLeft);
        // right x down: the way the quad faces away from its front
        const Vec3 forward{ right.y * down.z - right.z * down.y, right.z * down.x - right.x * down.z, right.x * down.y - right.y * down.x };

        const float approach = dot(direction, forward);
        if (!(approach > 0.0f)) {
            return std::nullopt;
        }
        const float distance = dot(subtract(topLeft, origin), forward) / approach;
        if (!(distance > 0.0f)) {
            return std::nullopt;
        }

        const Vec3 fromTopLeft{ origin.x + direction.x * distance - topLeft.x, origin.y + direction.y * distance - topLeft.y, origin.z + direction.z * distance - topLeft.z };
        return QuadHit{ .u = dot(fromTopLeft, right) / dot(right, right), .v = dot(fromTopLeft, down) / dot(down, down), .distance = distance };
    }

    /**
     * Where a ray meets the front of a quad, if it does: on the quad's plane, and not beside the quad.
     * The quad and the ray are given as for intersectPlaneFront.
     */
    template <class Vec>
    std::optional<QuadHit> intersectQuadFront(const Vec& origin, const Vec& direction, const Vec& topLeft, const Vec& topRight, const Vec& bottomLeft)
    {
        const auto hit = intersectPlaneFront(origin, direction, topLeft, topRight, bottomLeft);
        // negated so a NaN is rejected as well
        if (!hit || !(hit->u >= 0.0f && hit->u <= 1.0f && hit->v >= 0.0f && hit->v <= 1.0f)) {
            return std::nullopt;
        }
        return hit;
    }

    /**
     * What one hand does in a frame.
     */
    struct PointerHandInput
    {
        // its ray is on an interactive canvas
        bool onCanvas = false;

        // its trigger is down
        bool down = false;
    };

    /**
     * Whether a hand operates the UI, and whether its trigger presses it. One for each hand.
     * A hand operates the UI while its ray is on a canvas, and a pull of its trigger there is a press.
     * A press is the UI's until the trigger is released, wherever the ray is by then. Without that, a ray
     * that slides off the canvas in the middle of a press gives the game a trigger that is already down,
     * and the UI never gets the release.
     * A trigger that is already down when the ray comes to a canvas is the game's until it is released, and
     * the hand does not operate the UI before that: the pull makes no press on arrival, and is not taken
     * from the game in its middle.
     */
    class PointerHandLatch
    {
    public:
        void update(const PointerHandInput& hand)
        {
            if (_state == State::Pressing && hand.down) {
                return;
            }
            if (!hand.onCanvas) {
                _state = State::Away;
            } else if (_state == State::Over || _state == State::Pressing) {
                // a press gets here when it is released
                _state = hand.down ? State::Pressing : State::Over;
            } else {
                _state = hand.down ? State::Waiting : State::Over;
            }
        }

        /**
         * The hand is the UI's, and not the game's: its ray is on a canvas, or it holds a press.
         */
        bool operates() const
        {
            return _state == State::Over || _state == State::Pressing;
        }

        /**
         * Its trigger presses the UI.
         */
        bool pressing() const
        {
            return _state == State::Pressing;
        }

    private:
        enum class State : std::uint8_t
        {
            // the ray is on no canvas
            Away,

            // the ray came to a canvas with the trigger down, which is the game's until it is released
            Waiting,

            // the ray is on a canvas
            Over,

            // the trigger was pulled on a canvas and has not been released since
            Pressing,
        };

        State _state = State::Away;
    };

    enum class PointerOwner : std::uint8_t
    {
        None,
        Primary,
        Offhand,
    };

    /**
     * Which hand owns the pointer. ImGui has one pointer, so one hand owns it at a time.
     * A hand whose ray is alone on a canvas owns it. With both rays on a canvas it is the hand that pressed
     * its trigger on a canvas last, and the primary hand before either has.
     * The owner keeps the pointer while it holds its trigger down on a canvas, so a press of the other hand
     * does not take a drag over.
     * The hands are given as the UI has them (PointerHandLatch): on a canvas only while the hand operates
     * the UI, and down only for a press that began on a canvas.
     */
    class PointerOwnership
    {
    public:
        PointerOwner update(const PointerHandInput& primary, const PointerHandInput& offhand)
        {
            const PointerHandInput& owner = _owner == PointerOwner::Offhand ? offhand : primary;
            const bool ownerHolds = _owner != PointerOwner::None && owner.onCanvas && owner.down;
            if (!ownerHolds) {
                const bool primaryPressed = primary.onCanvas && primary.down && !_primaryWasDown;
                const bool offhandPressed = offhand.onCanvas && offhand.down && !_offhandWasDown;
                if (primaryPressed) {
                    _primaryPressedLast = true;
                } else if (offhandPressed) {
                    _primaryPressedLast = false;
                }

                if (primary.onCanvas && offhand.onCanvas) {
                    _owner = _primaryPressedLast ? PointerOwner::Primary : PointerOwner::Offhand;
                } else if (primary.onCanvas) {
                    _owner = PointerOwner::Primary;
                } else if (offhand.onCanvas) {
                    _owner = PointerOwner::Offhand;
                } else {
                    _owner = PointerOwner::None;
                }
            }
            _primaryWasDown = primary.down;
            _offhandWasDown = offhand.down;
            return _owner;
        }

    private:
        PointerOwner _owner = PointerOwner::None;
        bool _primaryPressedLast = true;
        bool _primaryWasDown = false;
        bool _offhandWasDown = false;
    };
}
