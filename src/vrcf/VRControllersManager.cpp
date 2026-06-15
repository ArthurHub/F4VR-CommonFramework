#include "VRControllersManager.h"

#include <numbers>

#include "VRControllersHaptic.h"
#include "VRControllersSuppressor.h"

#include "../../external/openvr/openvr.h"

namespace f4cf::vrcf
{
    /**
     * Update controller states; must be called each frame
     */
    void VRControllersManager::update(const bool isLeftHanded)
    {
        if (!vr::VRSystem()) {
            return;
        }

        _leftHanded = isLeftHanded;

        _left.index = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        _right.index = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

        const auto now = getCurrentTimeSeconds();

        _left.update(_left.index, now);
        _right.update(_right.index, now);

        _currentTime = now;
    }

    /**
     * Resets the controllers state to clear any tracked button presses, release, long presses, etc.
     * Useful when knowing the game is changing state that the global VR controllers manager is unaware of.
     * Examples:
     * - Closing of a message box
     * - Loading of a game
     */
    void VRControllersManager::reset()
    {
        _left.reset();
        _right.reset();
        _currentTime = getCurrentTimeSeconds();
    }

    /**
     * Sets the debounce cooldown time (in seconds) between consecutive presses
     */
    void VRControllersManager::setDebounceCooldown(const float seconds)
    {
        _debounceCooldown = seconds;
    }

    vr::VRControllerState_t VRControllersManager::getControllerState_DEPRECATED(const TrackerType a_tracker) const
    {
        return a_tracker == TrackerType::Left ? _left.current : _right.current;
    }

    /**
     * Evaluate a config-loaded InputBinding against the current controller state.
     * Dispatches to the matching check method based on binding.type. If the binding has a modifier,
     * it must be held down (on its own hand, or the binding's hand if unspecified) for the binding to
     * trigger. A `duration` of 0 falls back to a sensible per-type default. A Disabled binding always
     * returns false. Returns true when the binding's activation condition is met.
     */
    bool VRControllersManager::check(const InputBinding& binding)
    {
        if (binding.modifier) {
            const Hand modifierHand = binding.modifier->hand.value_or(binding.hand);
            if (!isPressHeldDown(modifierHand, binding.modifier->button)) {
                return false;
            }
        }

        switch (binding.type) {
        case ActivationType::Disabled:
            return false;
        case ActivationType::Touch:
            return isTouching(binding.hand, binding.button);
        case ActivationType::Press:
            return isPressed(binding.hand, binding.button);
        case ActivationType::HoldDown:
            return isPressHeldDown(binding.hand, binding.button, binding.duration);
        case ActivationType::Release:
            return isReleased(binding.hand, binding.button, binding.duration > 0.0f ? binding.duration : 99.0f);
        case ActivationType::Tap:
            return isTap(binding.hand, binding.button);
        case ActivationType::LongPress:
            return isLongPressed(binding.hand, binding.button, binding.duration > 0.0f ? binding.duration : 0.6f);
        case ActivationType::DoublePress:
            return isDoublePressed(binding.hand, binding.button, binding.duration > 0.0f ? binding.duration : 0.4f);
        case ActivationType::AxisDirection:
            return isAxisPressed(binding.hand, binding.axis, binding.direction, binding.threshold, binding.cooldown);
        default:
            return false;
        }
    }

    /**
     * Returns true if the specified button is being touched on the given hand
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    bool VRControllersManager::isTouching(const Hand hand, const vr::EVRButtonId button) const
    {
        return isTouching(getHand(hand), button);
    }

    bool VRControllersManager::isTouching(const Hand hand, const int button) const
    {
        return isTouching(getHand(hand), static_cast<vr::EVRButtonId>(button));
    }

    bool VRControllersManager::isTouching(const vr::ETrackedControllerRole hand, const vr::EVRButtonId button) const
    {
        return get(hand).isTouching(button);
    }

    /**
     * Returns true if the button was just pressed, factoring in debounce
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    bool VRControllersManager::isPressed(const Hand hand, const vr::EVRButtonId button)
    {
        return isPressed(getHand(hand), button);
    }

    bool VRControllersManager::isPressed(const Hand hand, const int button)
    {
        return isPressed(getHand(hand), static_cast<vr::EVRButtonId>(button));
    }

    bool VRControllersManager::isPressed(const vr::ETrackedControllerRole hand, const vr::EVRButtonId button)
    {
        return get(hand).justPressed(button, _currentTime, _debounceCooldown);
    }

    /**
     * Returns true if the button is currently held down
     * This will return true for EVERY frame while the button is pressed.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     * @param minHoldDurationSeconds optional: minimum duration in seconds that the button must be held down to return true.
     */
    bool VRControllersManager::isPressHeldDown(const Hand hand, const vr::EVRButtonId button, const float minHoldDurationSeconds) const
    {
        return isPressHeldDown(getHand(hand), button, minHoldDurationSeconds);
    }

