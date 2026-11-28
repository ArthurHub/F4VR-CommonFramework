#include "DrawCallbacks.h"

#include <algorithm>
#include <format>

#include <windows.h>
#include <wrl/client.h>

namespace f4cf::render
{
    namespace
    {
        // the function the overlay draw is timed as, on both runtimes, which the draw callbacks' sites share
        constexpr const char* DRAW_PERF_FUNCTION = "f4cf::render::drawToSubmittedTexture";
    }

    namespace internal
    {
        std::wstring widen(const std::string_view text)
        {
            if (text.empty()) {
                return {};
            }
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>((std::max)(size, 0)), L'\0');
            if (size > 0) {
                MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
            }
            return wide;
        }

        std::optional<D3D11_TEXTURE2D_DESC> textureDescOf(ID3D11View* view)
        {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (!resource || FAILED(resource.As(&texture))) {
                return std::nullopt;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            return desc;
        }

        /**
         * The one table, constructed by the first registration or host, on the game thread, so a draw never takes the
         * perf registry's lock to construct its sites.
         */
        DrawCallbacks& DrawCallbacks::get()
        {
            static DrawCallbacks instance;
            return instance;
        }

        DrawCallbacks::DrawCallbacks()
            : _drawPerfSite(DRAW_PERF_FUNCTION, nullptr, __FILE__, __LINE__),
              _drawGpuSite(DRAW_PERF_FUNCTION, nullptr, __FILE__, __LINE__, perf::SiteKind::Gpu),
              _setupGpuSite(DRAW_PERF_FUNCTION, "setup", __FILE__, __LINE__, perf::SiteKind::Gpu)
        {}

        /**
         * Append a callback, looking up its perf sites and the names it is plotted and marked by here, on the
         * registering thread, so a draw never takes the perf registry's lock nor allocates them.
         */
        DrawCallbackId DrawCallbacks::add(std::string name, SubmitDrawCallback callback, const int order)
        {
            const std::size_t index = _count.load(std::memory_order_relaxed);
            if (index >= MAX_DRAW_CALLBACKS || !callback) {
                logger::error("Cannot register draw callback '{}'", name);
                return INVALID_DRAW_CALLBACK;
            }

            auto& entry = _entries[index];
            entry.perfSite = &perf::dynamicSite(DRAW_PERF_FUNCTION, name);
            entry.gpuSite = &perf::dynamicSite(DRAW_PERF_FUNCTION, name, perf::SiteKind::Gpu);
            entry.gpuPlotName = std::format("gpu.draw.{}Ms", name);
            entry.captureMarkerName = widen(name);
            entry.name = std::move(name);
            entry.callback = std::move(callback);
            entry.order = order;
            // publish the entry before the count that makes a host look at it
            _count.store(index + 1, std::memory_order_release);
            return static_cast<DrawCallbackId>(index);
        }

        void DrawCallbacks::setActive(const DrawCallbackId id, const bool active, const bool readsSceneDepth)
        {
            if (id >= MAX_DRAW_CALLBACKS) {
                return;
            }
            const std::uint32_t bit = 1u << id;
            if (active && readsSceneDepth) {
                _sceneDepthMask.fetch_or(bit, std::memory_order_release);
            } else {
                _sceneDepthMask.fetch_and(~bit, std::memory_order_release);
            }
            if (active) {
                _activeMask.fetch_or(bit, std::memory_order_release);
            } else {
                _activeMask.fetch_and(~bit, std::memory_order_release);
            }
        }
    }

    DrawCallbackId registerDrawCallback(std::string name, SubmitDrawCallback callback, const int order)
    {
        return internal::DrawCallbacks::get().add(std::move(name), std::move(callback), order);
    }

    /**
     * Flip one callback between drawing and dormant. Dormant is genuinely free: when no callback is active a host's
     * draw is one relaxed atomic read.
     *
     * readsSceneDepth says whether the callback tests against SubmitFrame::sceneDepth. On VR the scene depth is captured
     * only while an active callback reads it: under an upscaler that is a copy the size of the eye texture every frame,
     * which a layer that is never occluded would otherwise pay for. Flat has no scene depth.
     */
    void setDrawCallbackActive(const DrawCallbackId id, const bool active, const bool readsSceneDepth)
    {
        internal::DrawCallbacks::get().setActive(id, active, readsSceneDepth);
    }
}
