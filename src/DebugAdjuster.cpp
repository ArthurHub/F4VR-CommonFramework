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

        // Active "Section::Key" field target state for DebugAdjustTarget::Field. (Re)loaded from the
        // INI whenever the field string changes; mutated in place each frame. s_fieldType is the
        // value kind inferred from the key's first letter ('t' transform, 'h' hand pose, 'f' float),
        // or 0 when nothing valid is loaded. s_fieldRaw mirrors the last-seen debug.adjustField so the
        // (potentially failing) parse + seed read + logging happens once per change, not every frame.
        std::string s_fieldRaw;
        std::string s_fieldSection;
        std::string s_fieldKey;
        char s_fieldType = 0;
        RE::NiTransform s_fieldTransform{};
        std::array<float, 22> s_fieldPose{};
        float s_fieldFloat = 0.0f;

        /**
         * Lowercase a single ASCII letter (used to make the field key-prefix match case-insensitive).
         */
        char toLowerAscii(const char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        /**
         * Trim leading/trailing spaces and tabs.
         */
        std::string trim(const std::string& s)
        {
            const auto begin = s.find_first_not_of(" \t");
            if (begin == std::string::npos) {
                return "";
            }
            return s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
        }

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
         * Serialize the active field's working value to its INI string form (empty when unloaded).
         */
        std::string fieldToString()
        {
            switch (s_fieldType) {
            case 't':
                return transformToFixedString(s_fieldTransform);
            case 'h':
                return handPoseToFixedString(s_fieldPose);
            case 'f':
                return toFixed(s_fieldFloat);
            default:
                return "";
            }
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
        const bool active = config.debug.adjustTarget != DebugAdjustTarget::None;

        // Idempotent and self-restoring: re-enabled / released the moment the adjuster is turned off.
        vrcf::VRControllersSuppress.setAllSuppressed("DebugAdjuster", active);
        if (!active) {
            // Forget the active field so re-activating re-seeds the working value from disk.
            s_fieldRaw.clear();
            s_fieldType = 0;
            return;
        }

        switch (config.debug.adjustTarget) {
        case DebugAdjustTarget::None:
            break; // unreachable, handled by the active guard above
        case DebugAdjustTarget::Transform:
            adjustTransform(config.debug.transform);
            break;
        case DebugAdjustTarget::HandPose:
            adjustHandPose(config.debug.handPose);
            break;
        case DebugAdjustTarget::FlowFlag1:
            adjustFloat(config.debug.flowFlag1);
            break;
        case DebugAdjustTarget::FlowFlag2:
            adjustFloat(config.debug.flowFlag2);
            break;
        case DebugAdjustTarget::FlowFlag3:
            adjustFloat(config.debug.flowFlag3);
            break;
        case DebugAdjustTarget::FlowFlag123:
            adjustFloat3(config.debug.flowFlag1, config.debug.flowFlag2, config.debug.flowFlag3);
            break;
        case DebugAdjustTarget::HapticTest:
            // Owns Primary-A as the play trigger, so it bypasses the shared save/reload bindings below.
            adjustHapticTest(config);
            return;
        case DebugAdjustTarget::Field:
            adjustField(config);
            break;
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
     * config's debug.flowText1 (sFlowText1) and plays it on the primary controller. Combined
     * with INI hot-reload this lets the user iterate on custom patterns: edit sFlowText1, let
     * it reload, tap A to feel the result, repeat. No-op (with a warning) when nothing parses.
     */
    void DebugAdjuster::adjustHapticTest(const ConfigBase& config)
    {
        if (!vrcf::VRControllers.isTap(vrcf::Hand::Primary, vr::k_EButton_A)) {
            return;
        }
        const auto segments = parseHapticSegments(config.debug.flowText1);
        if (segments.empty()) {
            logger::warn("DebugAdjuster: no valid haptic segments in sFlowText1='{}'", config.debug.flowText1);
            return;
        }
        logger::info("DebugAdjuster: playing {} haptic segment(s) from sFlowText1", segments.size());
        vrcf::VRHaptics.trigger(vrcf::Hand::Primary, segments);
    }

    /**
     * Resolve config.debug.adjustField ("Section::Key") and, when it changes, seed the working value
     * from the on-disk INI. The value kind is inferred from the key's first letter (t=transform,
     * h=hand pose, f=float). Returns true once a supported field is loaded. The parse/seed/log only
     * runs when the field string changes, so an invalid field doesn't spam the log every frame.
     */
    bool DebugAdjuster::loadField(const ConfigBase& config)
    {
        if (config.debug.adjustField == s_fieldRaw) {
            return s_fieldType != 0;
        }
        s_fieldRaw = config.debug.adjustField;
        s_fieldType = 0;

        const auto sep = s_fieldRaw.find("::");
        const std::string section = sep == std::string::npos ? "" : trim(s_fieldRaw.substr(0, sep));
        const std::string key = sep == std::string::npos ? "" : trim(s_fieldRaw.substr(sep + 2));
        if (section.empty() || key.empty()) {
            logger::warn("DebugAdjuster: field target '{}' must be 'Section::Key'", s_fieldRaw);
            return false;
        }

        const char type = toLowerAscii(key[0]);
        switch (type) {
        case 't':
            s_fieldTransform = config.readIniTransformValue(section.c_str(), key.c_str(), common::MatrixUtils::getTransform(0, 0, 0, 0, 0, 0));
            break;
        case 'h':
            s_fieldPose = config.readIniHandPoseValue(section.c_str(), key.c_str(), {});
            break;
        case 'f':
            s_fieldFloat = config.readIniFloatValue(section.c_str(), key.c_str(), 0.0f);
            break;
        default:
            logger::warn("DebugAdjuster: unsupported field '{}' (key must start with t/h/f for transform/hand pose/float)", s_fieldRaw);
            return false;
        }

        s_fieldSection = section;
        s_fieldKey = key;
        s_fieldType = type;
        f4vr::showNotification(fmt::format("Adjusting field: {}", s_fieldRaw));
        logger::info("DebugAdjuster: editing field '{}'", s_fieldRaw);
        return true;
    }

    /**
     * Adjust an arbitrary INI field referenced by config.debug.adjustField using the same input map as
     * the matching fixed target (transform/hand pose/float). The mutated working value is pushed into
     * the running config in-memory each frame it changes (no disk write) so the effect is live; the
     * explicit Primary-A save writes it to that key, and a reload re-seeds it from disk.
     */
    void DebugAdjuster::adjustField(ConfigBase& config)
    {
        if (!loadField(config)) {
            return;
        }

        const std::string before = fieldToString();
        switch (s_fieldType) {
        case 't':
            adjustTransform(s_fieldTransform);
            break;
        case 'h':
            adjustHandPose(s_fieldPose);
            break;
        case 'f':
            adjustFloat(s_fieldFloat);
            break;
        default:
            return;
        }

        const std::string after = fieldToString();
        if (after != before) {
            config.applyIniConfigWithOverride(s_fieldSection.c_str(), s_fieldKey.c_str(), after.c_str());
        }
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
        switch (config.debug.adjustTarget) {
        case DebugAdjustTarget::Transform:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "tTransform", transformToFixedString(config.debug.transform).c_str());
            logger::info("DebugAdjuster: saved tTransform to INI");
            break;
        case DebugAdjustTarget::HandPose:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "hHandPose", handPoseToFixedString(config.debug.handPose).c_str());
            logger::info("DebugAdjuster: saved hHandPose to INI");
            break;
        case DebugAdjustTarget::FlowFlag1:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "fFlowFlag1", toFixed(config.debug.flowFlag1).c_str());
            logger::info("DebugAdjuster: saved fFlowFlag1 to INI");
            break;
        case DebugAdjustTarget::FlowFlag2:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "fFlowFlag2", toFixed(config.debug.flowFlag2).c_str());
            logger::info("DebugAdjuster: saved fFlowFlag2 to INI");
            break;
        case DebugAdjustTarget::FlowFlag3:
            mutableConfig.saveIniConfigValue(INI_SECTION_DEBUG, "fFlowFlag3", toFixed(config.debug.flowFlag3).c_str());
            logger::info("DebugAdjuster: saved fFlowFlag3 to INI");
            break;
        case DebugAdjustTarget::FlowFlag123:
            mutableConfig.saveIniConfigValues(INI_SECTION_DEBUG,
                {
                    { "fFlowFlag1", toFixed(config.debug.flowFlag1) },
                    { "fFlowFlag2", toFixed(config.debug.flowFlag2) },
                    { "fFlowFlag3", toFixed(config.debug.flowFlag3) },
                });
            logger::info("DebugAdjuster: saved fFlowFlag1/2/3 to INI");
            break;
        case DebugAdjustTarget::Field:
            if (s_fieldType != 0) {
                mutableConfig.saveIniConfigValue(s_fieldSection.c_str(), s_fieldKey.c_str(), fieldToString().c_str());
                logger::info("DebugAdjuster: saved {}::{} to INI", s_fieldSection, s_fieldKey);
            }
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
        // Drop the field working value so it re-seeds from disk next frame, discarding unsaved edits.
        s_fieldRaw.clear();
        s_fieldType = 0;
        logger::info("DebugAdjuster: reloaded from INI");
    }
}
