#include "SubmitHook.h"

#include <array>
#include <atomic>
#include <filesystem>

#include <windows.h>
#include <wrl/client.h>

#include "../../external/openvr/openvr.h"
#include "SceneDepthCapture.h"

// The OpenVR IVRCompositor::Submit vtable hook every framework overlay draws through. Extracted
// from the debug-draw overlay (a port of ROCK's DebugBodyOverlay, see
// knowledge-base/debug_draw_overlay.md) and hardened per knowledge-base/imgui_ui_overlay_architecture.md
// sections 4-5 so that the hygiene rules are implemented once here rather than by each subsystem.
namespace f4cf::render
{
    namespace
    {
        // IVRCompositor::Submit, the last call before the frame reaches the headset - and therefore
        // the last chance to draw on it.
        constexpr std::size_t SUBMIT_VTABLE_INDEX = 5;

        // Two mods can both be lazily installing on the same frame; the vtable slot swap is a
        // read-modify-write, so it is serialized process-wide. Losing that race orphans whoever
        // wrote first, silently and permanently.
        constexpr const char* INSTALL_MUTEX_NAME = "Local\\F4VRCommonFramework_SubmitHookInstall";
        constexpr DWORD INSTALL_MUTEX_TIMEOUT_MS = 5000;

        // Frames a callback may stay active without the hook running before we call it orphaned.
        // Well past any legitimate stall at 90Hz.
        constexpr std::uint32_t ORPHAN_FRAME_THRESHOLD = 120;

        constexpr std::size_t MAX_DRAW_CALLBACKS = 32;

        using VRSubmit_t = vr::EVRCompositorError(__thiscall*)(vr::IVRCompositor*, vr::EVREye, const vr::Texture_t*, const vr::VRTextureBounds_t*, vr::EVRSubmitFlags);

        struct DrawCallbackEntry
        {
            std::string name;
            SubmitDrawCallback callback;
            int order = DRAW_ORDER_DEFAULT;
        };

        /**
         * RTV over the submitted eye texture, cached keyed by texture pointer + size - the texture
         * is stable frame-to-frame, so this avoids creating an RTV every frame.
         */
        struct CachedRenderTargetView
        {
            ID3D11Texture2D* texture = nullptr;
            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        };

        // --- shared by both threads -------------------------------------------------------------
        // The hook reads only these atomics plus the callback table, which is append-only and
        // published with release/acquire, so it never blocks or allocates on the render thread.
        std::atomic<std::uint32_t> s_activeMask{ 0 };
        std::atomic<std::size_t> s_callbackCount{ 0 };
        std::atomic<std::uint64_t> s_hookRuns{ 0 };
        std::atomic<bool> s_fenced{ false };
        std::array<DrawCallbackEntry, MAX_DRAW_CALLBACKS> s_callbacks;

        // --- render thread only -----------------------------------------------------------------
        thread_local int s_hookDepth = 0;
        CachedRenderTargetView s_submittedTextureRtv{};
        bool s_loggedDrawFailure = false;
        bool s_loggedReentry = false;

        // --- game thread only -------------------------------------------------------------------
        bool s_installed = false;
        bool s_d3dInitialized = false;
        bool s_loggedNotVR = false;
        bool s_loggedNoDevice = false;
        bool s_loggedNoCompositor = false;
        bool s_loggedD3dInitFailed = false;
        bool s_orphaned = false;
        std::uint64_t s_lastSeenHookRuns = 0;
        std::uint32_t s_framesWithoutHookRun = 0;

        VRSubmit_t s_originalVRSubmit = nullptr;
        void** s_compositorVTable = nullptr;
        vr::IVRCompositor* s_hookedCompositor = nullptr;
        ID3D11Buffer* s_cameraConstantBuffer = nullptr;

        /**
         * The camera constant buffer bound to VS b0 for every callback, so the per-eye matrices are
         * uploaded once per frame instead of once per subsystem.
         */
        bool initializeD3D(ID3D11Device* device)
        {
            D3D11_BUFFER_DESC cameraDesc{};
            cameraDesc.Usage = D3D11_USAGE_DYNAMIC;
            cameraDesc.ByteWidth = sizeof(StereoCameraConstants);
            cameraDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cameraDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            return SUCCEEDED(device->CreateBuffer(&cameraDesc, nullptr, &s_cameraConstantBuffer));
        }

