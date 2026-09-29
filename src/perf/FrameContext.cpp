#include "FrameContext.h"

#include <algorithm>
#include <atomic>

namespace f4cf::perf
{
    namespace
    {
        struct Live
        {
            Histogram interval;
            Histogram gpu;
            Histogram gameGpu;
            Histogram compositorGpu;
            Histogram lateStart;
            Histogram headroom;
            std::atomic<std::uint64_t> compositorFrames{ 0 };
            std::atomic<std::uint64_t> reprojectedCpu{ 0 };
            std::atomic<std::uint64_t> reprojectedGpu{ 0 };
            std::atomic<std::uint64_t> dropped{ 0 };
            std::atomic<std::uint64_t> misPresented{ 0 };
            std::atomic<float> displayHz{ 0 };
        };

        /**
         * Function-local, so it exists before any static that records into it.
         */
        Live& live()
        {
            static Live live;
            return live;
        }

        void recordMs(Histogram& histogram, const float ms)
        {
            histogram.recordNs(static_cast<std::uint64_t>((std::max)(ms, 0.0f) * 1'000'000.0f));
        }
    }

    void recordFrameInterval(const std::chrono::nanoseconds interval)
    {
        live().interval.record(interval);
    }

    void recordCompositorFrame(const CompositorFrame& frame)
    {
        auto& l = live();
        recordMs(l.gpu, frame.gpuMs);
        recordMs(l.gameGpu, frame.gameGpuMs);
        recordMs(l.compositorGpu, frame.compositorGpuMs);
        recordMs(l.lateStart, frame.lateStartMs);
        recordMs(l.headroom, frame.headroomMs);
        l.compositorFrames.fetch_add(1, std::memory_order_relaxed);
        l.reprojectedCpu.fetch_add(frame.reprojectedCpu ? 1 : 0, std::memory_order_relaxed);
        l.reprojectedGpu.fetch_add(frame.reprojectedGpu ? 1 : 0, std::memory_order_relaxed);
        l.dropped.fetch_add(frame.dropped, std::memory_order_relaxed);
        l.misPresented.fetch_add(frame.misPresented, std::memory_order_relaxed);
    }

    void setDisplayHz(const float hz)
    {
        live().displayHz.store(hz, std::memory_order_relaxed);
    }

    FrameContext readFrameContext()
    {
        const auto& l = live();
        FrameContext frame;
        frame.displayHz = l.displayHz.load(std::memory_order_relaxed);
        frame.interval = l.interval.peek();
        frame.compositorFrames = l.compositorFrames.load(std::memory_order_relaxed);
        frame.gpu = l.gpu.peek();
        frame.gameGpu = l.gameGpu.peek();
        frame.compositorGpu = l.compositorGpu.peek();
        frame.lateStart = l.lateStart.peek();
        frame.headroom = l.headroom.peek();
        frame.reprojectedCpu = l.reprojectedCpu.load(std::memory_order_relaxed);
        frame.reprojectedGpu = l.reprojectedGpu.load(std::memory_order_relaxed);
        frame.dropped = l.dropped.load(std::memory_order_relaxed);
        frame.misPresented = l.misPresented.load(std::memory_order_relaxed);
        return frame;
    }

    namespace internal
    {
        void resetFrameContext()
        {
            auto& l = live();
            for (auto* histogram : { &l.interval, &l.gpu, &l.gameGpu, &l.compositorGpu, &l.lateStart, &l.headroom }) {
                (void)histogram->drain();
            }
            for (auto* counter : { &l.compositorFrames, &l.reprojectedCpu, &l.reprojectedGpu, &l.dropped, &l.misPresented }) {
                counter->store(0, std::memory_order_relaxed);
            }
        }
    }
}
