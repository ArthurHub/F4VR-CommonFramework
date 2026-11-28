#pragma once

// The draw callbacks both runtimes' hosts draw: VR's Submit hook and player-space pass (SubmitHook.cpp), flat's UI pass
// (FlatDrawHost.cpp). A host decides where and when it draws; this holds what it draws and in which order.
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <d3d11_1.h>

#include "../perf/Perf.h"
#include "DrawHost.h"

namespace f4cf::render::internal
{
    inline constexpr std::size_t MAX_DRAW_CALLBACKS = 32;

    struct DrawCallbackEntry
    {
        std::string name;
        SubmitDrawCallback callback;
        int order = DRAW_ORDER_DEFAULT;
        // labelled with the name, under the draw's own site
        perf::Site* perfSite = nullptr;
        // its GPU time, under the draw's GPU site, and the Tracy plot of it
        perf::Site* gpuSite = nullptr;
        std::string gpuPlotName;
        // what a frame capture tool shows its drawing as
        std::wstring captureMarkerName;
    };

    /**
     * UTF-8 text as the wide text a frame capture tool takes.
     */
    std::wstring widen(std::string_view text);

    /**
     * The size and format of the texture a view is of, or nothing for a view of no 2D texture.
     */
    std::optional<D3D11_TEXTURE2D_DESC> textureDescOf(ID3D11View* view);

    /**
     * A named event around a stretch of our drawing, which a frame capture tool (RenderDoc, PIX) lists the draw by.
     * Given no annotation, which is how the draw says no tool is attached, it does nothing.
     */
    class ScopedCaptureMarker
    {
    public:
        ScopedCaptureMarker(ID3DUserDefinedAnnotation* annotation, const std::wstring& name)
            : _annotation(annotation)
        {
            if (_annotation) {
                _annotation->BeginEvent(name.c_str());
            }
        }

        ~ScopedCaptureMarker()
        {
            if (_annotation) {
                _annotation->EndEvent();
            }
        }

        ScopedCaptureMarker(const ScopedCaptureMarker&) = delete;
        ScopedCaptureMarker& operator=(const ScopedCaptureMarker&) = delete;
        ScopedCaptureMarker(ScopedCaptureMarker&&) = delete;
        ScopedCaptureMarker& operator=(ScopedCaptureMarker&&) = delete;

    private:
        ID3DUserDefinedAnnotation* _annotation;
    };

    /**
     * The registered draw callbacks, behind registerDrawCallback and setDrawCallbackActive of DrawHost.h. The table is
     * append-only and published with release/acquire, and a host reads only it and atomics, so a draw never blocks or
     * allocates.
     */
    class DrawCallbacks
    {
    public:
        static DrawCallbacks& get();

        DrawCallbackId add(std::string name, SubmitDrawCallback callback, int order);
        void setActive(DrawCallbackId id, bool active, bool readsSceneDepth);

        bool anyActive() const
        {
            return _activeMask.load(std::memory_order_relaxed) != 0;
        }

        /**
         * Whether an active callback reads the scene depth this frame, which is what VR's capture is on for.
         */
        bool sceneDepthRead() const
        {
            return (_activeMask.load(std::memory_order_relaxed) & _sceneDepthMask.load(std::memory_order_relaxed)) != 0;
        }

        /**
         * Run the active callbacks the filter takes, in painter order. Each is isolated: one that throws is dropped
         * permanently rather than retried every frame, and its neighbours still draw. afterEach runs after each, with it.
         */
        template <class Filter, class AfterEach>
        void run(const SubmitFrame& frame, ID3DUserDefinedAnnotation* captureAnnotation, Filter filter, AfterEach afterEach)
        {
            // Sorted here, on a stack array of at most 32 entries, rather than keeping the table itself sorted:
            // registration only ever appends, so the table is never reordered under the drawing thread's feet.
            const std::uint32_t activeMask = _activeMask.load(std::memory_order_relaxed);
            const std::size_t count = _count.load(std::memory_order_acquire);
            std::array<std::size_t, MAX_DRAW_CALLBACKS> ordered{};
            std::size_t orderedCount = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if ((activeMask & (1u << i)) == 0 || !filter(_entries[i])) {
                    continue;
                }
                std::size_t position = orderedCount++;
                while (position > 0 && _entries[ordered[position - 1]].order > _entries[i].order) {
                    ordered[position] = ordered[position - 1];
                    --position;
                }
                ordered[position] = i;
            }

            for (std::size_t i = 0; i < orderedCount; ++i) {
                const std::size_t index = ordered[i];
                const auto& entry = _entries[index];
                {
                    const ScopedCaptureMarker callbackMarker(captureAnnotation, entry.captureMarkerName);
                    try {
                        const perf::Scope callbackPerfScope(*entry.perfSite);
                        entry.callback(frame);
                    } catch (const std::exception& ex) {
                        setActive(static_cast<DrawCallbackId>(index), false, false);
                        logger::error("Draw callback '{}' threw ({}); dropped for this session", entry.name, ex.what());
                    }
                }
                afterEach(entry);
            }
        }

        // The host's draw on the CPU, a root of its own: it runs outside the mod's frame, so no other site is open around it
        perf::Site& drawPerfSite()
        {
            return _drawPerfSite;
        }

        // the GPU time of the whole draw, and of the shared setup under it; each callback's is its entry's gpuSite
        perf::Site& drawGpuSite()
        {
            return _drawGpuSite;
        }

        perf::Site& setupGpuSite()
        {
            return _setupGpuSite;
        }

    private:
        DrawCallbacks();

        std::atomic<std::uint32_t> _activeMask{ 0 };
        // the active callbacks that read the scene depth, which VR captures only while one does
        std::atomic<std::uint32_t> _sceneDepthMask{ 0 };
        std::atomic<std::size_t> _count{ 0 };
        std::array<DrawCallbackEntry, MAX_DRAW_CALLBACKS> _entries;
        perf::Site _drawPerfSite;
        perf::Site _drawGpuSite;
        perf::Site _setupGpuSite;
    };
}
