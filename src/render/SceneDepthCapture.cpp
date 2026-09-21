#include "SceneDepthCapture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <windows.h>
#include <wrl/client.h>

#include "RenderUtils.h"
#include "SceneDepthDiagnostics.h"
#include "SceneDepthResample.h"

namespace f4cf::render::sceneDepth
{
    namespace
    {
        // Fallout 4 VR 1.2.72. The scene depth-stencil view is bound while the engine commits its
        // graphics state and unbound again well before the frame reaches the compositor, so this
        // call site is the seam where it can be read. Hooking the CALL rather than the function
        // leaves CommitGraphicsState's other callers alone. Other plugins read depth at the same seam
        // (ROCK's RPS UI Framework does), so the hook chains onto one already there rather than
        // refusing it - see inspectCallSite.
        constexpr std::uintptr_t COMMIT_GRAPHICS_STATE_RVA = 0x1D9B5D0;
        constexpr std::uintptr_t CAPTURE_CALLSITE_RVA = 0x1D8E84A;

        constexpr std::uint8_t CALL_REL32_OPCODE = 0xE8;
        constexpr std::size_t CALL_REL32_LENGTH = 5;

        // First instructions of CommitGraphicsState. Checked before patching so a shifted or wrong
        // address declines to install rather than corrupting the instruction stream - which is the
        // difference between a warning in the log and a crash in someone's game.
        constexpr std::array<std::uint8_t, 19> COMMIT_PROLOGUE{
            0x88,
            0x54,
            0x24,
            0x10, // mov [rsp+0x10], dl
            0x88,
            0x4C,
            0x24,
            0x08, // mov [rsp+0x08], cl
            0x55,
            0x56,
            0x57,
            0x41,
            0x56,
            0x41,
            0x57, // push rbp, rsi, rdi, r14, r15
            0x48,
            0x83,
            0xEC,
            0x50 // sub rsp, 0x50
        };

        using CommitGraphicsStateFn = void (*)(std::uint8_t, std::uint8_t);

        // Fallout 4 VR's world depth is conventional - near surfaces smaller, cleared to 1 - so an
        // overlay is behind the world where the world's depth is less than its own. A constant rather
        // than read from the engine's depth-stencil state: the hooked call site commits the image-space
        // passes, never the geometry that writes depth, and those passes test in both directions, so
        // none of them says which way the world's depth runs.
        constexpr D3D11_COMPARISON_FUNC WORLD_DEPTH_COMPARISON = D3D11_COMPARISON_LESS_EQUAL;

        /**
         * How many frames old the world's copy may be and still be handed out under an upscaler.
         *
         * One was the original choice, and measuring it showed why two is right: a frame that draws
         * fewer passes than the plan expects makes no copy at all, and two such frames in a row left
         * the overlay unoccluded for a frame - visibly, as an overlay popping in front of the world.
         * Short frames are not rare and are not only an upscaler artefact: hiding any drawn object
         * removes a pass, so this mod's own meshes cause them (measured at roughly one frame in four
         * hundred, three visible drops in a 95-second session). Two frames of lag on a resampled copy
         * go unseen where an overlay blinking to the top does not.
         */
        constexpr std::uint64_t WORLD_COPY_MAX_AGE_FRAMES = 2;

        /**
         * Where one frame's passes drew into the engine's depth buffer, counted per viewport - to find
         * the world's viewport, and under an upscaler the pass to copy the world's depth at.
         *
         * An upscaler driving the engine's dynamic resolution (DLSS, FSR) keeps the depth buffer at the
         * submitted size but draws the world into a smaller viewport of it - top-left, both eyes in one.
         * Most of a frame's passes into the buffer draw with that viewport, while the first draws with
         * the whole buffer, so the most-used viewport is the world's.
         *
         * The upscaler also clears the buffer before the frame is submitted, so the world's depth has to
         * be copied during the frame, while its passes still draw against it - at the last of them, to
         * miss nothing drawn late. Which pass is last is only known once the frame is over, so the copy
         * is made at the last world pass of the shorter of the previous two frames: frames differ by a
         * pass or two, and a count that alternates between two values still reaches that pass every
         * frame. A frame that falls short makes no copy, and the previous frame's is used.
         *
         * Viewports within a few pixels count as one: passes that round the scaled size differently
         * would otherwise trade places as the most-used from frame to frame.
         *
         * None of that has a checkable answer, which is why two of these run: the shipped tally counts
         * every pass that draws into the buffer, and a shadow counts only the passes that test depth the
         * way the world does (isWorldDepthPass). The shadow decides nothing until sSceneDepthStrategy
         * names it; what it produces meanwhile is a divergence count, which is how a filter that would
         * quietly change which viewport wins earns its way in. See SceneDepthDiagnostics.h.
         */
        class WorldPassTally
        {
        public:
            explicit WorldPassTally(const bool depthFiltered)
                : _depthFiltered(depthFiltered)
            {}

            static bool sameViewport(const D3D11_VIEWPORT& a, const D3D11_VIEWPORT& b)
            {
                return std::abs(a.TopLeftX - b.TopLeftX) <= SAME_VIEWPORT_PIXELS && std::abs(a.TopLeftY - b.TopLeftY) <= SAME_VIEWPORT_PIXELS &&
                       std::abs(a.Width - b.Width) <= SAME_VIEWPORT_PIXELS && std::abs(a.Height - b.Height) <= SAME_VIEWPORT_PIXELS;
            }

