#include "VRControllersHaptic.h"

#include <algorithm>
#include <chrono>

namespace
{
    using f4cf::vrcf::HapticSegment;

    // Perceived strength is the duty cycle: pulse microseconds over the frame interval. Firing a
    // fixed pulse length would make the same intensity feel ~1.7x stronger at 120fps than at 70fps,
    // so the pulse is scaled to the measured frame delta to hold a constant duty cycle. 0.27 is the
    // duty cycle of the framework's previous fixed 3000us pulse at 90fps, preserving that feel.
    constexpr float FULL_INTENSITY_DUTY_CYCLE = 0.27f;

    // The runtime clamps TriggerHapticPulse at ~3999us per pulse.
    constexpr int MAX_PULSE_MICROSEC = 3999;

    // Frame delta clamp for the duty-cycle scaling: covers ~40-125fps so a load hitch can't spike a
    // pulse and an absurdly fast frame can't starve one.
    constexpr float MIN_FRAME_DELTA = 0.008f;
    constexpr float MAX_FRAME_DELTA = 0.025f;

    // Below this the pulse is too short to feel; skip the OpenVR call entirely.
    constexpr float MIN_AUDIBLE_INTENSITY = 0.02f;

    // --- Pattern library keyframes: { duration sec, start intensity, end intensity } ---
    // Timing granularity is one frame (~11ms at 90Hz): segments shorter than a frame still produce
    // exactly one pulse, and silent gaps should be >= ~50ms to be clearly felt as separation.

    constexpr HapticSegment PATTERN_TICK[] = { { 0.02f, 0.25f, 0.25f } };
    constexpr HapticSegment PATTERN_CLICK[] = { { 0.03f, 0.7f, 0.7f } };
    constexpr HapticSegment PATTERN_DOUBLE_CLICK[] = { { 0.03f, 0.5f, 0.5f }, { 0.05f, 0.0f, 0.0f }, { 0.03f, 0.5f, 0.5f } };
    constexpr HapticSegment PATTERN_TRIPLE_CLICK[] = { { 0.03f, 0.5f, 0.5f }, { 0.05f, 0.0f, 0.0f }, { 0.03f, 0.5f, 0.5f }, { 0.08f, 0.0f, 0.0f }, { 0.03f, 0.5f, 0.5f } };
    constexpr HapticSegment PATTERN_SUCCESS[] = { { 0.05f, 0.3f, 0.3f }, { 0.05f, 0.0f, 0.0f }, { 0.08f, 0.8f, 0.8f } };
    constexpr HapticSegment PATTERN_WARNING[] = { { 0.06f, 0.9f, 0.9f }, { 0.05f, 0.0f, 0.0f }, { 0.2f, 0.3f, 0.3f } };
    constexpr HapticSegment PATTERN_ERROR[] = { { 0.05f, 0.6f, 0.6f }, { 0.06f, 0.0f, 0.0f }, { 0.05f, 0.6f, 0.6f }, { 0.06f, 0.0f, 0.0f }, { 0.16f, 1.0f, 1.0f } };
    constexpr HapticSegment PATTERN_NOTIFICATION[] = { { 0.1f, 0.7f, 0.7f }, { 0.12f, 0.0f, 0.0f }, { 0.03f, 0.4f, 0.4f }, { 0.12f, 0.0f, 0.0f }, { 0.03f, 0.3f, 0.3f } };
    constexpr HapticSegment PATTERN_START[] = { { 0.5f, 0.0f, 1.0f }, { 0.1f, 1.0f, 1.0f } };
    constexpr HapticSegment PATTERN_STOP[] = { { 0.1f, 1.0f, 0.8f }, { 0.5f, 0.8f, 0.0f } };
    constexpr HapticSegment PATTERN_RAMP_UP[] = { { 0.6f, 0.0f, 1.0f } };
    constexpr HapticSegment PATTERN_RAMP_DOWN[] = { { 0.6f, 1.0f, 0.0f } };
    constexpr HapticSegment PATTERN_HEARTBEAT_1[] = { { 0.08f, 0.8f, 0.8f }, { 0.1f, 0.0f, 0.0f }, { 0.06f, 0.45f, 0.45f } };
    constexpr HapticSegment PATTERN_HEARTBEAT_2[] = { { 0.08f, 0.8f, 0.8f },
        { 0.1f, 0.0f, 0.0f },
        { 0.06f, 0.45f, 0.45f },
        { 0.4f, 0.0f, 0.0f },
        { 0.08f, 0.8f, 0.8f },
        { 0.1f, 0.0f, 0.0f },
        { 0.06f, 0.45f, 0.45f } };
    constexpr HapticSegment PATTERN_HEARTBEAT_3[] = { { 0.08f, 0.8f, 0.8f },
        { 0.1f, 0.0f, 0.0f },
        { 0.06f, 0.45f, 0.45f },
        { 0.4f, 0.0f, 0.0f },
        { 0.08f, 0.8f, 0.8f },
        { 0.1f, 0.0f, 0.0f },
        { 0.06f, 0.45f, 0.45f },
        { 0.4f, 0.0f, 0.0f },
        { 0.08f, 0.8f, 0.8f },
        { 0.1f, 0.0f, 0.0f },
        { 0.06f, 0.45f, 0.45f } };
    constexpr HapticSegment PATTERN_BUZZ[] = { { 0.25f, 0.5f, 0.5f } };
    constexpr HapticSegment PATTERN_MID_BUZZ[] = { { 0.5f, 0.5f, 0.5f } };
    constexpr HapticSegment PATTERN_LONG_BUZZ[] = { { 0.7f, 0.6f, 0.6f } };
}

