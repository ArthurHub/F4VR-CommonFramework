#pragma once

#include <unordered_map>

#include "../../external/openvr/openvr.h"

namespace f4cf::vrcf
{
    enum VRButtonId : std::uint8_t
    {
        k_EButton_System = 0,
        k_EButton_ApplicationMenu = 1,
        k_EButton_Grip = 2,
        k_EButton_DPad_Left = 3,
        k_EButton_DPad_Up = 4,
        k_EButton_DPad_Right = 5,
        k_EButton_DPad_Down = 6,
        k_EButton_A = 7,

        k_EButton_ProximitySensor = 31,

        k_EButton_Axis0 = 32,
        k_EButton_Axis1 = 33,
        k_EButton_Axis2 = 34,
        k_EButton_Axis3 = 35,
        k_EButton_Axis4 = 36,

        // aliases for well known controllers
        k_EButton_SteamVR_Touchpad = k_EButton_Axis0,
        k_EButton_SteamVR_Trigger = k_EButton_Axis1,

        k_EButton_Dashboard_Back = k_EButton_Grip,

        k_EButton_Knuckles_A = k_EButton_Grip,
        k_EButton_Knuckles_B = k_EButton_ApplicationMenu,
        k_EButton_Knuckles_JoyStick = k_EButton_Axis3,

        k_EButton_Max = 64
    };

    inline uint64_t ButtonMaskFromId(const VRButtonId id) { return 1ull << id; }

    // TODO: remove this after migrating all code from getControllerState_DEPRECATED
    enum class TrackerType : std::uint8_t
    {
        HMD,
        Left,
        Right,
        Vive
    };

    enum class Hand : std::uint8_t
    {
        Primary,
        Offhand,
        Right,
        Left,
    };

    /**
     * The direction of the controller thumbstick input.
     */
    enum class Direction : std::uint8_t
    {
        Right,
        Left,
        Up,
        Down,
    };

    /**
     * The analog axis of the controller.
     */
    enum class Axis : std::uint8_t
    {
        Thumbstick = 0,
        Trigger,
        Grip,
        Unknown2,
        Unknown3,
    };

    /**
     * Manages VR controller input states and button interaction logic
     */
    class VRControllersManager
    {
    public:
        void update(bool isLeftHanded);

        void reset();

        void setDebounceCooldown(float seconds);

        vr::VRControllerState_t getControllerState_DEPRECATED(TrackerType a_tracker) const;

        bool isTouching(Hand hand, vr::EVRButtonId button) const;
        bool isTouching(Hand hand, int button) const;
        bool isTouching(vr::ETrackedControllerRole hand, vr::EVRButtonId button) const;

        bool isPressed(Hand hand, vr::EVRButtonId button);
        bool isPressed(Hand hand, int button);
        bool isPressed(vr::ETrackedControllerRole hand, vr::EVRButtonId button);

        bool isPressHeldDown(Hand hand, vr::EVRButtonId button, float minHoldDurationSeconds = 0) const;
        bool isPressHeldDown(Hand hand, int button, float minHoldDurationSeconds = 0) const;
        bool isPressHeldDown(vr::ETrackedControllerRole hand, vr::EVRButtonId button, float minHoldDurationSeconds = 0) const;

        bool isReleased(Hand hand, vr::EVRButtonId button, float maxHoldDurationSeconds = 99);
        bool isReleased(Hand hand, int button, float maxHoldDurationSeconds = 99);
        bool isReleasedShort(Hand hand, vr::EVRButtonId button);
        bool isReleasedShort(Hand hand, int button);
        bool isReleased(vr::ETrackedControllerRole hand, vr::EVRButtonId button, float maxHoldDurationSeconds = 99);

        bool isLongPressed(Hand hand, vr::EVRButtonId button, float durationSeconds = 0.6f, bool clear = true);
        bool isLongPressed(vr::ETrackedControllerRole hand, vr::EVRButtonId button, float durationSeconds = 0.6f, bool clear = true);

        vr::VRControllerAxis_t getAxisValue(Hand primaryHand, Axis axis) const;
        vr::VRControllerAxis_t getAxisValue(vr::ETrackedControllerRole hand, Axis axis) const;
        bool isAxisPressed(Hand primaryHand, Axis axis, Direction direction, float threshold = 0.85f, float cooldown = 0.15f);
        bool isAxisPressed(vr::ETrackedControllerRole hand, Axis axis, Direction direction, float threshold = 0.85f, float cooldown = 0.15f);
        std::optional<Direction> getAxisPressedDirection(Hand primaryHand, Axis axis, float threshold = 0.85f, float cooldown = 0.15f);
        std::optional<Direction> getAxisPressedDirection(vr::ETrackedControllerRole hand, Axis axis, float threshold = 0.85f, float cooldown = 0.15f);