            /**
             * Count one pass into the buffer. When it is the pass to copy the world's depth at, returns
             * the viewport to copy it from - never the whole buffer, since a world drawn over all of it
             * is tested against where it lies and needs no copy.
             */
            std::optional<D3D11_VIEWPORT> add(const std::uint64_t epoch, const D3D11_VIEWPORT& viewport, const D3D11_VIEWPORT& whole)
            {
                if (_frameEpoch != epoch) {
                    startFrame(epoch, whole);
                }

                const auto end = _viewports.begin() + static_cast<std::ptrdiff_t>(_viewportCount);
                const auto group = std::ranges::find_if(_viewports.begin(), end, [&](const ViewportCount& entry) {
                    return sameViewport(entry.viewport, viewport);
                });
                if (group != end) {
                    ++group->passes;
                    // the widest of a group, so no pass's world is left out of the copy
                    group->viewport.Width = (std::max)(group->viewport.Width, viewport.Width);
                    group->viewport.Height = (std::max)(group->viewport.Height, viewport.Height);
                } else if (_viewportCount < _viewports.size()) {
                    _viewports[_viewportCount++] = ViewportCount{ .viewport = viewport, .passes = 1 };
                } else {
                    // a viewport dropped for want of a group cannot win the election, so the frame is
                    // reported as one where the cap was reached
                    _groupCapHit = true;
                }

                if (_copyAtPass == 0 || !sameViewport(_copyViewport, viewport) || ++_worldPassesSoFar != _copyAtPass || sameViewport(_copyViewport, whole)) {
                    return std::nullopt;
                }
                return _copyViewport;
            }

            /**
             * The viewport most of this frame's passes drew with, or nullopt when none was counted for it.
             */
            std::optional<D3D11_VIEWPORT> mostUsedViewport(const std::uint64_t epoch) const
            {
                const auto world = _frameEpoch == epoch ? rank().winner : std::nullopt;
                return world ? std::optional(world->viewport) : std::nullopt;
            }

        private:
            // passes that round the same scaled size differently stay within this; upscaler qualities are
            // hundreds of pixels apart
            static constexpr float SAME_VIEWPORT_PIXELS = 8.0f;

            struct ViewportCount
            {
                D3D11_VIEWPORT viewport{};
                std::uint32_t passes = 0;
            };

            /**
             * The election's result: the winner, and the best of the rest - a winner one pass ahead of
             * the runner-up is a coin toss the next frame may flip, which is worth saying out loud.
             */
            struct Ranking
            {
                std::optional<ViewportCount> winner;
                std::uint32_t runnerUpPasses = 0;
            };

            /**
             * Whether this tally is the one actually deciding, which is what the readout describes - so
             * promoting the shadow through sSceneDepthStrategy moves the instrumentation with it.
             */
            bool instrumented() const
            {
                return _depthFiltered == (internal::activeStrategy() == internal::Strategy::DepthPass);
            }

            void startFrame(const std::uint64_t epoch, const D3D11_VIEWPORT& whole)
            {
                const auto finished = rank();
                const bool sameWorld = finished.winner && _lastFrameWorld && sameViewport(finished.winner->viewport, _lastFrameWorld->viewport);
                const std::uint32_t nextCopyAtPass = !finished.winner ? 0 : sameWorld ? (std::min)(finished.winner->passes, _lastFrameWorld->passes) : finished.winner->passes;

                if (instrumented()) {
                    publishFinishedFrame(finished, whole, nextCopyAtPass);
                }

                _copyAtPass = nextCopyAtPass;
                _copyViewport = finished.winner ? finished.winner->viewport : D3D11_VIEWPORT{};
                _lastFrameWorld = finished.winner;

                _frameEpoch = epoch;
                _viewportCount = 0;
                _worldPassesSoFar = 0;
                _groupCapHit = false;
            }

            /**
             * Report the frame that just ended: what it elected and how close that was, whether its copy
             * was reached, and how the two-frame minimum that picks the next copy pass compares with
             * taking this frame's count alone - the pass-count jitter the minimum exists to absorb,
             * counted rather than assumed.
             */
            void publishFinishedFrame(const Ranking& finished, const D3D11_VIEWPORT& whole, const std::uint32_t nextCopyAtPass) const
            {
                if (!finished.winner) {
                    return; // nothing was counted for the frame, so there is nothing to report
                }

                internal::recordElection(internal::Election{
                    .viewport = finished.winner->viewport,
                    .passes = finished.winner->passes,
                    .runnerUpPasses = finished.runnerUpPasses,
                    .groups = static_cast<std::uint32_t>(_viewportCount),
                    .groupCapHit = _groupCapHit,
                    .changedIdentity = _lastFrameWorld.has_value() && !sameViewport(finished.winner->viewport, _lastFrameWorld->viewport),
                });

                // there is only a copy to reach under an upscaler: a world drawn over the whole buffer
                // is tested against where it lies
                if (!sameViewport(finished.winner->viewport, whole)) {
                    internal::recordCopyPlan(_copyAtPass, _worldPassesSoFar, _copyAtPass > 0 && _worldPassesSoFar >= _copyAtPass);
                }

                if (internal::diagnosticsEnabled() && internal::recordComparison(internal::Comparison::CopyPass, nextCopyAtPass == finished.winner->passes)) {
                    logger::info("scene depth: the copy pass differs by policy - the two-frame minimum says pass {}, this frame count alone says {} (the previous frame drew {})",
                        nextCopyAtPass,
                        finished.winner->passes,
                        _lastFrameWorld ? _lastFrameWorld->passes : 0);
                }
            }