        ID3D11RenderTargetView* getSubmittedTextureRtv(ID3D11Device* device, ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& desc)
        {
            if (s_submittedTextureRtv.texture == texture && s_submittedTextureRtv.rtv && s_submittedTextureRtv.width == desc.Width && s_submittedTextureRtv.height == desc.Height &&
                s_submittedTextureRtv.format == desc.Format) {
                return s_submittedTextureRtv.rtv.Get();
            }

            s_submittedTextureRtv.rtv.Reset();
            s_submittedTextureRtv.texture = nullptr;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
            if (FAILED(device->CreateRenderTargetView(texture, nullptr, rtv.GetAddressOf())) || !rtv) {
                return nullptr;
            }

            s_submittedTextureRtv.texture = texture;
            s_submittedTextureRtv.width = desc.Width;
            s_submittedTextureRtv.height = desc.Height;
            s_submittedTextureRtv.format = desc.Format;
            s_submittedTextureRtv.rtv = std::move(rtv);
            return s_submittedTextureRtv.rtv.Get();
        }

        /**
         * Run every active draw callback against the texture the game just handed to the compositor,
         * with the whole pipeline snapshotted around them. Each callback is isolated: one that throws
         * is dropped permanently rather than retried every frame, and its neighbours still draw.
         */
        void drawToSubmittedTexture(const vr::Texture_t* texture)
        {
            auto* device = getDevice();
            auto* context = getContext();
            if (!device || !context || !texture || !texture->handle || texture->eType != vr::TextureType_DirectX) {
                return;
            }

            auto* submittedTexture = static_cast<ID3D11Texture2D*>(texture->handle);
            D3D11_TEXTURE2D_DESC textureDesc{};
            submittedTexture->GetDesc(&textureDesc);

            // Scene depth for world occlusion has to be taken during the engine's own render, where
            // it is still bound, and matched back to this texture by size. Publishing that size here
            // is what lets the capture tell the view we draw into from every other pass; the first
            // frame therefore captures nothing, because the commit ran before this ever executed.
            sceneDepth::ensureInstalled();
            sceneDepth::setSubmittedTexture(submittedTexture);
            sceneDepth::setCaptureRequested(true);

            ID3D11RenderTargetView* rtv = getSubmittedTextureRtv(device, submittedTexture, textureDesc);
            if (!rtv) {
                return;
            }

            StereoCameraConstants camera{};
            if (!getStereoCameraConstants(camera)) {
                return;
            }

            const auto capturedDepth = sceneDepth::acquireForSubmittedTexture(submittedTexture);

            SubmitFrame frame;
            frame.device = device;
            frame.context = context;
            frame.renderTarget = rtv;
            frame.sceneDepth = capturedDepth.readOnlyView;
            frame.sceneDepthComparison = capturedDepth.comparison;
            frame.width = static_cast<float>(textureDesc.Width);
            frame.height = static_cast<float>(textureDesc.Height);
            frame.camera = &camera;

            // constructed before anything is bound, destroyed after the last callback returns
            const ScopedPipelineState savedState(context);

            // The depth view is read-only, so binding it for everyone is safe: a callback that does
            // not enable depth testing is unaffected, and none of them can write to the engine's
            // buffer even by accident.
            context->OMSetRenderTargets(1, &rtv, capturedDepth.readOnlyView);
            D3D11_VIEWPORT viewport{};
            viewport.Width = frame.width;
            viewport.Height = frame.height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(s_cameraConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                *static_cast<StereoCameraConstants*>(mapped.pData) = camera;
                context->Unmap(s_cameraConstantBuffer, 0);
            }
            context->VSSetConstantBuffers(0, 1, &s_cameraConstantBuffer);

            // Collect the active callbacks in painter order. Sorted here, on a stack array of at
            // most 32 entries, rather than keeping the table itself sorted: registration only ever
            // appends, so the table is never reordered under the render thread's feet.
            sceneDepth::advanceFrame();

            const std::uint32_t activeMask = s_activeMask.load(std::memory_order_relaxed);
            const std::size_t count = s_callbackCount.load(std::memory_order_acquire);
            std::array<std::size_t, MAX_DRAW_CALLBACKS> ordered{};
            std::size_t orderedCount = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if ((activeMask & (1u << i)) == 0) {
                    continue;
                }
                std::size_t position = orderedCount++;
                while (position > 0 && s_callbacks[ordered[position - 1]].order > s_callbacks[i].order) {
                    ordered[position] = ordered[position - 1];
                    --position;
                }
                ordered[position] = i;
            }

            for (std::size_t i = 0; i < orderedCount; ++i) {
                const std::size_t index = ordered[i];
                try {
                    s_callbacks[index].callback(frame);
                } catch (const std::exception& ex) {
                    setDrawCallbackActive(static_cast<DrawCallbackId>(index), false);
                    logger::error("Draw callback '{}' threw ({}); dropped for this session", s_callbacks[index].name, ex.what());
                }
            }
        }

