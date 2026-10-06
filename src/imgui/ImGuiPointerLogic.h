#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
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
     * A quad bent around a cylinder, which is how a curved canvas is drawn and how the pointer meets it. Both
     * take it from here, so the pointer is on the canvas where the canvas is drawn.
     * The cylinder's axis runs along the quad's height, on the side of the quad's front, at the radius from
     * the quad's middle. So the middle of the quad stays where it is, its sides come toward whoever faces it,
     * and its width is kept along the curve.
     * The quad is a rectangle given by three of its corners as it is when flat, as seen from its front. A
     * radius under the quad's width over pi is taken as that, so the quad is at most half of the cylinder.
     * Vec is any type with x, y and z.
     */
    template <class Vec>
    class CurvedQuad
    {
    public:
        CurvedQuad(const Vec& topLeft, const Vec& topRight, const Vec& bottomLeft, const float radius)
        {
            const Vec3 right = subtract(topRight, topLeft);
            const Vec3 down = subtract(bottomLeft, topLeft);
            _width = std::sqrt(dot(right, right));
            _height = std::sqrt(dot(down, down));
            // a quad with no width or height has no direction there: it is met by no ray, and all its points are finite
            _right = _width > 0.0f ? scale(right, 1.0f / _width) : Vec3{};
            _down = _height > 0.0f ? scale(down, 1.0f / _height) : Vec3{};
            // right x down: the way the quad faces away from its front
            _forward = Vec3{ _right.y * _down.z - _right.z * _down.y, _right.z * _down.x - _right.x * _down.z, _right.x * _down.y - _right.y * _down.x };
            _radius = (std::max)(radius, _width / std::numbers::pi_v<float>);

            // on the axis, level with the quad's top edge
            _axisTop =
                Vec3{ topLeft.x + right.x * 0.5f - _forward.x * _radius, topLeft.y + right.y * 0.5f - _forward.y * _radius, topLeft.z + right.z * 0.5f - _forward.z * _radius };
        }

        /**
         * The angle the quad takes up around the axis, in radians.
         */
        float arc() const
        {
            return _width / _radius;
        }

        /**
         * The point at the given fractions of the quad's width and height from its top left corner.
         */
        Vec pointAt(const float u, const float v) const
        {
            const float angle = (u - 0.5f) * arc();
            const float across = std::sin(angle) * _radius;
            const float out = std::cos(angle) * _radius;
            const float along = v * _height;
            return Vec{ _axisTop.x + _right.x * across + _forward.x * out + _down.x * along,
                _axisTop.y + _right.y * across + _forward.y * out + _down.y * along,
                _axisTop.z + _right.z * across + _forward.z * out + _down.z * along };
        }

        /**
         * The way the quad's width runs at the given fraction of its width, of length 1: the quad's right
         * there, which turns along the curve.
         */
        Vec rightAt(const float u) const
        {
            const float angle = (u - 0.5f) * arc();
            const float across = std::cos(angle);
            const float out = -std::sin(angle);
            return Vec{ _right.x * across + _forward.x * out, _right.y * across + _forward.y * out, _right.z * across + _forward.z * out };
        }

        /**
         * Where a ray meets the cylinder from the quad's front, which is the cylinder's inside, if it does.
         * The place is given as on the quad, measured along the curve, and is under 0 or over 1 where the ray
         * passes beside the quad. A ray that starts inside the cylinder meets it unless it runs along the
         * axis. A ray from outside meets the inside of the far part, and passes through the near part, which
         * it comes to from behind.
         */
        std::optional<QuadHit> intersectFront(const Vec& origin, const Vec& direction) const
        {
            if (!(_width > 0.0f && _height > 0.0f)) {
                return std::nullopt;
            }
            const Vec3 fromAxis = subtract(origin, _axisTop);
            const float originAcross = dot(fromAxis, _right);
            const float originOut = dot(fromAxis, _forward);
            const float directionAcross = dot(direction, _right);
            const float directionOut = dot(direction, _forward);

            // seen along the axis the cylinder is a circle, and the ray meets it where it is at the radius from the axis
            const float a = directionAcross * directionAcross + directionOut * directionOut;
            const float halfB = originAcross * directionAcross + originOut * directionOut;
            const float c = originAcross * originAcross + originOut * originOut - _radius * _radius;
            const float discriminant = halfB * halfB - a * c;
            // negated so a NaN is rejected as well
            if (!(a > 0.0f) || !(discriminant >= 0.0f)) {
                return std::nullopt;
            }

            // the further of the two places is where the ray leaves the cylinder, so there it meets the inside
            const float distance = (-halfB + std::sqrt(discriminant)) / a;
            if (!(distance > 0.0f)) {
                return std::nullopt;
            }

            const float angle = std::atan2(originAcross + directionAcross * distance, originOut + directionOut * distance);
            const float along = dot(fromAxis, _down) + dot(direction, _down) * distance;
            return QuadHit{ .u = 0.5f + angle / arc(), .v = along / _height, .distance = distance };
        }

    private:
        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        template <class B>
        static Vec3 subtract(const Vec& a, const B& b)
        {
            return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
        }

        static Vec3 scale(const Vec3& a, const float factor)
        {
            return Vec3{ a.x * factor, a.y * factor, a.z * factor };
        }

        template <class A>
        static float dot(const A& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        // the quad's own directions, of length 1: along its width in its middle, along its height, and away from its front
        Vec3 _right;
        Vec3 _down;
        Vec3 _forward;

        Vec3 _axisTop;
        float _width = 0.0f;
        float _height = 0.0f;
        float _radius = 0.0f;
    };

    /**
     * Where a ray meets the surface of a canvas from its front, if it does: the plane of its quad, or for a
     * canvas with a curve radius the cylinder its quad is bent around (CurvedQuad).
     * The quad and the ray are given as for intersectPlaneFront, and so is the place: on the quad, and under
     * 0 or over 1 where the ray passes beside it.
     */
    template <class Vec>
    std::optional<QuadHit> intersectSurfaceFront(const Vec& origin, const Vec& direction, const Vec& topLeft, const Vec& topRight, const Vec& bottomLeft, const float curveRadius)
    {
        if (curveRadius > 0.0f) {
            return CurvedQuad(topLeft, topRight, bottomLeft, curveRadius).intersectFront(origin, direction);
        }
        return intersectPlaneFront(origin, direction, topLeft, topRight, bottomLeft);
    }

    /**
     * Where a ray meets the front of a quad, if it does: on the quad's surface, and not beside the quad.
     * The surface is the quad's plane, or with a curve radius the cylinder the quad is bent around.
     * The quad and the ray are given as for intersectPlaneFront.
     */
    template <class Vec>
    std::optional<QuadHit> intersectQuadFront(const Vec& origin, const Vec& direction, const Vec& topLeft, const Vec& topRight, const Vec& bottomLeft,
        const float curveRadius = 0.0f)
    {
        const auto hit = intersectSurfaceFront(origin, direction, topLeft, topRight, bottomLeft, curveRadius);
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

    /**
     * How much a thumbstick scrolls, from -1 to 1, for how far it is pushed along one axis, from -1 to 1.
     * Nothing inside the dead zone around its center, and from there evenly up to 1 at a full push, so the
     * scrolling starts slow at the edge of the dead zone instead of at a jump.
     */
    inline float scrollFromThumbstick(const float push, const float deadZone)
    {
        const float past = (push < 0.0f ? -push : push) - deadZone;
        // negated so a NaN scrolls nothing
        if (!(past > 0.0f) || !(deadZone < 1.0f)) {
            return 0.0f;
        }
        const float amount = past < 1.0f - deadZone ? past / (1.0f - deadZone) : 1.0f;
        return push < 0.0f ? -amount : amount;
    }

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