    bool VRControllersManager::isPressHeldDown(const Hand hand, const int button, const float minHoldDurationSeconds) const
    {
        return isPressHeldDown(getHand(hand), static_cast<vr::EVRButtonId>(button), minHoldDurationSeconds);
    }

    bool VRControllersManager::isPressHeldDown(const vr::ETrackedControllerRole hand, const vr::EVRButtonId button, const float minHoldDurationSeconds) const
    {
        auto& state = get(hand);
        return state.isPressed(button) && state.getHeldDuration(button, _currentTime) >= minHoldDurationSeconds;
    }

    /**
     * Returns true if the specified button was just released
     * This will return true for ONE frame only when the button is first released.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     * @param maxHoldDurationSeconds optional: max duration in seconds that the button can be held before it is ignored for release.
     */
    bool VRControllersManager::isReleased(const Hand hand, const vr::EVRButtonId button, const float maxHoldDurationSeconds)
    {
        return isReleased(getHand(hand), button, maxHoldDurationSeconds);
    }

    bool VRControllersManager::isReleased(const Hand hand, const int button, const float maxHoldDurationSeconds)
    {
        return isReleased(getHand(hand), static_cast<vr::EVRButtonId>(button), maxHoldDurationSeconds);
    }

    /**
     * Returns true on a tap: the button was just released after being held for less than 0.3 seconds.
     * This will return true for ONE frame only when the button is first released.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    bool VRControllersManager::isTap(const Hand hand, const vr::EVRButtonId button)
    {
        return isReleased(getHand(hand), button, 0.3f);
    }

    bool VRControllersManager::isTap(const Hand hand, const int button)
    {
        return isReleased(getHand(hand), static_cast<vr::EVRButtonId>(button), 0.3f);
    }

    bool VRControllersManager::isReleased(const vr::ETrackedControllerRole hand, const vr::EVRButtonId button, const float maxHoldDurationSeconds)
    {
        auto& state = get(hand);
        const bool justReleased = state.justReleased(button, _currentTime, _debounceCooldown);
        return justReleased && state.getHeldDurationForRelease(button, _currentTime) < maxHoldDurationSeconds;
    }

    /**
     * Returns true if the button has been held longer than a threshold.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     * If <clear> false: will return true for EVERY frame when the button is pressed for longer then <durationSeconds>.
     * If <clear> true: will return true for ONE frame when the button is pressed for longer then <durationSeconds>.
     */
    bool VRControllersManager::isLongPressed(const Hand hand, const vr::EVRButtonId button, const float durationSeconds, const bool clear)
    {
        return isLongPressed(getHand(hand), button, durationSeconds, clear);
    }

    bool VRControllersManager::isLongPressed(const vr::ETrackedControllerRole hand, const vr::EVRButtonId button, const float durationSeconds, const bool clear)
    {
        auto& state = get(hand);
        const bool isLongPressed = state.isPressed(button) && state.getHeldDuration(button, _currentTime) >= durationSeconds;
        if (clear && isLongPressed) {
            state.clearHeldDuration(button);
        }
        return isLongPressed;
    }

