#include "DebugAdjuster.h"

#include <algorithm>
#include <vector>

#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRUtils.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersManager.h"
#include "vrcf/VRControllersSuppressor.h"

namespace f4cf
{
    namespace
    {
        constexpr float DEADZONE = 0.5f;
        constexpr float TRANSLATE_PER_FRAME = 0.3f;
        constexpr float ROTATE_DEGREES_PER_FRAME = 0.5f;
        constexpr float SCALE_PER_FRAME = 0.005f;
        constexpr float FLOW_FLAG_PER_FRAME = 0.03f;
        constexpr float HAND_POSE_FINGER_PER_FRAME = 0.01f;
        constexpr float HAND_POSE_PALM_PER_FRAME = 0.25f;

        // Saved debug values are rounded to this many decimals to keep the INI readable.
        constexpr int SAVE_PRECISION = 2;

        // Hand pose slot names — index matches the active-slot state used by adjustHandPose.
        // Slots 0..4 control one finger each (4 floats: prox,mid,dist,splay).
        // Slot 5 controls the two palm floats (palmPitch, palmYaw).
        constexpr std::array<const char*, 6> HAND_POSE_SLOT_NAMES = { "thumb", "index", "middle", "ring", "pinky", "palm" };
        std::size_t s_handPoseSlot = 0;

        /**
         * Format a float with SAVE_PRECISION decimals for writing to the INI.
         */
        std::string toFixed(const float value)
        {
            return common::toStringWithPrecision(value, SAVE_PRECISION);
        }

        /**
         * Format an NiTransform as a fixed-precision "x,y,z;heading,roll,attitude;scale" string
         * (rotation in degrees). Mirrors the framework's transform format but at SAVE_PRECISION
         * decimals so saved debug transforms stay readable.
         */
        std::string transformToFixedString(const RE::NiTransform& transform)
        {
            float heading, roll, attitude;
            common::MatrixUtils::getEulerAnglesFromMatrixDegrees(transform.rotate, &heading, &roll, &attitude);
            return fmt::format("{},{},{};{},{},{};{}",
                toFixed(transform.translate.x),
                toFixed(transform.translate.y),
                toFixed(transform.translate.z),
                toFixed(heading),
                toFixed(roll),
                toFixed(attitude),
                toFixed(transform.scale));
        }

        /**
         * Format a 22-float hand pose in the canonical INI layout:
         * 5 ';'-separated groups of 4 ','-separated floats (thumb...pinky each prox,mid,dist,splay),
         * then 2 trailing ',' separated palm floats (palmPitch, palmYaw).
         */
        std::string handPoseToFixedString(const std::array<float, 22>& v)
        {
            return fmt::format("{},{},{},{};{},{},{},{};{},{},{},{};{},{},{},{};{},{},{},{};{},{}",
                toFixed(v[0]),
                toFixed(v[1]),
                toFixed(v[2]),
                toFixed(v[3]),
                toFixed(v[4]),
                toFixed(v[5]),
                toFixed(v[6]),
                toFixed(v[7]),
                toFixed(v[8]),
                toFixed(v[9]),
                toFixed(v[10]),
                toFixed(v[11]),
                toFixed(v[12]),
                toFixed(v[13]),
                toFixed(v[14]),
                toFixed(v[15]),
                toFixed(v[16]),
                toFixed(v[17]),
                toFixed(v[18]),
                toFixed(v[19]),
                toFixed(v[20]),
                toFixed(v[21]));
        }

        /**
         * Zero out stick noise inside the deadzone.
         */
        float applyDeadzone(const float value, const float sensitivityFactor)
        {
            const float adjValue = value * sensitivityFactor;
            const float deadZone = DEADZONE * sensitivityFactor;
            if (std::fabs(adjValue) < deadZone) {
                return 0;
            }
            return adjValue > 0 ? adjValue - deadZone : adjValue + deadZone;
        }

        /**
         * True iff any of the four primary/offhand stick axes is outside the deadzone.
         */
        bool anyStickInput(const float px, const float py, const float sx, const float sy)
        {
            return applyDeadzone(px, 0.5f) != 0.0f || applyDeadzone(py, 0.5f) != 0.0f || applyDeadzone(sx, 0.5f) != 0.0f || applyDeadzone(sy, 0.5f) != 0.0f;
        }

