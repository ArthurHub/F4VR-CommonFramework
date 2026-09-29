#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <d3d11.h>
#include <wrl/client.h>

#include "Perf.h"

namespace f4cf::perf
{
    /**
     * GPU time of the work a mod issues on the D3D11 immediate context, one frame at a time, split into consecutive
     * spans: a timestamp where the work starts and one after each span, so N spans take N+1 timestamps, and a disjoint
     * query around them that says whether they can be trusted. The whole frame is recorded in the root GPU site and each
     * span in its own site under it; while a Tracy viewer is connected each is also a plot.
     *
     * Results are read 1-3 frames later without waiting or flushing. A frame is not timed when every slot is still in
     * flight, and a frame the GPU clock was disjoint over is dropped. Nothing is issued unless perf is recording or a
     * Tracy viewer is connected.
     *
     * Use it on the thread that issues the work, one frame at a time, as with the context itself.
     *
     * @code
     *     static perf::Site gpuSite("MyLayer::draw", nullptr, __FILE__, __LINE__, perf::SiteKind::Gpu);
     *     static perf::Site blurSite("MyLayer::draw", "blur", __FILE__, __LINE__, perf::SiteKind::Gpu);
     *     static perf::GpuTimer gpuTimer(gpuSite, "gpu.myLayerMs");
     *
     *     auto gpuFrame = gpuTimer.begin(context);
     *     drawBlur(context);
     *     gpuFrame.mark(blurSite, "gpu.myLayer.blurMs");
     * @endcode
     */
    class GpuTimer
    {
    public:
        // spans in one frame; marks past it are ignored
        static constexpr std::size_t MAX_SPANS = 40;
        // frames in flight at once
        static constexpr std::size_t SLOTS = 4;

        /**
         * One frame being timed, from begin() to the end of its scope. An untimed frame's marks do nothing.
         */
        class [[nodiscard]] Frame
        {
        public:
            ~Frame();

            Frame(const Frame&) = delete;
            Frame& operator=(const Frame&) = delete;
            Frame(Frame&&) = delete;
            Frame& operator=(Frame&&) = delete;

            /**
             * End the span that started at the previous mark, or at the frame's start, and record it in site, a GPU site
             * (SiteKind::Gpu) under the timer's root. plotName names its Tracy plot, a string that outlives the timer, or
             * nullptr for none.
             */
            void mark(Site& site, const char* plotName = nullptr) const;

            [[nodiscard]] bool isTimed() const
            {
                return _timer != nullptr;
            }

        private:
            friend class GpuTimer;

            explicit Frame(GpuTimer* timer)
                : _timer(timer)
            {}

            GpuTimer* _timer;
        };

        /**
         * @param root     the GPU site (SiteKind::Gpu) each whole frame is recorded in; it must outlive the timer
         * @param plotName its Tracy plot, a string that outlives the timer, or nullptr for none
         */
        GpuTimer(Site& root, const char* plotName);

        GpuTimer(const GpuTimer&) = delete;
        GpuTimer& operator=(const GpuTimer&) = delete;
        GpuTimer(GpuTimer&&) = delete;
        GpuTimer& operator=(GpuTimer&&) = delete;

        /**
         * Record the frames whose results are in, then start timing this one on the context. The frame returned is
         * untimed when neither perf nor a Tracy viewer wants it, when every slot is still in flight, or when the queries
         * could not be created.
         */
        Frame begin(ID3D11DeviceContext* context);

    private:
        struct Span
        {
            Site* site = nullptr;
            const char* plotName = nullptr;
        };

        struct Slot
        {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            // created as the marks first need them
            std::array<Microsoft::WRL::ComPtr<ID3D11Query>, MAX_SPANS + 1> timestamps;
            std::array<Span, MAX_SPANS> spans{};
            // timestamps issued this frame: the start and one per span
            std::size_t marks = 0;
            bool record = false;
            bool plot = false;
            // the perf window the frame was issued in: a reset since drops it
            std::chrono::steady_clock::time_point window{};
        };

        void collect();
        void readBack(const Slot& slot, std::uint64_t frequency);
        bool issueTimestamp(Slot& slot);
        void mark(Site& site, const char* plotName);
        void end();

        Site& _root;
        const char* _plotName;
        std::array<Slot, SLOTS> _slots;
        // the oldest slot in flight, and how many are
        std::size_t _oldest = 0;
        std::size_t _inFlight = 0;
        // the slot being issued between begin() and the end of its Frame
        Slot* _current = nullptr;
        // the game's, borrowed: the context of the frame being issued, and the device the queries were made on
        ID3D11DeviceContext* _context = nullptr;
        ID3D11Device* _device = nullptr;
        // a query could not be created: stop trying
        bool _unavailable = false;
    };
}