    /**
     * Returns true if the button was double pressed: pressed twice with less than <maxIntervalSeconds> between the two press-downs.
     * This will return true for ONE frame only, on the second press of the pair.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    bool VRControllersManager::isDoublePressed(const Hand hand, const vr::EVRButtonId button, const float maxIntervalSeconds)
    {
        return isDoublePressed(getHand(hand), button, maxIntervalSeconds);
    }

    bool VRControllersManager::isDoublePressed(const Hand hand, const int button, const float maxIntervalSeconds)
    {
        return isDoublePressed(getHand(hand), static_cast<vr::EVRButtonId>(button), maxIntervalSeconds);
    }

    bool VRControllersManager::isDoublePressed(const vr::ETrackedControllerRole hand, const vr::EVRButtonId button, const float maxIntervalSeconds)
    {
        return get(hand).justDoublePressed(button, _currentTime, maxIntervalSeconds);
    }

    /**
     * Retrieves analog axis value for the specified controller.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    vr::VRControllerAxis_t VRControllersManager::getAxisValue(const Hand primaryHand, const Axis axis) const
    {
        return getAxisValue(getHand(primaryHand), axis);
    }

    vr::VRControllerAxis_t VRControllersManager::getAxisValue(const vr::ETrackedControllerRole hand, const Axis axis) const
    {
        return get(hand).getAxis(static_cast<uint32_t>(axis));
    }

    /**
     * Is the analog axis is pressed in the specified direction.
     * Pressed is defined as the axis value being above a certain threshold in the specified direction.
     * There is cooldown milliseconds debounce time between consecutive positive passing checks.
     */
    bool VRControllersManager::isAxisPressed(const Hand primaryHand, const Axis axis, const Direction direction, const float threshold, const float cooldown)
    {
        return isAxisPressed(getHand(primaryHand), axis, direction, threshold, cooldown);
    }

    bool VRControllersManager::isAxisPressed(const vr::ETrackedControllerRole hand, const Axis axis, const Direction direction, const float threshold, const float cooldown)
    {
        return get(hand).isAxisPressedAndClear(static_cast<uint32_t>(axis), direction, _currentTime, threshold, cooldown);
    }

    /**
     * Get the direction of analog axis if it is pressed.
     * Pressed is defined as the axis value being above a certain threshold in the specified direction.
     * There is cooldown milliseconds debounce time between consecutive positive passing checks.
     */
    std::optional<Direction> VRControllersManager::getAxisPressedDirection(const Hand primaryHand, const Axis axis, const float threshold, const float cooldown)
    {
        return getAxisPressedDirection(getHand(primaryHand), axis, threshold, cooldown);
    }

    std::optional<Direction> VRControllersManager::getAxisPressedDirection(const vr::ETrackedControllerRole hand, const Axis axis, const float threshold, const float cooldown)
    {
        return get(hand).getAxisPressedAndClear(static_cast<uint32_t>(axis), _currentTime, threshold, cooldown);
    }

    /**
     * Retrieves analog thumbstick value for the specified controller.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    vr::VRControllerAxis_t VRControllersManager::getThumbstickValue(const Hand primaryHand) const
    {
        return getThumbstickValue(getHand(primaryHand));
    }

    vr::VRControllerAxis_t VRControllersManager::getThumbstickValue(const vr::ETrackedControllerRole hand) const
    {
        return get(hand).getAxis(static_cast<uint32_t>(Axis::Thumbstick));
    }

    /**
     * Is the analog thumbstick is pressed in the specified direction.
     * Pressed is defined as the axis value being above a certain threshold in the specified direction.
     * There is cooldown milliseconds debounce time between consecutive positive passing checks.
     */
    bool VRControllersManager::isThumbstickPressed(const Hand primaryHand, const Direction direction, const float threshold, const float cooldown)
    {
        return isThumbstickPressed(getHand(primaryHand), direction, threshold, cooldown);
    }

    bool VRControllersManager::isThumbstickPressed(const vr::ETrackedControllerRole hand, const Direction direction, const float threshold, const float cooldown)
    {
        return get(hand).isAxisPressedAndClear(static_cast<uint32_t>(Axis::Thumbstick), direction, _currentTime, threshold, cooldown);
    }

    /**
     * Get the direction of analog thumbstick if it is pressed.
     * Pressed is defined as the axis value being above a certain threshold in the specified direction.
     * There is cooldown milliseconds debounce time between consecutive positive passing checks.
     */
    std::optional<Direction> VRControllersManager::getThumbstickPressedDirection(const Hand primaryHand, const float threshold, const float cooldown)
    {
        return getThumbstickPressedDirection(getHand(primaryHand), threshold, cooldown);
    }

