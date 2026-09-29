#include "FrameSampler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

#include "../../external/openvr/openvr.h"
#include "FrameContext.h"
#include "Perf.h"
#include "Tracy.h"

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
         * One compositor frame on the Tracy viewer's plots.
         */
        void plotCompositorFrame(const CompositorFrame& frame)
        {
            tracyPlot("vr.gpuMs", frame.gpuMs);
            tracyPlot("vr.gameGpuMs", frame.gameGpuMs);
            tracyPlot("vr.lateStartMs", frame.lateStartMs);
            tracyPlot("vr.headroomMs", frame.headroomMs);
            tracyPlot("vr.reprojectedCpu", frame.reprojectedCpu ? 1.0 : 0.0);
            tracyPlot("vr.reprojectedGpu", frame.reprojectedGpu ? 1.0 : 0.0);
            tracyPlot("vr.dropped", frame.dropped);
        }

        /**
         * Reads the compositor's frame timings, each frame once, only frames timed in the current window: into the
         * frame context while recording, onto the Tracy plots while a viewer is connected.
         */
        class CompositorPoller
        {
        public:
            /**
             * @param resumed nothing was sampled last frame, so the frames timed since the last read belong to no
             *                window, and would otherwise all arrive at once (on a viewer's plots, as one burst)
             */
            void poll(const Clock::time_point now, const bool recording, const bool plotting, const bool resumed)
            {
                auto* compositor = vr::VRCompositor();
                if (!compositor) {
                    return;
                }
                const auto window = windowStart();
                if (!_started || window != _window || resumed) {
                    startWindow(*compositor, window, now);
                    return;
                }
                // every frame while plotting, so each plot point lands by its frame on the timeline, not in a burst
                if (!plotting && now < _nextPoll) {
                    return;
                }
                _nextPoll = now + POLL_INTERVAL;

                // newest first, one frame at a time, back to the last frame already read
                const auto newest = frameTiming(*compositor, 0);
                if (!newest) {
                    return;
                }
                std::array<CompositorFrame, HISTORY_FRAMES> frames;
                std::size_t count = 0;
                auto newerIndex = newest->m_nFrameIndex;
                auto latestRead = _lastFrame;
                for (auto framesAgo = SETTLING_FRAMES; framesAgo < HISTORY_FRAMES; ++framesAgo) {
                    const auto timing = frameTiming(*compositor, framesAgo);
                    // an index that stops going down is the history's end, where the oldest frame repeats
                    if (!timing || timing->m_nFrameIndex <= _lastFrame || timing->m_nFrameIndex >= newerIndex) {
                        break;
                    }
                    newerIndex = timing->m_nFrameIndex;
                    frames[count++] = toCompositorFrame(*timing);
                    latestRead = (std::max)(latestRead, timing->m_nFrameIndex);
                }
                _lastFrame = latestRead;

                // oldest first, so the plots run forward in time
                for (auto i = count; i-- > 0;) {
                    if (recording) {
                        recordCompositorFrame(frames[i]);
                    }
                    if (plotting) {
                        plotCompositorFrame(frames[i]);
                    }
                }
            }

        private:
            /**
             * A new window (recording switched on, a reset, or sampling resumed): skip the frames timed before it, and
             * read the refresh rate again in case it changed.
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
        static bool previousFrameSampled = false;
        static Clock::time_point previousFrame{};
        const bool recording = isEnabled();
        const bool plotting = isTracyConnected();
        if (!recording && !plotting) {
            previousFrameSampled = false;
            return;
        }
        // what measuring the frame costs it
        F4CF_PERF_FUNCTION();
        const bool resumed = !previousFrameSampled;
        const auto now = Clock::now();
        // an interval only between two frames that were both sampled, so switching sampling on doesn't add one spanning
        // the whole time it was off
        if (previousFrameSampled) {
            const auto interval = now - previousFrame;
            if (recording) {
                recordFrameInterval(interval);
            }
            if (plotting) {
                tracyPlot("frame.intervalMs", std::chrono::duration<double, std::milli>(interval).count());
            }
        }
        previousFrame = now;
        previousFrameSampled = true;

        static CompositorPoller compositor;
        compositor.poll(now, recording, plotting, resumed);
    }
}