        /**
         * Parse a HapticSegment sequence from a debug text field (dev-only haptic-pattern testing).
         * Format: ';'-separated segments, each "duration,startIntensity,endIntensity" (seconds and
         * 0..1 intensities), e.g. "0.4,0.1,1.0; 0.1,0,0; 0.05,1,1". Whitespace around separators is
         * allowed. Non-empty segments that don't parse are skipped with a warning; the function
         * returns whatever parsed cleanly.
         */
        std::vector<vrcf::HapticSegment> parseHapticSegments(const std::string& text)
        {
            std::vector<vrcf::HapticSegment> segments;
            std::size_t pos = 0;
            while (pos <= text.size()) {
                const std::size_t sep = text.find(';', pos);
                const std::string token = text.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
                vrcf::HapticSegment seg{};
                if (std::sscanf(token.c_str(), " %f , %f , %f", &seg.duration, &seg.startIntensity, &seg.endIntensity) == 3) {
                    segments.push_back(seg);
                } else if (token.find_first_not_of(" \t") != std::string::npos) {
                    logger::warn("DebugAdjuster: malformed haptic segment '{}' (expected 'duration,startIntensity,endIntensity')", token);
                }
                if (sep == std::string::npos) {
                    break;
                }
                pos = sep + 1;
            }
            return segments;
        }
    }

    /**
     * Per-frame entry point. Disabled cheaply when target == None; otherwise dispatches to the
     * matching adjust routine and handles the save/reload Primary-A bindings with haptic feedback.
     * While active, the player's controllers are disabled using VRControllersSuppress so the player
     * can't move or do anything while adjusting.
     */
    void DebugAdjuster::onFrameUpdate(ConfigBase& config)
    {
        const bool active = config.debugAdjustTarget != DebugAdjustTarget::None;

        // Idempotent and self-restoring: re-enabled / released the moment the adjuster is turned off.
        vrcf::VRControllersSuppress.setAllSuppressed("DebugAdjuster", active);
        if (!active) {
            return;
        }

        switch (config.debugAdjustTarget) {
        case DebugAdjustTarget::None:
            break; // unreachable, handled by the active guard above
        case DebugAdjustTarget::Transform:
            adjustTransform(config.debugTransform);
            break;
        case DebugAdjustTarget::HandPose:
            adjustHandPose(config.debugHandPose);
            break;
        case DebugAdjustTarget::FlowFlag1:
            adjustFloat(config.debugFlowFlag1);
            break;
        case DebugAdjustTarget::FlowFlag2:
            adjustFloat(config.debugFlowFlag2);
            break;
        case DebugAdjustTarget::FlowFlag3:
            adjustFloat(config.debugFlowFlag3);
            break;
        case DebugAdjustTarget::FlowFlag123:
            adjustFloat3(config.debugFlowFlag1, config.debugFlowFlag2, config.debugFlowFlag3);
            break;
        case DebugAdjustTarget::HapticTest:
            // Owns Primary-A as the play trigger, so it bypasses the shared save/reload bindings below.
            adjustHapticTest(config);
            return;
        }

        // long-press is checked before tap because isLongPressed clears state when fired;
        // otherwise both would trigger on the same release.
        if (vrcf::VRControllers.isTap(vrcf::Hand::Primary, vr::k_EButton_A)) {
            saveCurrent(config);
            vrcf::VRHaptics.trigger(vrcf::Hand::Primary, vrcf::HapticPattern::Success);
        } else if (vrcf::VRControllers.isLongPressed(vrcf::Hand::Primary, vr::k_EButton_A)) {
            reloadFromIni(config);
            vrcf::VRHaptics.trigger(vrcf::Hand::Primary, vrcf::HapticPattern::Warning);
        }
    }