    std::optional<Direction> VRControllersManager::getThumbstickPressedDirection(const vr::ETrackedControllerRole hand, const float threshold, const float cooldown)
    {
        return get(hand).getAxisPressedAndClear(static_cast<uint32_t>(Axis::Thumbstick), _currentTime, threshold, cooldown);
    }

    /**
     * Get the heading (yaw) of the specified controller in radians [0, 2π)
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    float VRControllersManager::getControllerHeading(const Hand primaryHand) const
    {
        return getControllerHeading(getHand(primaryHand));
    }

    float VRControllersManager::getControllerHeading(const vr::ETrackedControllerRole hand) const
    {
        return get(hand).getHeading();
    }

    /**
     * Get the relative heading difference between controller and HMD
     * Returns the angle in radians [-PI, PI] that the controller is pointing relative to HMD
     */
    float VRControllersManager::getControllerRelativeHeading(const Hand primaryHand) const
    {
        return getControllerRelativeHeading(getHand(primaryHand));
    }

    float VRControllersManager::getControllerRelativeHeading(const vr::ETrackedControllerRole hand) const
    {
        const float hmdHeading = getHMDHeading();
        const float controllerHeading = getControllerHeading(hand);

        // Calculate relative difference
        float diff = controllerHeading - hmdHeading;

        // Normalize to [-PI, PI] range
        if (diff > std::numbers::pi_v<float>) {
            diff -= 2.0f * std::numbers::pi_v<float>;
        } else if (diff < -std::numbers::pi_v<float>) {
            diff += 2.0f * std::numbers::pi_v<float>;
        }

        return diff;
    }

    /**
     * Trigger a haptic pulse on the specified controller for specific duration and intensity.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     * Duration in seconds and intensity between 0.0 and 1.0.
     * Convenience forwarder to VRHaptics, which owns all haptics logic (see VRControllersHaptic.h).
     */
    void VRControllersManager::triggerHaptic(const Hand primaryHand, const float durationSeconds, const float intensity)
    {
        VRHaptics.trigger(primaryHand, durationSeconds, intensity);
    }

    void VRControllersManager::triggerHaptic(const vr::ETrackedControllerRole hand, const float durationSeconds, const float intensity)
    {
        VRHaptics.trigger(hand, durationSeconds, intensity);
    }

    /**
     * Get the heading (yaw) of the HMD in radians [0, 2π)
     */
    float VRControllersManager::getHMDHeading()
    {
        if (!vr::VRSystem()) {
            return 0.0f;
        }

        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, poses, vr::k_unMaxTrackedDeviceCount);

        const auto& hmdPose = poses[vr::k_unTrackedDeviceIndex_Hmd];
        if (!hmdPose.bPoseIsValid) {
            return 0.0f;
        }

        // Extract the forward direction vector from the transformation matrix
        const vr::HmdMatrix34_t& mat = hmdPose.mDeviceToAbsoluteTracking;
        const float x = -mat.m[0][2];
        const float z = -mat.m[2][2];

        // Calculate yaw angle using atan2 and normalize to [0, 2π) range
        float heading = std::atan2(x, z);
        if (heading < 0) {
            heading += 2.0f * std::numbers::pi_v<float>;
        }

