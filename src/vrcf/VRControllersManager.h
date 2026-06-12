#pragma once

#include <optional>
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

    inline uint64_t ButtonMaskFromId(const VRButtonId id)
    {
        return 1ull << id;
    }

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
     * How a button (or axis) must be activated for an InputBinding to be considered "triggered".
     * Each value maps to one of the VRControllersManager check methods; see check().
     */
    enum class ActivationType : std::uint8_t
    {
        Disabled, // never triggers - the binding is turned off (config "none" / "off" / "disabled" / empty value)
        Touch, // isTouching - button is currently touched (capacitive), not necessarily pressed
        Press, // isPressed - button edge-pressed this frame (debounced)
        HoldDown, // isPressHeldDown - button held down, every frame; uses `duration` as the minimum hold
        Release, // isReleased - button edge-released this frame; uses `duration` as the maximum hold to still count (0 = any)
        Tap, // isTap - quick press-and-release (held < 0.3s); the "Tap" gesture
        LongPress, // isLongPressed - button held longer than `duration`
        DoublePress, // isDoublePressed - two presses within `duration`
        AxisDirection, // isAxisPressed - `axis` pushed past `threshold` in `direction`
    };

    /**
     * Optional modifier button that must be held down for a binding to trigger (a chord).
     * `hand` is optional: when unset the modifier is checked on the binding's own hand; set it to
     * require the modifier on a specific hand (e.g. binding on the primary hand, modifier on the offhand).
     */
    struct InputModifier
    {
        vr::EVRButtonId button = vr::k_EButton_Grip;
        std::optional<Hand> hand;
    };

    /**
     * A fully-described, config-loadable controller input binding.
     * Bundles the hand, the button/axis, and how it must be activated so a single VRControllersManager::check()
     * call can evaluate any binding loaded from config. The meaning of the timing/sensitivity fields depends on
     * `type` (see ActivationType). For button types `button` is used; for AxisDirection `axis` + `direction` are used.
     */
    struct InputBinding
    {
        Hand hand = Hand::Primary;
        ActivationType type = ActivationType::Press;

        // Used by all button activation types (everything except AxisDirection).
        vr::EVRButtonId button = vr::k_EButton_SteamVR_Trigger;

        // Used by AxisDirection only.
        Axis axis = Axis::Thumbstick;
        Direction direction = Direction::Up;

        // Optional modifier button that must be held down for the binding to trigger (see InputModifier).
        std::optional<InputModifier> modifier;

        // Timing / sensitivity - meaning depends on `type` (see ActivationType). 0 means "use the per-type default".
        float duration = 0.0f;
        float threshold = 0.85f; // AxisDirection only
        float cooldown = 0.15f; // AxisDirection only
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

        bool check(const InputBinding& binding);

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
        bool isTap(Hand hand, vr::EVRButtonId button);
        bool isTap(Hand hand, int button);
        bool isReleased(vr::ETrackedControllerRole hand, vr::EVRButtonId button, float maxHoldDurationSeconds = 99);

        bool isLongPressed(Hand hand, vr::EVRButtonId button, float durationSeconds = 0.6f, bool clear = true);
        bool isLongPressed(vr::ETrackedControllerRole hand, vr::EVRButtonId button, float durationSeconds = 0.6f, bool clear = true);

        bool isDoublePressed(Hand hand, vr::EVRButtonId button, float maxIntervalSeconds = 0.4f);
        bool isDoublePressed(Hand hand, int button, float maxIntervalSeconds = 0.4f);
        bool isDoublePressed(vr::ETrackedControllerRole hand, vr::EVRButtonId button, float maxIntervalSeconds = 0.4f);

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

        // Convenience forwarders to VRHaptics (see VRControllersHaptic.h); prefer VRHaptics directly
        // for the pattern library (HapticPattern) and playback control.
        static void triggerHaptic(Hand primaryHand, float durationSeconds = 0.1f, float intensity = 0.3f);
        static void triggerHaptic(vr::ETrackedControllerRole hand, float durationSeconds = 0.2f, float intensity = 0.3f);
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
            std::unordered_map<vr::EVRButtonId, float> lastPressDownTime; // Track time of last press-down for double press detection
            std::unordered_map<vr::EVRButtonId, bool> longPressHandled; // Track if long press was handled
            float axisLastPassedPressCheck[5] = { 0, 0, 0, 0, 0 };

            void update(vr::TrackedDeviceIndex_t newIndex, float now);
            void reset();
            bool isPressed(vr::EVRButtonId button) const;
            bool isTouching(vr::EVRButtonId button) const;
            bool justPressed(vr::EVRButtonId button, float now, float debounceCooldown);
            bool justReleased(vr::EVRButtonId button, float now, float debounceCooldown);
            bool justDoublePressed(vr::EVRButtonId button, float now, float maxInterval);
            static bool checkDebounce(float& lastTime, float now, float cooldown);
            vr::VRControllerAxis_t getAxis(uint32_t axisIndex) const;
            bool isAxisPressedAndClear(uint32_t axisIndex, Direction direction, float now, float threshold, float cooldown);
            std::optional<Direction> getAxisPressedAndClear(uint32_t axisIndex, float now, float threshold, float cooldown);
            std::optional<Direction> getAxisPressed(uint32_t axisIndex, float threshold) const;
            float getHeldDuration(vr::EVRButtonId button, float now) const;
            void clearHeldDuration(vr::EVRButtonId button);
            float getHeldDurationForRelease(vr::EVRButtonId button, float now) const;
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
