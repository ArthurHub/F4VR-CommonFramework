#include "FrameSampler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

#include "../../external/openvr/openvr.h"
#include "FrameContext.h"
#include "Perf.h"

namespace f4cf::perf::internal
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // well inside the compositor's history at any refresh rate, so no frame falls out of it between reads
        constexpr auto POLL_INTERVAL = std::chrono::milliseconds(500);

        // the frames the compositor keeps; asking further back returns its oldest one again
        constexpr std::uint32_t HISTORY_FRAMES = 128;

        // the newest frames' GPU times may not be in yet, so they wait for the next read
        constexpr std::uint32_t SETTLING_FRAMES = 2;

        /**
         * Where the compositor writes one frame's timing. Our header's Compositor_FrameTiming is older and smaller than
         * the runtime's, and a runtime that writes its own size whatever m_nSize says would run past the struct: the
         * slack takes that. The runtime's struct only grows at the end, so the fields read here stay where they are.
         * GetFrameTimings is not used: an array of these it filled ran past its end into the plugin's globals.
         */
        struct FrameTimingBuffer
        {
            vr::Compositor_FrameTiming timing{ .m_nSize = sizeof(vr::Compositor_FrameTiming) };
            std::array<std::byte, 256> slack{};
        };

        /**
         * The compositor's timing of the frame so many frames ago, or nothing when it has none.
         */
        std::optional<vr::Compositor_FrameTiming> frameTiming(vr::IVRCompositor& compositor, const std::uint32_t framesAgo)
        {
            FrameTimingBuffer buffer;
            if (!compositor.GetFrameTiming(&buffer.timing, framesAgo)) {
                return std::nullopt;
            }
            return buffer.timing;
        }

        CompositorFrame toCompositorFrame(const vr::Compositor_FrameTiming& timing)
        {
            return {
                .gpuMs = timing.m_flTotalRenderGpuMs,
                .gameGpuMs = timing.m_flPreSubmitGpuMs,
                .compositorGpuMs = timing.m_flCompositorRenderGpuMs,
                .lateStartMs = timing.m_flWaitGetPosesCalledMs - timing.m_flNewPosesReadyMs,
                .headroomMs = timing.m_flCompositorIdleCpuMs,
                .reprojectedCpu = (timing.m_nReprojectionFlags & vr::VRCompositor_ReprojectionReason_Cpu) != 0,
                .reprojectedGpu = (timing.m_nReprojectionFlags & vr::VRCompositor_ReprojectionReason_Gpu) != 0,
                .dropped = timing.m_nNumDroppedFrames,
                .misPresented = timing.m_nNumMisPresented,
            };
        }

        /**
         * Reads the compositor's frame timings into the frame context, each frame once, only frames timed in the
         * current window.
         */
        class CompositorPoller
        {
        public:
            void poll(const Clock::time_point now)
            {
                auto* compositor = vr::VRCompositor();
                if (!compositor) {
                    return;
                }
                const auto window = windowStart();
                if (!_started || window != _window) {
                    startWindow(*compositor, window, now);
                    return;
                }
                if (now < _nextPoll) {
                    return;
                }
                _nextPoll = now + POLL_INTERVAL;

                // newest first, one frame at a time, back to the last frame already recorded
                const auto newest = frameTiming(*compositor, 0);
                if (!newest) {
                    return;
                }
                auto newerIndex = newest->m_nFrameIndex;
                auto latestRecorded = _lastFrame;
                for (auto framesAgo = SETTLING_FRAMES; framesAgo < HISTORY_FRAMES; ++framesAgo) {
                    const auto timing = frameTiming(*compositor, framesAgo);
                    // an index that stops going down is the history's end, where the oldest frame repeats
                    if (!timing || timing->m_nFrameIndex <= _lastFrame || timing->m_nFrameIndex >= newerIndex) {
                        break;
                    }
                    newerIndex = timing->m_nFrameIndex;
                    recordCompositorFrame(toCompositorFrame(*timing));
                    latestRecorded = (std::max)(latestRecorded, timing->m_nFrameIndex);
                }
                _lastFrame = latestRecorded;
            }

        private:
            /**
             * A new window (recording switched on, or a reset): skip the frames timed before it, and read the refresh
             * rate again in case it changed.
             */
            void startWindow(vr::IVRCompositor& compositor, const Clock::time_point window, const Clock::time_point now)
            {
                _started = true;
                _window = window;
                _nextPoll = now + POLL_INTERVAL;
                if (const auto newest = frameTiming(compositor, 0)) {
                    _lastFrame = newest->m_nFrameIndex;
                }
                if (auto* system = vr::VRSystem()) {
                    setDisplayHz(system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float));
                }
            }

            bool _started = false;
            Clock::time_point _window{};
            Clock::time_point _nextPoll{};
            std::uint32_t _lastFrame = 0;
        };
    }

    void sampleFrame()
    {
        static bool previousFrameRecorded = false;
        static Clock::time_point previousFrame{};
        if (!isEnabled()) {
            previousFrameRecorded = false;
            return;
        }
        const auto now = Clock::now();
        // an interval only between two frames that both recorded, so switching recording on doesn't add one spanning
        // the whole time it was off
        if (previousFrameRecorded) {
            recordFrameInterval(now - previousFrame);
        }
        previousFrame = now;
        previousFrameRecorded = true;

        static CompositorPoller compositor;
        compositor.poll(now);
    }
}