        vr::VRControllerAxis_t getThumbstickValue(Hand primaryHand) const;
        vr::VRControllerAxis_t getThumbstickValue(vr::ETrackedControllerRole hand) const;
        bool isThumbstickPressed(Hand primaryHand, Direction direction, float threshold = 0.85f, float cooldown = 0.15f);
        bool isThumbstickPressed(vr::ETrackedControllerRole hand, Direction direction, float threshold = 0.85f, float cooldown = 0.15f);
        std::optional<Direction> getThumbstickPressedDirection(Hand primaryHand, float threshold = 0.85f, float cooldown = 0.15f);
        std::optional<Direction> getThumbstickPressedDirection(vr::ETrackedControllerRole hand, float threshold = 0.85f, float cooldown = 0.15f);

        float getControllerHeading(Hand primaryHand) const;
        float getControllerHeading(vr::ETrackedControllerRole hand) const;
        float getControllerRelativeHeading(Hand primaryHand) const;
        float getControllerRelativeHeading(vr::ETrackedControllerRole hand) const;

        void triggerHaptic(Hand primaryHand, float durationSeconds = 0.1f, float intensity = 0.3f);
        void triggerHaptic(vr::ETrackedControllerRole hand, float durationSeconds = 0.2f, float intensity = 0.3f);
        static float getHMDHeading();

    private:
        // Internal state for one controller (left or right)
        struct ControllerState
        {
            vr::TrackedDeviceIndex_t index = vr::k_unTrackedDeviceIndexInvalid;
            vr::VRControllerState_t current{};
            vr::VRControllerState_t previous{};
            vr::TrackedDevicePose_t pose{};
            bool valid = false;
            std::unordered_map<vr::EVRButtonId, float> pressStartTimes; // Track how long each button has been held
            std::unordered_map<vr::EVRButtonId, float> pressStartTimesForRelease;
            std::unordered_map<vr::EVRButtonId, float> lastPressTime;
            std::unordered_map<vr::EVRButtonId, float> lastReleaseTime;
            std::unordered_map<vr::EVRButtonId, bool> longPressHandled; // Track if long press was handled
            float axisLastPassedPressCheck[5] = { 0, 0, 0, 0, 0 };
            float hapticEndTime = 0;
            float hapticIntensity = 0;

            void update(vr::TrackedDeviceIndex_t newIndex, float now);
            void reset();
            bool isPressed(vr::EVRButtonId button) const;
            bool isTouching(vr::EVRButtonId button) const;
            bool justPressed(vr::EVRButtonId button, float now, float debounceCooldown);
            bool justReleased(vr::EVRButtonId button, float now, float debounceCooldown);
            static bool checkDebounce(float& lastTime, float now, float cooldown);
            vr::VRControllerAxis_t getAxis(uint32_t axisIndex) const;
            bool isAxisPressedAndClear(uint32_t axisIndex, Direction direction, float now, float threshold, float cooldown);
            std::optional<Direction> getAxisPressedAndClear(uint32_t axisIndex, float now, float threshold, float cooldown);
            std::optional<Direction> getAxisPressed(uint32_t axisIndex, float threshold) const;
            float getHeldDuration(vr::EVRButtonId button, float now) const;
            void clearHeldDuration(vr::EVRButtonId button);
            float getHeldDurationForRelease(vr::EVRButtonId button, float now) const;
            void startHaptic(float endTime, float intensity);
            void triggerHapticPulse() const;
            float getHeading() const;
        };

        static float getCurrentTimeSeconds();
        const ControllerState& get(vr::ETrackedControllerRole hand) const;
        ControllerState& get(vr::ETrackedControllerRole hand);
        vr::ETrackedControllerRole getHand(Hand hand) const;

        ControllerState _left;
        ControllerState _right;
        bool _leftHanded = false;
        float _currentTime = 0.0f;
        float _debounceCooldown = 0.1f; // default 100 ms
    };

    // Global singleton instance for convenient access
    inline VRControllersManager VRControllers; // NOLINT(clang-diagnostic-unique-object-duplication)
}
