#include "PlayerRotation.h"

#include <algorithm>
#include <cmath>

namespace f4cf::f4vr
{
    namespace
    {
        constexpr float DEG_TO_RAD = 0.017453292f;
        constexpr float TWO_PI = 6.2831855f;

        // A smoothed snap is done once this little of it is left, the same epsilon the engine's applier uses.
        constexpr float SNAP_ARRIVED_EPSILON = 0.0001f;

        // Ceiling on a frame delta, so a hitch or a loading screen doesn't spin the player.
        constexpr float MAX_FRAME_DELTA_SEC = 0.1f;

        /**
         * A float VR setting's value, 0 off VR.
         */
        float settingFloat(const RE::Setting* setting)
        {
            return setting ? setting->GetFloat() : 0.0f;
        }
    }

    VRRotationType PlayerRotation::getRotationType()
    {
        const auto type = static_cast<int>(RE::VRSettings::GetRotationType());
        return type >= static_cast<int>(VRRotationType::None) && type <= static_cast<int>(VRRotationType::Smooth) ? static_cast<VRRotationType>(type) : VRRotationType::None;
    }

    float PlayerRotation::getSnapAngleDegrees()
    {
        return settingFloat(RE::VRSettings::RotationAngle());
    }

    float PlayerRotation::getSmoothSpeedDegreesPerSec()
    {
        return settingFloat(RE::VRSettings::RotationSpeed());
    }

    float PlayerRotation::getSnapSmoothingSpeedDegreesPerSec()
    {
        return settingFloat(RE::VRSettings::AngleSnapSmoothingSpeed());
    }

    /**
     * Turn from the thumbstick the way the vanilla handler does: past the threshold is one snap per flick for
     * the snap styles (the latch is only released by letting the stick return), or a continuous turn for the
     * smooth style. A smoothed snap already running is advanced first, so this is the only per-frame call.
     */
    void PlayerRotation::turnByThumbstick(const float axisX, const float threshold)
    {
        // Two clocks: this one times the turn from the previous stick reading, leaving the snap interpolation
        // its own, which onFrameUpdate() shares. One clock between them would hand each caller only the slice
        // of the frame since the other last ran, and a smooth turn driven alongside onFrameUpdate() would come
        // out slower than the vanilla one by however much of the frame sat between the two calls.
        const auto deltaSeconds = elapsedSeconds(_lastTurnTime);
        stepPendingSnap(elapsedSeconds(_lastSnapStepTime));

        const auto type = getRotationType();
        if (type == VRRotationType::None) {
            return;
        }

        if (std::abs(axisX) < threshold) {
            _turnLatched = false;
            return;
        }

        const auto right = axisX > 0;
        if (type == VRRotationType::Smooth) {
            smoothTurn(right, deltaSeconds);
            return;
        }

        if (!_turnLatched) {
            _turnLatched = true;
            snapTurn(right);
        }
    }

    void PlayerRotation::onFrameUpdate()
    {
        stepPendingSnap(elapsedSeconds(_lastSnapStepTime));
    }

    /**
     * Queue (or apply) one snap of the configured angle. The smoothed style adds onto whatever a snap still in
     * flight owes, so flicking again mid-turn stacks the way the engine's moving target does.
     */
    void PlayerRotation::snapTurn(const bool right)
    {
        const auto angle = getSnapAngleDegrees() * DEG_TO_RAD * (right ? 1.0f : -1.0f);
        if (getRotationType() == VRRotationType::SnapInstant) {
            rotateBy(angle);
        } else {
            _pendingSnap += angle;
        }
    }

    // Same as the engine's own: fRotationSpeed:VR, converted to radians, times the frame time - read off the
    // smooth branch of the vanilla turn worker, which multiplies by the same constant 0.017453292.
    void PlayerRotation::smoothTurn(const bool right, const float deltaSeconds)
    {
        rotateBy(getSmoothSpeedDegreesPerSec() * DEG_TO_RAD * deltaSeconds * (right ? 1.0f : -1.0f));
    }

    float PlayerRotation::getYaw()
    {
        const auto vrWorldData = RE::VRWorldData::GetSingleton();
        return vrWorldData ? vrWorldData->GetYaw() : 0;
    }

    /**
     * Write the VR world yaw, normalized into [0, 2pi) as the engine's own rotation code keeps it so a long
     * session of turning one way can't drift the value off into imprecision.
     */
    void PlayerRotation::setYaw(const float yawRadians)
    {
        const auto vrWorldData = RE::VRWorldData::GetSingleton();
        if (!vrWorldData) {
            return;
        }
        auto yaw = std::fmod(yawRadians, TWO_PI);
        if (yaw < 0) {
            yaw += TWO_PI;
        }
        vrWorldData->SetYaw(yaw);
    }

    void PlayerRotation::rotateBy(const float deltaRadians)
    {
        if (deltaRadians != 0) {
            setYaw(getYaw() + deltaRadians);
        }
    }

    bool PlayerRotation::isSnapInProgress()
    {
        return _pendingSnap != 0;
    }

    void PlayerRotation::cancel()
    {
        _pendingSnap = 0;
        _turnLatched = false;
    }

    /**
     * Move a smoothed snap along by one frame's worth of fAngleSnapSmoothingSpeed, never past what it owes.
     */
    void PlayerRotation::stepPendingSnap(const float deltaSeconds)
    {
        if (_pendingSnap == 0) {
            return;
        }

        const auto step = getSnapSmoothingSpeedDegreesPerSec() * DEG_TO_RAD * deltaSeconds;
        const auto applied = std::clamp(_pendingSnap, -step, step);
        _pendingSnap = std::abs(_pendingSnap - applied) < SNAP_ARRIVED_EPSILON ? 0 : _pendingSnap - applied;
        rotateBy(applied);
    }

    /**
     * Seconds since the given clock last ran, re-arming it. Measured here rather than taken from the engine:
     * the global the engine's own smooth turn multiplies by is unidentified, and a wrong guess would come out
     * as a wrong turn speed. The first call and any hitch yield no turning rather than a jump.
     */
    float PlayerRotation::elapsedSeconds(std::chrono::steady_clock::time_point& lastTime)
    {
        const auto now = std::chrono::steady_clock::now();
        if (lastTime == std::chrono::steady_clock::time_point{}) {
            lastTime = now;
            return 0;
        }
        const auto delta = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        return delta > MAX_FRAME_DELTA_SEC ? 0 : delta;
    }
}
