#pragma once

#include "ConfigBase.h"

namespace f4cf
{
    /**
     * In-game adjuster for the ConfigBase debug fields. When the active config's
     * `debugAdjustTarget` is not None, controller input mutates the corresponding
     * value live; Primary-A short-tap saves to INI, Primary-A long-press reloads.
     *
     * Input map (when target == Transform):
     *   - Offhand-Grip held: rotate (primary stick Y=pitch X=yaw, secondary X=roll)
     *   - Offhand-A held:    scale (primary stick Y)
     *   - Otherwise:         translate (primary X=Y-axis, Y=X-axis, secondary Y=Z-axis)
     *
     * Input map (when target == HandPose): one finger (or the palm) is active at a time.
     *   - Active finger (4 axes): primary X = prox, primary Y = mid, secondary X = dist, secondary Y = splay
     *   - Active palm   (2 axes): primary X = palmPitch, primary Y = palmYaw
     *   - Offhand-A short-release: advance to the next slot (thumb -> index -> middle -> ring ->
     *     pinky -> palm -> thumb...), fires a haptic and an in-game notification with the new slot.
     *
     * Input map (when target == FlowFlag1/2/3): primary stick Y adjusts the value.
     *
     * Input map (when target == FlowFlag123): primary X = flag1, primary Y = flag2,
     * secondary Y = flag3 — all three editable at once.
     *
     * Owned and driven by ModBase; ModBase calls `onFrameUpdate` each frame with its own config.
     */
    class DebugAdjuster
    {
    public:
        static void onFrameUpdate(ConfigBase& config);

    private:
        static void adjustTransform(RE::NiTransform& transform);
        static void adjustHandPose(std::array<float, 22>& pose);
        static void adjustFloat(float& value);
        static void adjustFloat3(float& flag1, float& flag2, float& flag3);
        static void saveCurrent(const ConfigBase& config);
        static void reloadFromIni(ConfigBase& config);
    };
}