        /**
         * The Submit hook. Every path chains: a skipped original Submit black-screens the headset,
         * so the chain call is the last statement and nothing between here and it may throw.
         */
        vr::EVRCompositorError vrSubmitHook(vr::IVRCompositor* compositor, const vr::EVREye eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
            const vr::EVRSubmitFlags flags)
        {
            // Re-entry means a foreign hook re-installed itself over us without an idempotence
            // check and is now calling back through this function - one TLS counter turns what
            // would be an infinite recursion into a log line.
            if (s_hookDepth > 0) {
                if (!s_loggedReentry) {
                    s_loggedReentry = true;
                    logger::error("Re-entered - a foreign Submit hook is cycling through us; skipping the overlay draw");
                }
                return s_originalVRSubmit(compositor, eye, texture, bounds, flags);
            }

            // counts calls rather than draws, so standing down does not read as being orphaned
            s_hookRuns.fetch_add(1, std::memory_order_relaxed);

            // both eyes arrive as one double-wide texture, so only the first submit draws
            if (eye == vr::Eye_Left && s_activeMask.load(std::memory_order_relaxed) != 0 && !s_fenced.load(std::memory_order_relaxed)) {
                ++s_hookDepth;
                try {
                    drawToSubmittedTexture(texture);
                } catch (const std::exception& ex) {
                    // the shared setup failed rather than one callback; stand down permanently
                    // instead of retrying, and let every other overlay in the chain carry on
                    s_fenced.store(true, std::memory_order_relaxed);
                    if (!s_loggedDrawFailure) {
                        s_loggedDrawFailure = true;
                        logger::error("Overlay draw threw ({}); overlay disabled for this session", ex.what());
                    }
                }
                --s_hookDepth;
            } else if (eye == vr::Eye_Left) {
                // nothing draws against the next frame's depth, so it is not captured - which under an
                // upscaler is a full-screen copy every frame
                sceneDepth::setCaptureRequested(false);
            }
            return s_originalVRSubmit(compositor, eye, texture, bounds, flags);
        }