        return heading;
    }

    // Updates the controller state and tracks press transitions
    void VRControllersManager::ControllerState::update(const vr::TrackedDeviceIndex_t newIndex, const float now)
    {
        if (newIndex == vr::k_unTrackedDeviceIndexInvalid || !vr::VRSystem()) {
            valid = false;
            return;
        }

        previous = current;
        {
            // Mark this as our own read so VRControllersSuppressor returns raw (unsuppressed) state,
            // even when another mod hooks the same IVRSystem vtable slot outside ours.
            const SelfControllerReadScope selfRead;
            valid = vr::VRSystem()->GetControllerStateWithPose(vr::TrackingUniverseStanding, newIndex, &current, sizeof(current), &pose);
        }
        if (!valid) {
            return;
        }

        // Update press start times for all button transitions
        for (int b = vr::k_EButton_System; b <= vr::k_EButton_Max; ++b) {
            auto button = static_cast<vr::EVRButtonId>(b);
            const bool isNowPressed = (current.ulButtonPressed & vr::ButtonMaskFromId(button)) != 0;
            const bool wasPressed = (previous.ulButtonPressed & vr::ButtonMaskFromId(button)) != 0;

            if (isNowPressed && !wasPressed) {
                pressStartTimes[button] = now;
            } else if (!isNowPressed && wasPressed) {
                if (auto node = pressStartTimes.extract(button)) {
                    pressStartTimesForRelease.insert(std::move(node));
                }
            } else {
                pressStartTimesForRelease.erase(button);
            }
        }
    }

    void VRControllersManager::ControllerState::reset()
    {
        index = vr::k_unTrackedDeviceIndexInvalid;
        current = {};
        previous = {};
        pose = {};
        valid = false;
        pressStartTimes.clear();
        pressStartTimesForRelease.clear();
        lastPressTime.clear();
        lastReleaseTime.clear();
        lastPressDownTime.clear();
        longPressHandled.clear();
        for (auto& t : axisLastPassedPressCheck)
            t = 0.0f;
    }

    bool VRControllersManager::ControllerState::isPressed(const vr::EVRButtonId button) const
    {
        return valid && current.ulButtonPressed & vr::ButtonMaskFromId(button);
    }

    bool VRControllersManager::ControllerState::isTouching(const vr::EVRButtonId button) const
    {
        return valid && current.ulButtonTouched & vr::ButtonMaskFromId(button);
    }

    bool VRControllersManager::ControllerState::justPressed(const vr::EVRButtonId button, const float now, const float debounceCooldown)
    {
        if (!valid) {
            return false;
        }
        const auto mask = vr::ButtonMaskFromId(button);
        const bool wasJustPressed = !(previous.ulButtonPressed & mask) && current.ulButtonPressed & mask;
        return wasJustPressed && checkDebounce(lastPressTime[button], now, debounceCooldown);
    }

    bool VRControllersManager::ControllerState::justReleased(const vr::EVRButtonId button, const float now, const float debounceCooldown)
    {
        const auto mask = vr::ButtonMaskFromId(button);
        if ((!valid || longPressHandled[button]) && !(current.ulButtonPressed & mask)) {
            longPressHandled[button] = false;
            return false;
        }
        const bool wasJustReleased = previous.ulButtonPressed & mask && !(current.ulButtonPressed & mask);
        return wasJustReleased && checkDebounce(lastReleaseTime[button], now, debounceCooldown);
    }

    bool VRControllersManager::ControllerState::justDoublePressed(const vr::EVRButtonId button, const float now, const float maxInterval)
    {
        if (!valid) {
            return false;
        }
        const auto mask = vr::ButtonMaskFromId(button);
        const bool wasJustPressed = !(previous.ulButtonPressed & mask) && current.ulButtonPressed & mask;
        if (!wasJustPressed) {
            return false;
        }
        // Second press-down within the interval since the first -> double press.
        // Clear the stored time so a third quick press starts a new sequence rather than re-firing.
        const auto it = lastPressDownTime.find(button);
        if (it != lastPressDownTime.end() && now - it->second <= maxInterval) {
            lastPressDownTime.erase(it);
            return true;
        }
        // First press (or too slow to pair) -> record it as the start of a potential double press.
        lastPressDownTime[button] = now;
        return false;
    }

    bool VRControllersManager::ControllerState::checkDebounce(float& lastTime, const float now, const float cooldown)
    {
        if (now - lastTime < cooldown) {
            return false;
        }
        lastTime = now;
        return true;
    }

    vr::VRControllerAxis_t VRControllersManager::ControllerState::getAxis(const uint32_t axisIndex) const
    {
        if (!valid || axisIndex >= vr::k_unControllerStateAxisCount) {
            return {};
        }
        return current.rAxis[axisIndex];
    }

    bool VRControllersManager::ControllerState::isAxisPressedAndClear(const uint32_t axisIndex, const Direction direction, const float now, const float threshold,
        const float cooldown)
    {
        if (!valid || now - axisLastPassedPressCheck[axisIndex] < cooldown) {
            return false;
        }
        const auto pressedDirection = getAxisPressed(axisIndex, threshold);
        const bool isPressed = pressedDirection.has_value() && pressedDirection.value() == direction;
        if (isPressed) {
            axisLastPassedPressCheck[axisIndex] = now;
        }
        return isPressed;
    }

    std::optional<Direction> VRControllersManager::ControllerState::getAxisPressedAndClear(const uint32_t axisIndex, const float now, const float threshold, const float cooldown)
    {
        if (!valid || now - axisLastPassedPressCheck[axisIndex] < cooldown) {
            return std::nullopt;
        }
        const auto pressedDirection = getAxisPressed(axisIndex, threshold);
        if (pressedDirection.has_value()) {
            axisLastPassedPressCheck[axisIndex] = now;
        }
        return pressedDirection;
    }

    std::optional<Direction> VRControllersManager::ControllerState::getAxisPressed(const uint32_t axisIndex, const float threshold) const
    {
        const auto axis = getAxis(axisIndex);
        if (axis.x > threshold) {
            return Direction::Right;
        }
        if (axis.x < -threshold) {
            return Direction::Left;
        }
        if (axis.y > threshold) {
            return Direction::Up;
        }
        if (axis.y < -threshold) {
            return Direction::Down;
        }
        return std::nullopt;
    }

    float VRControllersManager::ControllerState::getHeldDuration(const vr::EVRButtonId button, const float now) const
    {
        const auto it = pressStartTimes.find(button);
        if (it == pressStartTimes.end()) {
            return 0.0f;
        }
        return now - it->second;
    }

    void VRControllersManager::ControllerState::clearHeldDuration(const vr::EVRButtonId button)
    {
        pressStartTimes.erase(button);
        // Mark long press as handled so isReleased won't fire
        longPressHandled[button] = true;
    }

    float VRControllersManager::ControllerState::getHeldDurationForRelease(const vr::EVRButtonId button, const float now) const
    {
        const auto it = pressStartTimesForRelease.find(button);
        if (it == pressStartTimesForRelease.end()) {
            return 0.0f;
        }
        return now - it->second;
    }

    /**
     * Get the current heading (yaw) of the controller in radians [0, 2π)
     */
    float VRControllersManager::ControllerState::getHeading() const
    {
        if (!pose.bPoseIsValid) {
            return 0.0f;
        }

        const vr::HmdMatrix34_t& mat = pose.mDeviceToAbsoluteTracking;

        // Extract the forward direction vector from the transformation matrix
        // The forward direction is the negative Z axis in VR coordinate space
        const float x = -mat.m[0][2];
        const float z = -mat.m[2][2];

        // Calculate yaw angle using atan2 and normalize to [0, 2π) range
        float heading = std::atan2(x, z);
        if (heading < 0) {
            heading += 2.0f * std::numbers::pi_v<float>;
        }

        return heading;
    }

    // Returns a time value in seconds since static program start
    float VRControllersManager::getCurrentTimeSeconds()
    {
        static const auto START = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<float> elapsed = now - START;
        return elapsed.count();
    }

    // Helpers to access left or right controller states
    const VRControllersManager::ControllerState& VRControllersManager::get(const vr::ETrackedControllerRole hand) const
    {
        return hand == vr::TrackedControllerRole_LeftHand ? _left : _right;
    }

    VRControllersManager::ControllerState& VRControllersManager::get(const vr::ETrackedControllerRole hand)
    {
        return hand == vr::TrackedControllerRole_LeftHand ? _left : _right;
    }

    // Resolves controller hand based on primary hand and left-handed setting
    inline vr::ETrackedControllerRole VRControllersManager::getHand(const Hand hand) const
    {
        switch (hand) {
        case Hand::Primary:
            return _leftHanded ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
        case Hand::Offhand:
            return _leftHanded ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand;
        case Hand::Right:
            return vr::TrackedControllerRole_RightHand;
        case Hand::Left:
            return vr::TrackedControllerRole_LeftHand;
        default:
            return vr::TrackedControllerRole_OptOut;
        }
    }
}
