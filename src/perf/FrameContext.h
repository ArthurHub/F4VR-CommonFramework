#pragma once

#include <chrono>
#include <cstdint>

#include "Histogram.h"

namespace f4cf::perf
{
    /**
     * One frame as the VR compositor timed it: the fields of OpenVR's Compositor_FrameTiming that perf keeps, copied
     * out so this module stays free of OpenVR.
     */
    struct CompositorFrame
    {
        // the whole GPU frame, from the work submitted after the last present to the end of the compositor's
        // (m_flTotalRenderGpuMs)
        float gpuMs = 0;
        // the game's scene, between WaitGetPoses and the second Submit (m_flPreSubmitGpuMs)
        float gameGpuMs = 0;
        // distortion, chaperone, overlays (m_flCompositorRenderGpuMs)
        float compositorGpuMs = 0;
        // how late the game asked for poses after they were ready, the CPU-bound signal
        // (m_flWaitGetPosesCalledMs - m_flNewPosesReadyMs); negative when early, recorded as 0
        float lateStartMs = 0;
        // time the compositor idled waiting for the frame's running start, which the game could have used
        // (m_flCompositorIdleCpuMs)
        float headroomMs = 0;
        // the runtime reprojected this frame because the CPU or the GPU was late (m_nReprojectionFlags)
        bool reprojectedCpu = false;
        bool reprojectedGpu = false;
        // extra times the previous frame was scanned out (m_nNumDroppedFrames)
        std::uint32_t dropped = 0;
        // times this frame was presented on a vsync other than the one it was predicted for (m_nNumMisPresented)
        std::uint32_t misPresented = 0;
    };

    /**
     * What the whole frame looked like over the window: the game thread's frame interval, and when the game runs in VR,
     * the compositor's timing of the same frames. A mod's own sites read against it: the same 0.3 ms matters when the
     * game is short of its budget, and much less when it has headroom.
     */
    struct FrameContext
    {
        // the headset's refresh rate; 0 when unknown (flat, or OpenVR not up yet)
        float displayHz = 0;
        // between the starts of consecutive game frames, as ModBase sees them
        Histogram::Snapshot interval;

        // the compositor's frames in the window, and what each one's numbers are made of; all empty without VR
        std::uint64_t compositorFrames = 0;
        Histogram::Snapshot gpu;
        Histogram::Snapshot gameGpu;
        Histogram::Snapshot compositorGpu;
        Histogram::Snapshot lateStart;
        Histogram::Snapshot headroom;
        std::uint64_t reprojectedCpu = 0;
        std::uint64_t reprojectedGpu = 0;
        std::uint64_t dropped = 0;
        std::uint64_t misPresented = 0;

        /**
         * One frame's time at the refresh rate, in ms; 0 when the rate is unknown.
         */
        [[nodiscard]] double budgetMs() const
        {
            return displayHz > 0 ? 1000.0 / static_cast<double>(displayHz) : 0.0;
        }
    };

    /**
     * Record the time since the previous game frame started. ModBase calls it once per frame while recording is on.
     * Any thread.
     */
    void recordFrameInterval(std::chrono::nanoseconds interval);

    /**
     * Record one frame the compositor timed. ModBase's poll calls it for each new frame while recording is on. Any thread.
     */
    void recordCompositorFrame(const CompositorFrame& frame);

    /**
     * The headset's refresh rate, which the budget is derived from. Kept across resets. Any thread.
     */
    void setDisplayHz(float hz);

    /**
     * Everything recorded since the last reset, leaving it in place. perf::reset() also resets this. Any thread.
     */
    [[nodiscard]] FrameContext readFrameContext();

    namespace internal
    {
        /**
         * Drop everything recorded but the refresh rate; part of perf::reset().
         */
        void resetFrameContext();
    }
}