            Ranking rank() const
            {
                Ranking out;
                for (std::size_t i = 0; i < _viewportCount; ++i) {
                    const auto& entry = _viewports[i];
                    if (!out.winner || entry.passes > out.winner->passes) {
                        out.runnerUpPasses = out.winner ? out.winner->passes : 0;
                        out.winner = entry;
                    } else if (entry.passes > out.runnerUpPasses) {
                        out.runnerUpPasses = entry.passes;
                    }
                }
                return out;
            }

            // whether this tally is fed every pass into the buffer (false) or only the ones that test
            // depth the way the world does (true)
            const bool _depthFiltered;

            std::uint64_t _frameEpoch = 0;
            std::array<ViewportCount, 4> _viewports{};
            std::size_t _viewportCount = 0;
            bool _groupCapHit = false;

            // the previous frame's world, which with this frame's picks the next frame's pass to copy at
            std::optional<ViewportCount> _lastFrameWorld;
            D3D11_VIEWPORT _copyViewport{};
            std::uint32_t _copyAtPass = 0;
            std::uint32_t _worldPassesSoFar = 0;
        };

        /**
         * The engine's depth buffer, and the frame it was last seen bound in - which is what proves it
         * holds this frame's world. The read-only view is made once per texture: the engine keeps its
         * buffer from frame to frame.
         */
        struct EngineDepth
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> readOnlyView;
            // the owning device as COM identity, resolved once with the texture - see sameDevice
            Microsoft::WRL::ComPtr<IUnknown> deviceIdentity;
            D3D11_TEXTURE2D_DESC desc{};
            // the mip the read-only view addresses, which is the extent that has to match the
            // submitted texture rather than the texture level 0 the desc describes
            UINT viewMipSlice = 0;
            std::uint64_t frameEpoch = 0;
        };

        // The submit hook and the commit hook both run on the render thread, but nothing in the
        // engine promises that, and the cost of being wrong is a torn COM pointer. Guards the four below.
        std::mutex s_captureMutex;
        EngineDepth s_engineDepth;
        // the shipped election over every pass into the buffer, and the shadow over depth-testing
        // passes only; which one decides is sSceneDepthStrategy
        WorldPassTally s_worldPasses{ false };
        WorldPassTally s_depthPassTally{ true };
        // the frame the world's depth was last copied in under an upscaler; 0 is never a frame
        std::uint64_t s_copiedEpoch = 0;

        std::atomic<bool> s_installed = false;
        // a failed install is permanent: the addresses are not going to start matching later, and
        // retrying every frame would re-log the same warning forever
        std::atomic<bool> s_installFailed = false;
        std::atomic<bool> s_requested = false;
        std::atomic<std::uint64_t> s_frameEpoch = 1;
        CommitGraphicsStateFn s_original = nullptr;

        // Which frame the engine's depth buffer was already seen in. Duplicated out of EngineDepth so
        // the dozens of commits after it in a frame know they only count their pass from an atomic
        // read, rather than a mutex acquisition.
        std::atomic<std::uint64_t> s_capturedEpoch = 0;

        // The size the overlay will draw into, published by the submit side and read by the capture
        // side. Separate atomics rather than a D3D11_TEXTURE2D_DESC because the two sides are only
        // the same thread by convention, and a torn read here would silently reject every capture.
        std::atomic<UINT> s_submittedWidth = 0;
        std::atomic<UINT> s_submittedHeight = 0;

        D3D11_VIEWPORT wholeBuffer(const UINT width, const UINT height)
        {
            return D3D11_VIEWPORT{ .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(width),
                .Height = static_cast<float>(height),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f };
        }

        /**
         * The extent of one mip of a texture, which for a depth view is the only extent that matters:
         * a view addressing mip 2 of a 4k buffer binds beside a 1k render target, not a 4k one.
         */
        UINT mipExtent(const UINT extent, const UINT mip)
        {
            return (std::max)(1u, extent >> mip);
        }

        /**
         * Which mip of the texture a depth view addresses. The multisampled dimensions have no mips at
         * all, and anything unexpected is treated as mip 0 - the overwhelmingly common case, and the
         * one the size check then applies unchanged.
         */
        UINT viewMipSlice(const D3D11_DEPTH_STENCIL_VIEW_DESC& desc)
        {
            switch (desc.ViewDimension) {
            case D3D11_DSV_DIMENSION_TEXTURE2D:
                return desc.Texture2D.MipSlice;
            case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
                return desc.Texture2DArray.MipSlice;
            default:
                return 0;
            }
        }

        /**
         * Whether the captured depth can legally and meaningfully back the texture being submitted:
         * the mip the view addresses has to be that texture size, and both have to be sampled the same
         * way. D3D refuses to bind a pair whose extents differ, and a pair differing only in sample
         * count reads silently wrong.
         */
        bool matchesSubmittedLayout(const D3D11_TEXTURE2D_DESC& depth, const UINT depthMip, const D3D11_TEXTURE2D_DESC& submitted)
        {
            return mipExtent(depth.Width, depthMip) == submitted.Width && mipExtent(depth.Height, depthMip) == submitted.Height &&
                   depth.SampleDesc.Count == submitted.SampleDesc.Count && depth.SampleDesc.Quality == submitted.SampleDesc.Quality;
        }

