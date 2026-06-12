// ReSharper disable CppClangTidyClangDiagnosticUniqueObjectDuplication
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "VRControllersManager.h"

#include "../../external/openvr/openvr.h"

namespace f4cf::vrcf
{
    /**
     * Library of named haptic patterns, loosely modeled on the Apple Watch / iOS haptic vocabulary
     * (WKHapticType, UIImpactFeedbackGenerator, UINotificationFeedbackGenerator) so different
     * operations get a distinct, recognizable feel. Keyframes for each live in VRControllersHaptic.cpp.
     */
    enum class HapticPattern : std::uint8_t
    {
        Tick, // lightest single tap - selection change, scroll detent
        Click, // crisp single tap - button press acknowledgement
        DoubleClick, // two quick equal taps - toggle on, mode switch
        TripleClick, // three quick equal taps - special/rare action
        Success, // light tap then a stronger one (rising) - operation completed
        Warning, // strong tap then a weaker one (falling) - attention needed
        Error, // two short buzzes then a long strong one - operation failed
        Notification, // long buzz then two light taps - something happened elsewhere
        Start, // ramp up into a hold - continuous operation began
        Stop, // hold then ramp down - continuous operation ended
        RampUp, // long smooth low-to-high ramp - charging / filling
        RampDown, // long smooth high-to-low ramp - draining / powering off
        Heartbeat1, // one lub-dub beat - alive/active indicator
        Heartbeat2, // two lub-dub beats - alive/active indicator
        Heartbeat3, // three lub-dub beats - alive/active indicator
        Buzz, // short constant rumble - generic feedback
        MidBuzz, // mid constant rumble - moderate emphasis
        LongBuzz, // long constant rumble - heavy emphasis
    };

    /**
     * One keyframed step of a haptic pattern. Intensity is linearly interpolated from
     * `startIntensity` to `endIntensity` (both 0..1) over `duration` seconds; equal values give a
     * constant buzz, zero values give a silent gap between pulses.
     */
    struct HapticSegment
    {
        float duration = 0.1f; // seconds
        float startIntensity = 0.3f; // 0..1
        float endIntensity = 0.3f; // 0..1
    };

    /**
     * Plays haptic feedback on the VR controllers: simple duration+intensity buzzes, named patterns
     * from the HapticPattern library, or fully custom HapticSegment sequences.
     *
     * Why a per-frame engine at all: the only haptic API Fallout 4 VR's legacy IVRSystem exposes is
     * TriggerHapticPulse(device, axis, microseconds) - a single one-shot pulse capped at ~4ms
     * (values above ~3999us are clamped by the runtime), with a 5ms re-trigger lockout per
     * controller+axis. There is no real duration or amplitude control: the microseconds argument is
     * effectively perceived intensity (duty cycle within the frame). Any sustained or shaped
     * feedback therefore has to be synthesized by re-firing a short pulse every frame with a
     * per-frame intensity, which is exactly what this class does (same approach as FO4VRTools'
     * StartHaptics/UpdateHaptics). Pulses are scaled to the measured frame delta so a given
     * intensity holds the same duty cycle - and therefore the same feel - at any frame rate.
     *
     * Driven by ModBase: update() is called every frame alongside VRControllers.update(), advancing
     * each hand's active pattern and firing that frame's pulse. trigger() also fires the first pulse
     * immediately so one-frame feedback (e.g. a Tick) never gets lost. A new trigger on a hand
     * replaces whatever was playing on it. Main thread only.
     */
    class VRControllersHaptic
    {
    public:
        // Driven each frame by the framework; advances active patterns and fires the frame's pulses.
        void update(bool isLeftHanded);

        // Stops all playback and forgets controller indexes (lifecycle reload).
        void reset();

        // Constant buzz: intensity 0..1 held for the given duration.
        void trigger(Hand hand, float durationSeconds = 0.1f, float intensity = 0.3f);
        void trigger(vr::ETrackedControllerRole hand, float durationSeconds = 0.1f, float intensity = 0.3f);

        // Play a named pattern from the library. intensityScale multiplies every keyframe (e.g. 0.5 = gentle).
        void trigger(Hand hand, HapticPattern pattern, float intensityScale = 1.0f);
        void trigger(vr::ETrackedControllerRole hand, HapticPattern pattern, float intensityScale = 1.0f);

        // Play a custom segment sequence; the segments are copied, the span need not outlive the call.
        void trigger(Hand hand, std::span<const HapticSegment> pattern, float intensityScale = 1.0f);
        void trigger(vr::ETrackedControllerRole hand, std::span<const HapticSegment> pattern, float intensityScale = 1.0f);

        // Cut playback on one hand / both hands.
        void stop(Hand hand);
        void stop(vr::ETrackedControllerRole hand);
        void stopAll();

        bool isPlaying(Hand hand) const;
        bool isPlaying(vr::ETrackedControllerRole hand) const;

        // The keyframes behind a named pattern (e.g. to tweak and re-trigger as a custom pattern).
        static std::span<const HapticSegment> getPattern(HapticPattern pattern);

    private:
        // Active pattern playback state for one physical controller.
        struct Playback
        {
            vr::TrackedDeviceIndex_t index = vr::k_unTrackedDeviceIndexInvalid;
            std::vector<HapticSegment> segments;
            std::size_t segmentIndex = 0;
            float segmentStartTime = 0.0f;
            float intensityScale = 1.0f;
            bool active = false;
        };

        vr::ETrackedControllerRole resolve(Hand hand) const;
        Playback& get(vr::ETrackedControllerRole hand);
        const Playback& get(vr::ETrackedControllerRole hand) const;

        void start(Playback& playback, std::span<const HapticSegment> pattern, float intensityScale);
        static void advanceAndPulse(Playback& playback, float now, float frameDelta);
        static void firePulse(vr::TrackedDeviceIndex_t index, float intensity, float frameDelta);
        static float getCurrentTimeSeconds();

        Playback _left;
        Playback _right;
        bool _leftHanded = false;
        // Measured time between update() calls (clamped), used to scale pulses to a constant duty
        // cycle so intensity feels the same at any frame rate.
        float _frameDelta = 1.0f / 90.0f;
        float _lastUpdateTime = 0.0f;
    };

    // Global singleton instance, matching the VRControllers convention.
    inline VRControllersHaptic VRHaptics; // NOLINT(clang-diagnostic-unique-object-duplication)
}