        /**
         * Name whatever currently owns the Submit vtable slot, so "the overlay stopped drawing"
         * becomes a five-second diagnosis instead of a bug report.
         */
        std::string describeSubmitSlotOwner()
        {
            if (!s_compositorVTable) {
                return "<not installed>";
            }
            void* slot = s_compositorVTable[SUBMIT_VTABLE_INDEX];
            if (slot == reinterpret_cast<void*>(&vrSubmitHook)) {
                return "this mod (slot unchanged - the compositor object itself was likely replaced)";
            }

            HMODULE module = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(slot), &module) && module) {
                char path[MAX_PATH] = {};
                if (GetModuleFileNameA(module, path, MAX_PATH)) {
                    return std::filesystem::path(path).filename().string();
                }
            }
            return std::format("unknown module at 0x{:X}", reinterpret_cast<std::uintptr_t>(slot));
        }

        /**
         * Watch for the hook having been spliced out of the chain: a foreign mod that installed
         * after us and then restored the slot blindly takes us with it, silently, and no amount of
         * reading the slot can tell "chained politely above us" from "removed us". Only the fact
         * that the hook stopped running while we had something to draw can.
         *
         * Detect and log loudly rather than auto-recover: re-installing fights an unknown third
         * party, and two mods both recovering ping-pong forever.
         */
        void checkOrphaned()
        {
            const auto runs = s_hookRuns.load(std::memory_order_relaxed);
            if (s_activeMask.load(std::memory_order_relaxed) == 0) {
                s_lastSeenHookRuns = runs;
                s_framesWithoutHookRun = 0;
                return;
            }

            if (runs != s_lastSeenHookRuns) {
                s_lastSeenHookRuns = runs;
                s_framesWithoutHookRun = 0;
                if (s_orphaned) {
                    s_orphaned = false;
                    logger::info("Back in the chain, overlay drawing again");
                }
                return;
            }

            if (++s_framesWithoutHookRun < ORPHAN_FRAME_THRESHOLD || s_orphaned) {
                return;
            }
            s_orphaned = true;
            const bool compositorReplaced = vr::VRCompositor() != s_hookedCompositor;
            logger::error("Not called for {} frames while drawing was active - orphaned. Submit slot now owned by: {}{}",
                ORPHAN_FRAME_THRESHOLD,
                describeSubmitSlotOwner(),
                compositorReplaced ? "; the compositor object was also replaced (a wrapped openvr_api.dll?)" : "");
        }

        /**
         * Patch IVRCompositor vtable index 5 on the live compositor.
         *
         * The slot is written exactly once and never restored. Restoring is only safe for whoever
         * is currently on top (LIFO); a hook in the middle of the chain cannot splice itself out,
         * because the pointer to it lives inside the next hook's saved original, which it cannot
         * reach - so it would silently remove everyone above it instead. There is no uninstall API
         * for that reason; setDrawCallbackActive(false) makes the hook a tail call instead.
         */
        bool installSubmitHook()
        {
            auto* compositor = vr::VRCompositor();
            if (!compositor) {
                if (!s_loggedNoCompositor) {
                    s_loggedNoCompositor = true;
                    logger::warn("OpenVR compositor unavailable; will retry on frame update");
                }
                return false;
            }

            const HANDLE installMutex = CreateMutexA(nullptr, FALSE, INSTALL_MUTEX_NAME);
            bool holdsInstallMutex = false;
            if (installMutex) {
                const DWORD waitResult = WaitForSingleObject(installMutex, INSTALL_MUTEX_TIMEOUT_MS);
                holdsInstallMutex = waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED;
            }
            const auto releaseInstallMutex = [&] {
                if (holdsInstallMutex) {
                    ReleaseMutex(installMutex);
                }
                if (installMutex) {
                    CloseHandle(installMutex);
                }
            };

            auto*** objectVTable = reinterpret_cast<void***>(compositor);
            void** vtable = *objectVTable;

            DWORD oldProtect = 0;
            if (!VirtualProtect(&vtable[SUBMIT_VTABLE_INDEX], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                releaseInstallMutex();
                logger::warn("VirtualProtect failed; Submit hook not installed");
                return false;
            }

            s_compositorVTable = vtable;
            s_hookedCompositor = compositor;
            s_originalVRSubmit = reinterpret_cast<VRSubmit_t>(vtable[SUBMIT_VTABLE_INDEX]);
            vtable[SUBMIT_VTABLE_INDEX] = reinterpret_cast<void*>(&vrSubmitHook);
            VirtualProtect(&vtable[SUBMIT_VTABLE_INDEX], sizeof(void*), oldProtect, &oldProtect);
            releaseInstallMutex();
            return true;
        }
    }

    DrawCallbackId registerDrawCallback(std::string name, SubmitDrawCallback callback, const int order)
    {
        const std::size_t index = s_callbackCount.load(std::memory_order_relaxed);
        if (index >= MAX_DRAW_CALLBACKS || !callback) {
            logger::error("Cannot register draw callback '{}'", name);
            return INVALID_DRAW_CALLBACK;
        }

        s_callbacks[index].name = std::move(name);
        s_callbacks[index].callback = std::move(callback);
        s_callbacks[index].order = order;
        // publish the entry before the count that makes the render thread look at it
        s_callbackCount.store(index + 1, std::memory_order_release);
        return static_cast<DrawCallbackId>(index);
    }

    void setDrawCallbackActive(const DrawCallbackId id, const bool active)
    {
        if (id >= MAX_DRAW_CALLBACKS) {
            return;
        }
        const std::uint32_t bit = 1u << id;
        if (active) {
            s_activeMask.fetch_or(bit, std::memory_order_release);
        } else {
            s_activeMask.fetch_and(~bit, std::memory_order_release);
        }
    }

    /**
     * Lazily install on first use: D3D objects off the game device, then the Submit hook. Safe to
     * call every frame - each unavailable dependency just retries - and doubles as the game-thread
     * tick for orphan detection.
     */
    bool ensureInstalled()
    {
        if (s_installed) {
            checkOrphaned();
            return true;
        }

        if (!REL::Module::IsVR()) {
            if (!s_loggedNotVR) {
                s_loggedNotVR = true;
                logger::warn("Only supported on Fallout 4 VR; overlays disabled");
            }
            return false;
        }

        auto* device = getDevice();
        if (!device) {
            if (!s_loggedNoDevice) {
                s_loggedNoDevice = true;
                logger::warn("D3D11 device unavailable; will retry on frame update");
            }
            return false;
        }

        if (!s_d3dInitialized) {
            if (!initializeD3D(device)) {
                if (!s_loggedD3dInitFailed) {
                    s_loggedD3dInitFailed = true;
                    logger::error("D3D initialization failed; overlays disabled");
                }
                return false;
            }
            s_d3dInitialized = true;
        }

        if (!installSubmitHook()) {
            return false;
        }

        s_installed = true;
        logger::info("OpenVR Submit hook installed");
        return true;
    }

    bool isInstalled()
    {
        return s_installed;
    }
}
