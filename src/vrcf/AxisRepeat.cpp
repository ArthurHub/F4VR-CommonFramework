#include "AxisRepeat.h"

#include <algorithm>
#include <cmath>

#include "VRControllersManager.h"

namespace f4cf::vrcf
{
    /**
     * The one way an axis is pushed past `threshold`: along its dominant component, so every caller of the axis
     * reads a diagonal push the same way. Empty when the dominant component is within the threshold.
     */
    std::optional<Direction> AxisRepeat::pushedDirection(const float x, const float y, const float threshold)
    {
        if (std::abs(x) >= std::abs(y)) {
            if (std::abs(x) > threshold) {
                return x > 0 ? Direction::Right : Direction::Left;
            }
        } else if (std::abs(y) > threshold) {
            return y > 0 ? Direction::Up : Direction::Down;
        }
        return std::nullopt;
    }

    /**
     * Whether a caller asking for `accepted` takes a push `pushed`: the direction itself, or any of the ones Any,
     * UpDown and RightLeft stand for.
     */
    bool AxisRepeat::accepts(const Direction accepted, const Direction pushed)
    {
        switch (accepted) {
        case Direction::Any:
            return true;
        case Direction::UpDown:
            return pushed == Direction::Up || pushed == Direction::Down;
        case Direction::RightLeft:
            return pushed == Direction::Right || pushed == Direction::Left;
        default:
            return accepted == pushed;
        }
    }

    /**
     * The direction of a step of the axis at (x, y), for a caller that takes `accepted`, or empty when this check is
     * not one. A fresh push steps at once; held, it repeats first after max(cooldown, INITIAL_DELAY), then every
     * cooldown, halved after FASTER_AFTER and quartered after FASTEST_AFTER of holding (unless not `accelerate`).
     * The hold ends below RELEASE_SHARE of the threshold, on a push another way, or after UNCHECKED_RELEASE with no
     * check. The state is the axis's, shared by its callers: a step a caller does not accept is left for another.
     */
    std::optional<Direction> AxisRepeat::step(const float x, const float y, const float now, const float threshold, const float cooldown, const bool accelerate,
        const Direction accepted)
    {
        if (!_lastCheck || now - *_lastCheck > UNCHECKED_RELEASE) {
            _holding = false;
        }
        _lastCheck = now;

        const auto pushed = pushedDirection(x, y, threshold);
        if (_holding && (along(x, y) < RELEASE_SHARE * threshold || (pushed && *pushed != _direction))) {
            _holding = false;
        }
        if (!_holding) {
            if (!pushed) {
                return std::nullopt;
            }
            _holding = true;
            _direction = *pushed;
            _holdStart = now;
            _steps = 0;
        }

        if (_steps > 0 && now - _lastStep < interval(now, cooldown, accelerate)) {
            return std::nullopt;
        }
        if (!accepts(accepted, _direction)) {
            return std::nullopt;
        }
        _lastStep = now;
        _steps = std::min(_steps + 1, 2);
        return _direction;
    }

    /**
     * Forget the hold, as after the controller was lost: the next push is a fresh one.
     */
    void AxisRepeat::reset()
    {
        *this = AxisRepeat{};
    }

    /**
     * How far the axis is pushed the way the hold is.
     */
    float AxisRepeat::along(const float x, const float y) const
    {
        switch (_direction) {
        case Direction::Right:
            return x;
        case Direction::Left:
            return -x;
        case Direction::Up:
            return y;
        default:
            return -y;
        }
    }

    /**
     * The wait after the hold's last step before the next one, by how long it has been held.
     */
    float AxisRepeat::interval(const float now, const float cooldown, const bool accelerate) const
    {
        if (_steps == 1) {
            return std::max(cooldown, INITIAL_DELAY);
        }
        if (!accelerate) {
            return cooldown;
        }
        const float held = now - _holdStart;
        if (held >= FASTEST_AFTER) {
            return cooldown / 4.0f;
        }
        if (held >= FASTER_AFTER) {
            return cooldown / 2.0f;
        }
        return cooldown;
    }
}
