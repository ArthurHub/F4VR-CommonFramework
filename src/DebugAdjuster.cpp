#include "DebugAdjuster.h"

#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRThumbstickControls.h"
#include "vrcf/VRControllersManager.h"

namespace f4cf
{
    namespace
    {
        constexpr float DEADZONE = 0.5f;
        constexpr float TRANSLATE_PER_FRAME = 0.3f;
        constexpr float ROTATE_DEGREES_PER_FRAME = 0.5f;
        constexpr float SCALE_PER_FRAME = 0.005f;
        constexpr float FLOW_FLAG_PER_FRAME = 0.03f;

        // Saved debug values are rounded to this many decimals to keep the INI readable.
        constexpr int SAVE_PRECISION = 2;

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
                toFixed(transform.translate.x), toFixed(transform.translate.y), toFixed(transform.translate.z),
                toFixed(heading), toFixed(roll), toFixed(attitude), toFixed(transform.scale));
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
            return applyDeadzone(px, 0.5f) != 0.0f || applyDeadzone(py, 0.5f) != 0.0f
                || applyDeadzone(sx, 0.5f) != 0.0f || applyDeadzone(sy, 0.5f) != 0.0f;
        }
    }

    /**
     * Per-frame entry point. Disabled cheaply when target == None; otherwise dispatches to the
     * matching adjust routine and handles the save/reload Primary-A bindings with haptic feedback.
     * While active, the player's movement thumbstick is disabled so adjusting doesn't move/turn
     * the player; it is restored as soon as the target returns to None. Note this does not block
     * the Primary-A button from the game (e.g. Pip-Boy/VATS) - that needs an OpenVR input hook.
     */
    void DebugAdjuster::onFrameUpdate(ConfigBase& config)
    {
        const bool active = config.debugAdjustTarget != DebugAdjustTarget::None;

        // Idempotent and self-restoring: re-enabled the moment the adjuster is turned off.
        f4vr::F4VRThumbstickControls::setControlsThumbstickEnableState(!active);
        if (!active) {
            return;
        }

        switch (config.debugAdjustTarget) {
        case DebugAdjustTarget::None:
            break; // unreachable, handled by the active guard above
        case DebugAdjustTarget::Transform:
            adjustTransform(config.debugTransform);
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
        }

        // long-press is checked before short-release because isLongPressed clears state when fired;
        // otherwise both would trigger on the same release.
        if (vrcf::VRControllers.isReleasedShort(vrcf::Hand::Primary, vr::k_EButton_A)) {
            saveCurrent(config);
            vrcf::VRControllers.triggerHaptic(vrcf::Hand::Primary, 0.1f, 0.4f);
        } else if (vrcf::VRControllers.isLongPressed(vrcf::Hand::Primary, vr::k_EButton_A)) {
            reloadFromIni(config);
            vrcf::VRControllers.triggerHaptic(vrcf::Hand::Primary, 0.25f, 0.6f);
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
            const auto delta = common::MatrixUtils::getMatrixFromEulerAnglesDegrees(
                -applyDeadzone(py, ROTATE_DEGREES_PER_FRAME),
                applyDeadzone(sx, ROTATE_DEGREES_PER_FRAME),
                applyDeadzone(px, ROTATE_DEGREES_PER_FRAME));
            transform.rotate = delta * transform.rotate;
        } else if (vrcf::VRControllers.isPressHeldDown(vrcf::Hand::Offhand, vr::k_EButton_A)) {
            // scale: primary Y, clamped to a sane minimum
            transform.scale = std::fmax(0.05f, transform.scale + applyDeadzone(py, SCALE_PER_FRAME));
        } else {
            // translate: primary stick -> XY, secondary Y -> Z
            transform.translate.x += applyDeadzone(py, TRANSLATE_PER_FRAME);
            transform.translate.y += applyDeadzone(px, TRANSLATE_PER_FRAME);
            transform.translate.z += applyDeadzone(sy, TRANSLATE_PER_FRAME);
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
            mutableConfig.saveIniConfigValues(INI_SECTION_DEBUG, {
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