namespace f4cf::vrcf
{
    /**
     * Refresh controller indexes and advance both hands' active patterns; must be called each frame.
     */
    void VRControllersHaptic::update(const bool isLeftHanded)
    {
        if (!vr::VRSystem()) {
            return;
        }

        _leftHanded = isLeftHanded;

        _left.index = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        _right.index = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

        const auto now = getCurrentTimeSeconds();
        _frameDelta = std::clamp(now - _lastUpdateTime, MIN_FRAME_DELTA, MAX_FRAME_DELTA);
        _lastUpdateTime = now;

        advanceAndPulse(_left, now, _frameDelta);
        advanceAndPulse(_right, now, _frameDelta);
    }

    /**
     * Stops all playback and forgets controller indexes. For lifecycle reload (clean slate).
     */
    void VRControllersHaptic::reset()
    {
        stopAll();
        _left.index = vr::k_unTrackedDeviceIndexInvalid;
        _right.index = vr::k_unTrackedDeviceIndexInvalid;
    }

    /**
     * Constant buzz on the given hand: intensity 0..1 sustained for the duration by per-frame pulses.
     * Regular primary is right hand, but if left hand mode is on then primary is left hand.
     */
    void VRControllersHaptic::trigger(const Hand hand, const float durationSeconds, const float intensity)
    {
        trigger(resolve(hand), durationSeconds, intensity);
    }

    void VRControllersHaptic::trigger(const vr::ETrackedControllerRole hand, const float durationSeconds, const float intensity)
    {
        const HapticSegment segment{ durationSeconds, intensity, intensity };
        start(get(hand), std::span(&segment, 1), 1.0f);
    }

    /**
     * Play a named pattern from the library on the given hand, replacing whatever was playing.
     */
    void VRControllersHaptic::trigger(const Hand hand, const HapticPattern pattern, const float intensityScale)
    {
        trigger(resolve(hand), pattern, intensityScale);
    }

    void VRControllersHaptic::trigger(const vr::ETrackedControllerRole hand, const HapticPattern pattern, const float intensityScale)
    {
        start(get(hand), getPattern(pattern), intensityScale);
    }

    /**
     * Play a custom segment sequence on the given hand. The segments are copied.
     */
    void VRControllersHaptic::trigger(const Hand hand, const std::span<const HapticSegment> pattern, const float intensityScale)
    {
        trigger(resolve(hand), pattern, intensityScale);
    }

    void VRControllersHaptic::trigger(const vr::ETrackedControllerRole hand, const std::span<const HapticSegment> pattern, const float intensityScale)
    {
        start(get(hand), pattern, intensityScale);
    }

    void VRControllersHaptic::stop(const Hand hand)
    {
        stop(resolve(hand));
    }

    void VRControllersHaptic::stop(const vr::ETrackedControllerRole hand)
    {
        auto& playback = get(hand);
        playback.active = false;
        playback.segments.clear();
    }

    void VRControllersHaptic::stopAll()
    {
        stop(vr::TrackedControllerRole_LeftHand);
        stop(vr::TrackedControllerRole_RightHand);
    }

    bool VRControllersHaptic::isPlaying(const Hand hand) const
    {
        return isPlaying(resolve(hand));
    }

    bool VRControllersHaptic::isPlaying(const vr::ETrackedControllerRole hand) const
    {
        return get(hand).active;
    }