        /**
         * The COM identity of the device a texture belongs to.
         *
         * Asked for as IUnknown rather than kept as the ID3D11Device pointer: COM only guarantees that
         * an object identity is stable through IUnknown, so two interface pointers to one device may
         * legitimately differ - and comparing those raw would reject a perfectly good capture.
         */
        Microsoft::WRL::ComPtr<IUnknown> deviceIdentityOf(ID3D11Texture2D* texture)
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            texture->GetDevice(device.GetAddressOf());
            Microsoft::WRL::ComPtr<IUnknown> identity;
            if (device) {
                device.As(&identity);
            }
            return identity;
        }

        bool sameDevice(const Microsoft::WRL::ComPtr<IUnknown>& depthDevice, ID3D11Texture2D* submitted)
        {
            const auto submittedDevice = deviceIdentityOf(submitted);
            return depthDevice && submittedDevice && depthDevice.Get() == submittedDevice.Get();
        }

        /**
         * Whether this pass draws against the world the way the capture will read it.
         *
         * A pass with depth testing off is not consulting the scene depth at all, and one comparing in
         * a direction that is not an ordering of it - EQUAL, NEVER, ALWAYS - says nothing about which
         * way the world depth runs. Neither is a world pass, so neither should have a say in the
         * election. A null state is the D3D default state, which is depth-enabled and LESS.
         *
         * Not applied to the shipped tally, which counts every pass: this decides what the shadow is
         * fed, and the divergence between the two is what says whether the filter is worth adopting.
         */
        bool isWorldDepthPass(ID3D11DeviceContext* context)
        {
            Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
            UINT stencilRef = 0;
            context->OMGetDepthStencilState(state.GetAddressOf(), &stencilRef);
            if (!state) {
                return true;
            }

            D3D11_DEPTH_STENCIL_DESC desc{};
            state->GetDesc(&desc);
            if (!desc.DepthEnable) {
                internal::countStage(internal::CaptureStage::DepthDisabled);
                return false;
            }
            switch (desc.DepthFunc) {
            case D3D11_COMPARISON_LESS:
            case D3D11_COMPARISON_LESS_EQUAL:
            case D3D11_COMPARISON_GREATER:
            case D3D11_COMPARISON_GREATER_EQUAL:
                return true;
            default:
                internal::countStage(internal::CaptureStage::DepthFuncRejected);
                return false;
            }
        }

        /**
         * A read-only view onto the engine's depth texture: same buffer, but the overlay physically
         * cannot write to it. Stencil is made read-only too for the combined formats, which is
         * required - D3D refuses a partially read-only view of a depth-stencil resource that is
         * still bound for reading elsewhere.
         */
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> makeReadOnlyView(ID3D11Texture2D* texture, D3D11_DEPTH_STENCIL_VIEW_DESC desc)
        {
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            texture->GetDevice(device.GetAddressOf());
            if (!device) {
                return view;
            }

            desc.Flags |= D3D11_DSV_READ_ONLY_DEPTH;
            if (desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
                desc.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
            }
            if (FAILED(device->CreateDepthStencilView(texture, &desc, view.GetAddressOf()))) {
                view.Reset();
            }
            return view;
        }

        /**
         * Name each distinct depth buffer considered at this seam, once, and say whether it is the size
         * we can use. Several passes bind depth per frame; this is what says how many DISTINCT buffers
         * are involved and whether the one we take is the only candidate.
         */
        void logDepthTargetOnce(const D3D11_TEXTURE2D_DESC& desc, const bool usable)
        {
            struct Shape
            {
                UINT width = 0;
                UINT height = 0;
                DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

                bool operator==(const Shape&) const = default;
            };

            static std::array<Shape, 8> seen{};
            static std::size_t seenCount = 0;

            const Shape shape{ .width = desc.Width, .height = desc.Height, .format = desc.Format };
            if (seenCount >= seen.size() || std::ranges::find(seen.begin(), seen.begin() + seenCount, shape) != seen.begin() + seenCount) {
                return;
            }
            seen[seenCount++] = shape;
            logger::info("scene depth: depth buffer {}x{} format {} samples {} - {}",
                desc.Width,
                desc.Height,
                static_cast<int>(desc.Format),
                desc.SampleDesc.Count,
                usable ? "USABLE, matches the submitted size" : "wrong size, skipped");
        }

        /**
         * Report the depth buffer taken, whenever a different shape is taken. With the world line
         * logWorldViewportChange writes, this is the whole diagnostic surface: one session says whether
         * the seam works on this build, what the buffer looks like - and whether a shader can read it,
         * which copying it under an upscaler needs - and where in it the world is drawn.
         */
        void logEngineDepthOnce(const D3D11_TEXTURE2D_DESC& desc, const bool readOnlyViewCreated)
        {
            struct Shape
            {
                UINT width = 0;
                UINT height = 0;
                DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
                bool readOnly = false;

                bool operator==(const Shape&) const = default;
            };

            static std::optional<Shape> lastLogged;

            const Shape shape{ .width = desc.Width, .height = desc.Height, .format = desc.Format, .readOnly = readOnlyViewCreated };
            if (lastLogged == shape) {
                return;
            }
            lastLogged = shape;

            logger::info("scene depth captured: {}x{} format {} samples {} array {} bind flags 0x{:X} (shader-readable: {}), read-only view {}",
                desc.Width,
                desc.Height,
                static_cast<int>(desc.Format),
                desc.SampleDesc.Count,
                desc.ArraySize,
                desc.BindFlags,
                (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 ? "yes" : "no",
                readOnlyViewCreated ? "created" : "FAILED");
        }

        /**
         * Say where the world is drawn whenever that changes - which is how an upscaler's mode, or the
         * lack of one, shows up in the log. Changes within a few pixels are not changes (see
         * WorldPassTally). At most once a second, but a change is never lost: it is compared again every
         * frame, so the latest one is logged once the second is up.
         */
        void logWorldViewportChange(const D3D11_VIEWPORT& viewport, const UINT width, const UINT height)
        {
            static std::optional<D3D11_VIEWPORT> lastLogged;
            static std::chrono::steady_clock::time_point lastLogTime;

            if (lastLogged && WorldPassTally::sameViewport(*lastLogged, viewport)) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (lastLogged && now - lastLogTime < std::chrono::seconds(1)) {
                return;
            }
            lastLogged = viewport;
            lastLogTime = now;

            logger::info("scene depth: the world is drawn at {:.0f},{:.0f} size {:.0f}x{:.0f} of the {}x{} buffer ({:.1f}% x {:.1f}%) - {}",
                viewport.TopLeftX,
                viewport.TopLeftY,
                viewport.Width,
                viewport.Height,
                width,
                height,
                width > 0 ? viewport.Width * 100.0f / static_cast<float>(width) : 0.0f,
                height > 0 ? viewport.Height * 100.0f / static_cast<float>(height) : 0.0f,
                WorldPassTally::sameViewport(viewport, wholeBuffer(width, height)) ? "tested against directly" : "upscaled, depth copied during the frame");
        }

        /**
         * Compare the two elections, and log the first few disagreements with the values behind them:
         * a divergence count on its own says nothing actionable, and by the time one is worth chasing
         * the frame that produced it is long gone.
         */
        void compareWorldRegions(const std::optional<D3D11_VIEWPORT>& shipped, const std::optional<D3D11_VIEWPORT>& filtered)
        {
            if (!shipped || !filtered) {
                // Either the shadow is dormant and nothing fed the filtered tally, or one of the two
                // counted nothing this frame. Neither is a disagreement.
                return;
            }
            if (!internal::recordComparison(internal::Comparison::WorldRegion, WorldPassTally::sameViewport(*shipped, *filtered))) {
                return;
            }
            logger::info(
                "scene depth: the world region differs by policy - every pass says {:.0f},{:.0f} {:.0f}x{:.0f}, depth-testing passes alone say {:.0f},{:.0f} {:.0f}x{:.0f}",
                shipped->TopLeftX,
                shipped->TopLeftY,
                shipped->Width,
                shipped->Height,
                filtered->TopLeftX,
                filtered->TopLeftY,
                filtered->Width,
                filtered->Height);
        }

        void reportFailure(const char* reason)
        {
            // sampled: this runs per committed graphics state, which is many times a frame
            logger::sample(10000, "scene depth capture unavailable: {} [{}]", reason, internal::stageCountersText());
        }

        /**
         * Take the depth buffer bound at this commit as the frame's engine depth, if it is the one
         * already known or one of the submitted size.
         *
         * The engine renders the world into its own G-buffer and only resolves into the texture it hands
         * the compositor, so the colour target here is never the submitted one - which makes matching by
         * identity useless. Size is the criterion that actually matters: D3D requires a depth view to
         * match the dimensions of the render target it is bound beside, and the world is rendered at
         * exactly the submitted resolution.
         */
        bool takeBoundDepth(ID3D11DepthStencilView* depthView, const Microsoft::WRL::ComPtr<ID3D11Resource>& depthResource, const std::uint64_t epoch)
        {
            internal::countStage(internal::CaptureStage::Attempt);
            {
                std::scoped_lock lock(s_captureMutex);
                // single inheritance, so a texture and its ID3D11Resource are the same pointer
                if (depthResource.Get() == static_cast<ID3D11Resource*>(s_engineDepth.texture.Get())) {
                    s_engineDepth.frameEpoch = epoch;
                    s_capturedEpoch.store(epoch, std::memory_order_release);
                    internal::countStage(internal::CaptureStage::Captured);
                    return true;
                }
            }

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (FAILED(depthResource.As(&texture))) {
                internal::countStage(internal::CaptureStage::NotATexture);
                reportFailure("the bound depth view is not a 2D texture");
                return false;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);

            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
            depthView->GetDesc(&viewDesc);
            const UINT mipSlice = viewMipSlice(viewDesc);
            const bool usable = mipExtent(desc.Width, mipSlice) == s_submittedWidth.load(std::memory_order_acquire) &&
                                mipExtent(desc.Height, mipSlice) == s_submittedHeight.load(std::memory_order_acquire);
            logDepthTargetOnce(desc, usable);
            if (!usable) {
                internal::countStage(internal::CaptureStage::WrongSize);
                return false;
            }

            auto readOnlyView = makeReadOnlyView(texture.Get(), viewDesc);
            logEngineDepthOnce(desc, readOnlyView != nullptr);
            if (!readOnlyView) {
                internal::countStage(internal::CaptureStage::ReadOnlyViewFailed);
                reportFailure("a read-only depth view could not be created");
                return false;
            }
            internal::recordBufferShape(desc);
            auto deviceIdentity = deviceIdentityOf(texture.Get());

            std::scoped_lock lock(s_captureMutex);
            s_engineDepth = EngineDepth{ .texture = std::move(texture),
                .readOnlyView = std::move(readOnlyView),
                .deviceIdentity = std::move(deviceIdentity),
                .desc = desc,
                .viewMipSlice = mipSlice,
                .frameEpoch = epoch };
            s_capturedEpoch.store(epoch, std::memory_order_release);
            internal::countStage(internal::CaptureStage::Captured);
            return true;
        }

        /**
         * Count this commit's pass, if it draws into the engine's depth buffer, and when it is the pass
         * to copy the world's depth at under an upscaler, copy it now, while it is still there (see
         * WorldPassTally). The copy is drawn outside the lock.
         *
         * Both tallies are fed - the shipped one every pass, the shadow only the passes that test depth
         * - but only the one sSceneDepthStrategy names decides where and when the copy is taken.
         */
        void countWorldPass(ID3D11DeviceContext* context, ID3D11Resource* depthResource, const std::uint64_t epoch)
        {
            D3D11_VIEWPORT viewport{};
            UINT viewportCount = 1;
            context->RSGetViewports(&viewportCount, &viewport);
            if (viewportCount == 0 || viewport.Width <= 0.0f || viewport.Height <= 0.0f) {
                return;
            }
            // The depth-state read is the one part of this that costs a D3D call per commit, so it
            // is skipped unless something consumes it: the shadow comparison, or the strategy that
            // promotes the filtered tally to the deciding one.
            const auto strategy = internal::activeStrategy();
            const bool useDepthFilter = internal::diagnosticsEnabled() || strategy == internal::Strategy::DepthPass;
            const bool worldDepthPass = useDepthFilter && isWorldDepthPass(context);

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            D3D11_VIEWPORT worldViewport{};
            {
                std::scoped_lock lock(s_captureMutex);
                if (depthResource != static_cast<ID3D11Resource*>(s_engineDepth.texture.Get())) {
                    return;
                }
                const auto whole = wholeBuffer(s_engineDepth.desc.Width, s_engineDepth.desc.Height);
                const auto shipped = s_worldPasses.add(epoch, viewport, whole);
                std::optional<D3D11_VIEWPORT> filtered;
                if (worldDepthPass) {
                    filtered = s_depthPassTally.add(epoch, viewport, whole);
                }

                const auto copyFrom = strategy == internal::Strategy::DepthPass ? filtered : shipped;
                // Direct still counts, so the readout keeps describing what the frame did; it only
                // never acts on it - which is the whole point of that escape hatch
                if (!copyFrom || strategy == internal::Strategy::Direct) {
                    return;
                }
                texture = s_engineDepth.texture;
                worldViewport = *copyFrom;
            }

            if (internal::resampleWorldDepth(context, texture.Get(), worldViewport)) {
                std::scoped_lock lock(s_captureMutex);
                s_copiedEpoch = epoch;
            }
        }

        /**
         * Look at the targets the engine just committed. Runs on the render thread, inside the engine's
         * own frame, for every commit at the hooked call site - dozens a frame - so a commit that binds
         * no colour and depth target together costs one D3D read, and one that does a few more.
         */
        void onGraphicsStateCommitted()
        {
            if (!s_requested.load(std::memory_order_acquire) || internal::activeStrategy() == internal::Strategy::Off) {
                return;
            }
            auto* context = getContext();
            if (!context) {
                internal::countStage(internal::CaptureStage::NoContext);
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorView;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
            context->OMGetRenderTargets(1, colorView.GetAddressOf(), depthView.GetAddressOf());
            // a depth-only pass is a shadow map, never the view we are drawing over
            if (!colorView || !depthView) {
                internal::countStage(internal::CaptureStage::NoTargets);
                return; // far too common to be worth logging
            }
            Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
            depthView->GetResource(depthResource.GetAddressOf());
            if (!depthResource) {
                internal::countStage(internal::CaptureStage::NoDepthResource);
                return;
            }

            const auto epoch = s_frameEpoch.load(std::memory_order_acquire);
            if (s_capturedEpoch.load(std::memory_order_acquire) == epoch) {
                internal::countStage(internal::CaptureStage::AlreadyCaptured);
            } else if (!takeBoundDepth(depthView.Get(), depthResource, epoch)) {
                return;
            }
            countWorldPass(context, depthResource.Get(), epoch);
        }

        /**
         * The detour. The original runs first: we want the state the engine just committed, not the
         * one it is about to replace.
         */
        __declspec(noinline) void hookedCommitGraphicsState(const std::uint8_t firstMode, const std::uint8_t secondMode)
        {
            if (s_original) {
                s_original(firstMode, secondMode);
            }
            onGraphicsStateCommitted();
        }

        /**
         * What the call site holds before we patch it.
         */
        enum class CallSiteState : std::uint8_t
        {
            // not the call we expect; nothing may be written
            Invalid,
            // the engine's own call, straight to CommitGraphicsState
            Untouched,
            // already redirected by another plugin's hook of the same seam, which we chain onto
            AlreadyHooked,
        };

        /**
         * Whether the game executable's image contains the address. REL::Module exposes the base but
         * not the size, so the size comes from the image's own PE headers.
         */
        bool isInsideGameImage(const std::uintptr_t address)
        {
            const std::uintptr_t base = REL::Module::get().base();
            const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
            return address >= base && address < base + ntHeaders->OptionalHeader.SizeOfImage;
        }

        /**
         * Whether `length` bytes at the address are committed, executable memory - checked before
         * reading another plugin's code, or trusting that a call into it is a hook.
         */
        bool isExecutableMemory(const std::uintptr_t address, const std::size_t length)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info)) {
                return false;
            }
            constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            return info.State == MEM_COMMIT && (info.Protect & EXECUTABLE) != 0 && address + length <= regionEnd;
        }

        /**
         * Name the module an existing hook's code belongs to, for the log. A hook written through a
         * trampoline lands first on a stub - jmp qword ptr [rip+0] followed by the absolute address -
         * in memory no module owns, so one such stub is followed to the detour behind it.
         */
        std::string describeHookOwner(std::uintptr_t target)
        {
            constexpr std::array<std::uint8_t, 6> ABSOLUTE_JMP{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
            if (isExecutableMemory(target, ABSOLUTE_JMP.size() + sizeof(std::uintptr_t)) &&
                std::memcmp(reinterpret_cast<const void*>(target), ABSOLUTE_JMP.data(), ABSOLUTE_JMP.size()) == 0) {
                std::memcpy(&target, reinterpret_cast<const void*>(target + ABSOLUTE_JMP.size()), sizeof(target));
            }

            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(target), &module)) {
                return "memory owned by no module";
            }
            std::array<char, MAX_PATH> path{};
            const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
            const std::string_view fullPath(path.data(), length);
            const auto separator = fullPath.find_last_of("\\/");
            return std::string(separator == std::string_view::npos ? fullPath : fullPath.substr(separator + 1));
        }

        /**
         * Prove the addresses are what we think before writing anything, and say whether another
         * plugin hooked the call first.
         *
         * CommitGraphicsState's own first instructions prove the build and the function address. The
         * call site then has to be a CALL landing either on that function, untouched, or on executable
         * code outside the game's image - which can only be another plugin's hook of this call, and is
         * chained onto rather than refused, since several plugins reading depth here is expected. A call
         * landing anywhere else inside the game means the address is not the call we expect, and the hook
         * declines to install.
         */
        CallSiteState inspectCallSite(const std::uintptr_t callsite, const std::uintptr_t commitGraphicsState, std::uintptr_t& currentTarget)
        {
            if (std::memcmp(reinterpret_cast<const void*>(commitGraphicsState), COMMIT_PROLOGUE.data(), COMMIT_PROLOGUE.size()) != 0) {
                logger::warn("scene depth hook not installed: the target function does not start with the expected instructions");
                return CallSiteState::Invalid;
            }

            const auto* callBytes = reinterpret_cast<const std::uint8_t*>(callsite);
            if (callBytes[0] != CALL_REL32_OPCODE) {
                logger::warn("scene depth hook not installed: no call instruction at the expected call site");
                return CallSiteState::Invalid;
            }

            std::int32_t relative = 0;
            std::memcpy(&relative, callBytes + 1, sizeof(relative));
            currentTarget = callsite + CALL_REL32_LENGTH + static_cast<std::intptr_t>(relative);
            if (currentTarget == commitGraphicsState) {
                return CallSiteState::Untouched;
            }
            if (!isInsideGameImage(currentTarget) && isExecutableMemory(currentTarget, 1)) {
                return CallSiteState::AlreadyHooked;
            }

            logger::warn("scene depth hook not installed: the call site targets neither the expected function nor another plugin's hook");
            return CallSiteState::Invalid;
        }
    }

    bool ensureInstalled()
    {
        // called every frame from the submit hook, so the common path is one relaxed atomic read
        if (s_installed.load(std::memory_order_acquire)) {
            return true;
        }
        if (s_installFailed.load(std::memory_order_acquire)) {
            return false;
        }

        static std::mutex installMutex;
        std::scoped_lock lock(installMutex);
        if (s_installed.load(std::memory_order_acquire)) {
            return true;
        }
        s_installFailed.store(true, std::memory_order_release);

        if (!REL::Module::IsVR()) {
            logger::warn("scene depth hook not installed: addresses are Fallout 4 VR only");
            return false;
        }

        const auto callsite = REL::Offset(CAPTURE_CALLSITE_RVA).address();
        const auto commitGraphicsState = REL::Offset(COMMIT_GRAPHICS_STATE_RVA).address();
        std::uintptr_t currentTarget = 0;
        const CallSiteState state = inspectCallSite(callsite, commitGraphicsState, currentTarget);
        if (state == CallSiteState::Invalid) {
            return false;
        }

        // write_call hands back whatever the call pointed at: CommitGraphicsState itself, or the hook
        // already there, which calls through to it in turn - so calling it as the original keeps both
        s_original =
            reinterpret_cast<CommitGraphicsStateFn>(F4SE::GetTrampoline().write_call<CALL_REL32_LENGTH>(callsite, reinterpret_cast<std::uintptr_t>(&hookedCommitGraphicsState)));
        if (!s_original) {
            logger::warn("scene depth hook not installed: the trampoline write returned no original");
            return false;
        }

        s_installed.store(true, std::memory_order_release);
        s_installFailed.store(false, std::memory_order_release);
        if (state == CallSiteState::AlreadyHooked) {
            logger::info("scene depth capture hook installed, chained after an existing hook of the same call in {}", describeHookOwner(currentTarget));
        } else {
            logger::info("scene depth capture hook installed");
        }
        return true;
    }

    bool isInstalled()
    {
        return s_installed.load(std::memory_order_acquire);
    }

    /**
     * Turn the capture on or off. Turning it off also frees the upscaler's depth copy, which is as
     * large as the eye texture - so the submit hook turns it off on frames with nothing to draw.
     */
    void setCaptureRequested(const bool requested)
    {
        if (s_requested.exchange(requested, std::memory_order_acq_rel) && !requested) {
            internal::releaseResampledWorldDepth();
        }
    }

    void setSubmittedTexture(ID3D11Texture2D* texture)
    {
        if (!texture) {
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        s_submittedWidth.store(desc.Width, std::memory_order_release);
        s_submittedHeight.store(desc.Height, std::memory_order_release);
    }

    /**
     * Hand out this frame's depth lined up with the submitted texture: the engine's own buffer when the
     * world was drawn over all of it, or, when an upscaler drew the world into part of it, the copy made
     * during the frame (see WorldPassTally) - by now the engine's buffer is cleared.
     *
     * The copy may be the previous frame's, when this frame fell short of the pass it is made at; a
     * frame's lag goes unseen where overlays blinking to the top would not. With no copy that recent,
     * nothing is handed out, and overlays draw on top rather than being hidden by depth that does not
     * line up with them.
     */
    SceneDepth acquireForSubmittedTexture(ID3D11Texture2D* texture)
    {
        const auto strategy = internal::activeStrategy();
        if (strategy == internal::Strategy::Off) {
            internal::recordAcquire(internal::AcquireOutcome::StrategyOff);
            return {};
        }
        if (!texture) {
            internal::recordAcquire(internal::AcquireOutcome::NoTexture);
            return {};
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        ID3D11DepthStencilView* engineView = nullptr;
        Microsoft::WRL::ComPtr<IUnknown> depthDevice;
        D3D11_TEXTURE2D_DESC depthDesc{};
        UINT depthMip = 0;
        std::optional<D3D11_VIEWPORT> shippedWorld;
        std::optional<D3D11_VIEWPORT> filteredWorld;
        std::uint64_t epoch = 0;
        std::uint64_t copiedEpoch = 0;
        {
            std::scoped_lock lock(s_captureMutex);
            epoch = s_frameEpoch.load(std::memory_order_acquire);
            if (!s_engineDepth.readOnlyView) {
                internal::recordAcquire(internal::AcquireOutcome::NoCapture);
                return {};
            }
            if (s_engineDepth.frameEpoch != epoch) {
                // not seen this frame; occluding against it would lag the world
                internal::recordAcquire(internal::AcquireOutcome::StaleCapture);
                return {};
            }
            // borrowed: EngineDepth keeps its reference until a different buffer replaces it
            engineView = s_engineDepth.readOnlyView.Get();
            depthDevice = s_engineDepth.deviceIdentity;
            depthDesc = s_engineDepth.desc;
            depthMip = s_engineDepth.viewMipSlice;
            shippedWorld = s_worldPasses.mostUsedViewport(epoch);
            filteredWorld = s_depthPassTally.mostUsedViewport(epoch);
            copiedEpoch = s_copiedEpoch;
        }

        // re-checked here rather than trusted: binding a mismatched depth view is a device error
        if (!matchesSubmittedLayout(depthDesc, depthMip, desc)) {
            internal::recordAcquire(internal::AcquireOutcome::SubmittedLayoutMismatch);
            return {};
        }
        if (!sameDevice(depthDevice, texture)) {
            internal::recordAcquire(internal::AcquireOutcome::DifferentDevice);
            return {};
        }

        compareWorldRegions(shippedWorld, filteredWorld);

        const D3D11_VIEWPORT whole = wholeBuffer(desc.Width, desc.Height);
        const auto& elected = strategy == internal::Strategy::DepthPass ? filteredWorld : shippedWorld;
        const D3D11_VIEWPORT viewport = elected.value_or(whole);
        logWorldViewportChange(viewport, desc.Width, desc.Height);

        if (strategy == internal::Strategy::Direct || WorldPassTally::sameViewport(viewport, whole)) {
            internal::releaseResampledWorldDepth(); // no upscaler, or no longer: the copy is not needed
            internal::recordAcquire(internal::AcquireOutcome::Ok);
            return SceneDepth{ .readOnlyView = engineView, .comparison = WORLD_DEPTH_COMPARISON };
        }

        const bool copyRecent = copiedEpoch != 0 && copiedEpoch + WORLD_COPY_MAX_AGE_FRAMES >= epoch;
        auto* copy = copyRecent ? internal::resampledWorldDepth(desc.Width, desc.Height) : nullptr;
        if (!copy) {
            internal::recordAcquire(internal::AcquireOutcome::NoWorldCopy);
            return {};
        }
        internal::recordAcquire(internal::AcquireOutcome::OkResampled);
        return SceneDepth{ .readOnlyView = copy, .comparison = WORLD_DEPTH_COMPARISON };
    }

    FrameScope::~FrameScope()
    {
        s_frameEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
}