    /**
     * Mutates the given transform based on thumbstick input and Offhand modifier buttons.
     * Holding Offhand-Grip routes the sticks into rotation, Offhand-A into scale, otherwise
     * into translation. Early-outs when no stick is active to avoid spurious matrix work.
     */
    void DebugAdjuster::adjustTransform(RE::NiTransform& transform)
    {
        const auto [px, py] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Primary);
        const auto [sx, sy] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Offhand);
        if (!anyStickInput(px, py, sx, sy)) {
            return;
        }

        if (vrcf::VRControllers.isPressHeldDown(vrcf::Hand::Offhand, vr::k_EButton_Grip)) {
            // rotate: primary Y -> pitch (heading), primary X -> yaw (attitude), secondary X -> roll
            const auto delta = common::MatrixUtils::getMatrixFromEulerAnglesDegrees(-applyDeadzone(py, ROTATE_DEGREES_PER_FRAME),
                applyDeadzone(sx, ROTATE_DEGREES_PER_FRAME),
                applyDeadzone(px, ROTATE_DEGREES_PER_FRAME));
            transform.rotate = delta * transform.rotate;
        } else if (vrcf::VRControllers.isPressHeldDown(vrcf::Hand::Offhand, vr::k_EButton_A)) {
            // scale: primary Y, clamped to a sane minimum
            transform.scale = std::fmax(0.05f, transform.scale + applyDeadzone(py, SCALE_PER_FRAME));
        } else {
            // translate: primary stick -> XY, secondary Y -> Z
            transform.translate.x += std::clamp(applyDeadzone(py, TRANSLATE_PER_FRAME), -360.0f, 360.0f);
            transform.translate.y += std::clamp(applyDeadzone(px, TRANSLATE_PER_FRAME), -360.0f, 360.0f);
            transform.translate.z += std::clamp(applyDeadzone(sy, TRANSLATE_PER_FRAME), -360.0f, 360.0f);
        }
    }

    /**
     * Mutates the 22-float hand pose using a slot-based scheme. Only one slot is active at a time:
     * slots 0..4 = fingers (thumb,index,middle,ring,pinky), each editing 4 contiguous floats
     * (prox,mid,dist,splay) mapped to the 4 stick axes; slot 5 = palm, editing the trailing 2
     * floats (palmPitch, palmYaw) on the primary stick. Offhand-A short-release advances the slot
     * with wraparound, fires a small haptic, and surfaces an in-game notification naming the new
     * slot.
     */
    void DebugAdjuster::adjustHandPose(std::array<float, 22>& pose)
    {
        if (vrcf::VRControllers.isTap(vrcf::Hand::Offhand, vr::k_EButton_A)) {
            s_handPoseSlot = (s_handPoseSlot + 1) % HAND_POSE_SLOT_NAMES.size();
            f4vr::showNotification(fmt::format("Adjusting hand pose: {}", HAND_POSE_SLOT_NAMES[s_handPoseSlot]));
            vrcf::VRHaptics.trigger(vrcf::Hand::Offhand, vrcf::HapticPattern::Tick);
        }

        const auto [px, py] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Primary);
        const auto [sx, sy] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Offhand);
        if (!anyStickInput(px, py, sx, sy)) {
            return;
        }

        if (s_handPoseSlot < 5) {
            // finger slot: 4 contiguous floats (prox, mid, dist, splay) at base index slot*4
            const std::size_t base = s_handPoseSlot * 4;
            if (vrcf::VRControllers.isPressHeldDown(vrcf::Hand::Offhand, vr::k_EButton_Grip)) {
                pose[base + 3] = std::clamp(pose[base + 3] + applyDeadzone(py, HAND_POSE_FINGER_PER_FRAME), -0.8f, 2.0f);
            } else {
                pose[base + 0] = std::clamp(pose[base + 0] + applyDeadzone(py, HAND_POSE_FINGER_PER_FRAME), -0.8f, 2.0f);
                pose[base + 1] = std::clamp(pose[base + 1] + applyDeadzone(px, HAND_POSE_FINGER_PER_FRAME), -0.8f, 2.0f);
                pose[base + 2] = std::clamp(pose[base + 2] + applyDeadzone(sy, HAND_POSE_FINGER_PER_FRAME), -0.8f, 2.0f);
            }
        } else {
            // palm slot: trailing 2 floats (palmPitch, palmYaw) on the primary stick
            pose[20] = std::clamp(pose[20] + applyDeadzone(py, HAND_POSE_PALM_PER_FRAME), -10.0f, 15.0f);
            pose[21] = std::clamp(pose[21] + applyDeadzone(px, HAND_POSE_PALM_PER_FRAME), -10.0f, 15.0f);
        }
    }

    /**
     * Bumps a float by the primary stick Y, scaled by the per-frame sensitivity.
     */
    void DebugAdjuster::adjustFloat(float& value)
    {
        const auto [px, py] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Primary);
        const float pyd = applyDeadzone(py, FLOW_FLAG_PER_FRAME);
        if (pyd == 0.0f) {
            return;
        }
        value += pyd * FLOW_FLAG_PER_FRAME;
    }

    /**
     * Adjusts all three flow flags simultaneously: primary X -> flag1, primary Y -> flag2,
     * secondary Y -> flag3. Lets the user tune three related values in one session.
     */
    void DebugAdjuster::adjustFloat3(float& flag1, float& flag2, float& flag3)
    {
        const auto [px, py] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Primary);
        const auto [sx, sy] = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Offhand);
        if (!anyStickInput(px, py, sx, sy)) {
            return;
        }
        flag1 += applyDeadzone(py, FLOW_FLAG_PER_FRAME);
        flag2 += applyDeadzone(px, FLOW_FLAG_PER_FRAME);
        flag3 += applyDeadzone(sy, FLOW_FLAG_PER_FRAME);
    }

    /**
     * Dev-only haptic-pattern tester. On Primary-A tap, parses a HapticSegment sequence from the
     * config's debugFlowText1 (sDebugFlowText1) and plays it on the primary controller. Combined
     * with INI hot-reload this lets the user iterate on custom patterns: edit sDebugFlowText1, let
     * it reload, tap A to feel the result, repeat. No-op (with a warning) when nothing parses.
     */
    void DebugAdjuster::adjustHapticTest(const ConfigBase& config)
    {
        if (!vrcf::VRControllers.isTap(vrcf::Hand::Primary, vr::k_EButton_A)) {
            return;
        }
        const auto segments = parseHapticSegments(config.debugFlowText1);
        if (segments.empty()) {
            logger::warn("DebugAdjuster: no valid haptic segments in sDebugFlowText1='{}'", config.debugFlowText1);
            return;
        }
        logger::info("DebugAdjuster: playing {} haptic segment(s) from sDebugFlowText1", segments.size());
        vrcf::VRHaptics.trigger(vrcf::Hand::Primary, segments);
    }

    /**
     * Writes the current in-memory value(s) for the active target back to the INI file.
     * FlowFlag123 saves all three keys; the single-target cases save one key each.
     */
    void DebugAdjuster::saveCurrent(const ConfigBase& config)
    {
        // const_cast is fine: ConfigBase exposes saveIniConfigValue as non-const because it touches
        // file state and watcher flags, but the in-memory config values we're saving are not mutated.
        auto& mutableConfig = const_cast<ConfigBase&>(config);
        switch (config.debugAdjustTarget) {
        case DebugAdjustTarget::Transform:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "tDebugTransform", transformToFixedString(config.debugTransform).c_str());
            logger::info("DebugAdjuster: saved tDebugTransform to INI");
            break;
        case DebugAdjustTarget::HandPose:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "hDebugHandPose", handPoseToFixedString(config.debugHandPose).c_str());
            logger::info("DebugAdjuster: saved hDebugHandPose to INI");
            break;
        case DebugAdjustTarget::FlowFlag1:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "fDebugFlowFlag1", toFixed(config.debugFlowFlag1).c_str());
            logger::info("DebugAdjuster: saved fDebugFlowFlag1 to INI");
            break;
        case DebugAdjustTarget::FlowFlag2:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "fDebugFlowFlag2", toFixed(config.debugFlowFlag2).c_str());
            logger::info("DebugAdjuster: saved fDebugFlowFlag2 to INI");
            break;
        case DebugAdjustTarget::FlowFlag3:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "fDebugFlowFlag3", toFixed(config.debugFlowFlag3).c_str());
            logger::info("DebugAdjuster: saved fDebugFlowFlag3 to INI");
            break;
        case DebugAdjustTarget::FlowFlag123:
            mutableConfig.saveIniConfigValues(INI_SECTION_DEBUG,
                {
                    { "fDebugFlowFlag1", toFixed(config.debugFlowFlag1) },
                    { "fDebugFlowFlag2", toFixed(config.debugFlowFlag2) },
                    { "fDebugFlowFlag3", toFixed(config.debugFlowFlag3) },
                });
            logger::info("DebugAdjuster: saved fDebugFlowFlag1/2/3 to INI");
            break;
        case DebugAdjustTarget::None:
            break;
        }
    }

    /**
     * Reverts unsaved changes by re-reading the on-disk INI.
     */
    void DebugAdjuster::reloadFromIni(ConfigBase& config)
    {
        config.reload();
        logger::info("DebugAdjuster: reloaded from INI");
    }
}