    /**
     * The keyframes behind a named pattern.
     */
    std::span<const HapticSegment> VRControllersHaptic::getPattern(const HapticPattern pattern)
    {
        switch (pattern) {
        case HapticPattern::Tick:
            return PATTERN_TICK;
        case HapticPattern::Click:
            return PATTERN_CLICK;
        case HapticPattern::DoubleClick:
            return PATTERN_DOUBLE_CLICK;
        case HapticPattern::TripleClick:
            return PATTERN_TRIPLE_CLICK;
        case HapticPattern::Success:
            return PATTERN_SUCCESS;
        case HapticPattern::Warning:
            return PATTERN_WARNING;
        case HapticPattern::Error:
            return PATTERN_ERROR;
        case HapticPattern::Notification:
            return PATTERN_NOTIFICATION;
        case HapticPattern::Start:
            return PATTERN_START;
        case HapticPattern::Stop:
            return PATTERN_STOP;
        case HapticPattern::RampUp:
            return PATTERN_RAMP_UP;
        case HapticPattern::RampDown:
            return PATTERN_RAMP_DOWN;
        case HapticPattern::Heartbeat1:
            return PATTERN_HEARTBEAT_1;
        case HapticPattern::Heartbeat2:
            return PATTERN_HEARTBEAT_2;
        case HapticPattern::Heartbeat3:
            return PATTERN_HEARTBEAT_3;
        case HapticPattern::Buzz:
            return PATTERN_BUZZ;
        case HapticPattern::MidBuzz:
            return PATTERN_MID_BUZZ;
        case HapticPattern::LongBuzz:
            return PATTERN_LONG_BUZZ;
        default:
            return PATTERN_CLICK;
        }
    }

    /**
     * Maps a logical hand to the physical controller role using the cached left-handed flag.
     * Mirrors VRControllersManager::getHand.
     */
    vr::ETrackedControllerRole VRControllersHaptic::resolve(const Hand hand) const
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

    VRControllersHaptic::Playback& VRControllersHaptic::get(const vr::ETrackedControllerRole hand)
    {
        return hand == vr::TrackedControllerRole_LeftHand ? _left : _right;
    }

    const VRControllersHaptic::Playback& VRControllersHaptic::get(const vr::ETrackedControllerRole hand) const
    {
        return hand == vr::TrackedControllerRole_LeftHand ? _left : _right;
    }

    /**
     * Replace the hand's playback with the given pattern and fire the first pulse immediately so
     * one-frame feedback isn't delayed (or lost) until the next update() tick.
     */
    void VRControllersHaptic::start(Playback& playback, const std::span<const HapticSegment> pattern, const float intensityScale)
    {
        playback.segments.assign(pattern.begin(), pattern.end());
        playback.segmentIndex = 0;
        playback.segmentStartTime = getCurrentTimeSeconds();
        playback.intensityScale = std::max(intensityScale, 0.0f);
        playback.active = !playback.segments.empty();

        if (playback.active) {
            firePulse(playback.index, playback.segments.front().startIntensity * playback.intensityScale, _frameDelta);
        }
    }

    /**
     * Advance the playback to the segment containing `now` (skipping segments fully elapsed during
     * dropped frames), then fire this frame's pulse at the interpolated intensity.
     */
    void VRControllersHaptic::advanceAndPulse(Playback& playback, const float now, const float frameDelta)
    {
        if (!playback.active) {
            return;
        }

        while (playback.segmentIndex < playback.segments.size() && now - playback.segmentStartTime >= playback.segments[playback.segmentIndex].duration) {
            playback.segmentStartTime += playback.segments[playback.segmentIndex].duration;
            ++playback.segmentIndex;
        }
        if (playback.segmentIndex >= playback.segments.size()) {
            playback.active = false;
            playback.segments.clear();
            return;
        }

        const auto& segment = playback.segments[playback.segmentIndex];
        const float t = segment.duration > 0.0f ? (now - playback.segmentStartTime) / segment.duration : 0.0f;
        const float intensity = (segment.startIntensity + (segment.endIntensity - segment.startIntensity) * t) * playback.intensityScale;
        firePulse(playback.index, intensity, frameDelta);
    }

    /**
     * Fire one OpenVR pulse at the given perceived intensity (0..1). TriggerHapticPulse's
     * microseconds argument is effectively intensity: a single pulse is capped at ~3999us (about a
     * third of a 90Hz frame), so the value sets the duty cycle within the frame, not a duration.
     * The pulse is scaled to the frame delta so an intensity feels the same at 70, 90, or 120fps.
     * Re-triggers within 5ms of the previous pulse are ignored by the runtime, which is harmless at
     * frame cadence (~11ms).
     */
    void VRControllersHaptic::firePulse(const vr::TrackedDeviceIndex_t index, const float intensity, const float frameDelta)
    {
        if (index == vr::k_unTrackedDeviceIndexInvalid || intensity < MIN_AUDIBLE_INTENSITY || !vr::VRSystem()) {
            return;
        }
        const auto micros = static_cast<unsigned short>(std::clamp(static_cast<int>(intensity * frameDelta * 1e6f * FULL_INTENSITY_DUTY_CYCLE), 0, MAX_PULSE_MICROSEC));
        vr::VRSystem()->TriggerHapticPulse(index, 0, micros);
    }

    float VRControllersHaptic::getCurrentTimeSeconds()
    {
        static const auto START = std::chrono::steady_clock::now();
        const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - START;
        return elapsed.count();
    }
}
