#include "SceneDepthCapture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

#include <windows.h>
#include <wrl/client.h>

#include "RenderUtils.h"

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

        /**
         * One frame's captured depth, with everything needed to prove it is still the right one.
         */
        struct Capture
        {
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> readOnlyView;
            D3D11_TEXTURE2D_DESC textureDesc{};
            D3D11_COMPARISON_FUNC comparison = D3D11_COMPARISON_LESS_EQUAL;
            std::uint64_t frameEpoch = 0;
        };

        // The submit hook and the commit hook both run on the render thread, but nothing in the
        // engine promises that, and the cost of being wrong is a torn COM pointer.
        std::mutex s_captureMutex;

        std::atomic<bool> s_installed = false;
        // a failed install is permanent: the addresses are not going to start matching later, and
        // retrying every frame would re-log the same warning forever
        std::atomic<bool> s_installFailed = false;
        std::atomic<bool> s_requested = false;
        std::atomic<std::uint64_t> s_frameEpoch = 1;
        CommitGraphicsStateFn s_original = nullptr;
        Capture s_capture;

        // Which frame we already have a capture for. Duplicated out of the Capture so the common
        // path - the dozens of commits per frame that arrive after the one we kept - is an atomic
        // read rather than a mutex acquisition.
        std::atomic<std::uint64_t> s_capturedEpoch = 0;

        // The size the overlay will draw into, published by the submit side and read by the capture
        // side. Separate atomics rather than a D3D11_TEXTURE2D_DESC because the two sides are only
        // the same thread by convention, and a torn read here would silently reject every capture.
        std::atomic<UINT> s_submittedWidth = 0;
        std::atomic<UINT> s_submittedHeight = 0;

        /**
         * COM identity: the same object can be handed out behind different interface pointers, so
         * only the IUnknown a QueryInterface returns is safe to compare.
         */
        Microsoft::WRL::ComPtr<IUnknown> comIdentity(IUnknown* object)
        {
            Microsoft::WRL::ComPtr<IUnknown> identity;
            if (object) {
                object->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(identity.GetAddressOf()));
            }
            return identity;
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
            if (!texture) {
                return view;
            }
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
         * The comparison the engine drew the world with, read rather than assumed: a reversed-Z
         * pipeline uses GREATER, and testing overlay geometry with the wrong sense would hide
         * exactly what should be visible.
         */
        D3D11_COMPARISON_FUNC boundDepthComparison(ID3D11DeviceContext* context)
        {
            Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
            UINT stencilRef = 0;
            context->OMGetDepthStencilState(state.GetAddressOf(), &stencilRef);
            if (!state) {
                return D3D11_COMPARISON_LESS_EQUAL;
            }
            D3D11_DEPTH_STENCIL_DESC desc{};
            state->GetDesc(&desc);
            return desc.DepthFunc;
        }

        /**
         * Report what the first capture found, and again whenever its shape changes. This is the
         * whole diagnostic surface: one game session says whether the seam works on this build,
         * what the depth buffer looks like, and which way its comparison runs.
         */
        void logCaptureOnce(const D3D11_TEXTURE2D_DESC& depthDesc, const D3D11_COMPARISON_FUNC comparison, const bool readOnlyViewCreated)
        {
            struct Shape
            {
                UINT width = 0;
                UINT height = 0;
                DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
                D3D11_COMPARISON_FUNC comparison = D3D11_COMPARISON_LESS_EQUAL;
                bool readOnly = false;

                bool operator==(const Shape&) const = default;
            };

            static Shape lastLogged;
            static bool everLogged = false;

            const Shape shape{ .width = depthDesc.Width, .height = depthDesc.Height, .format = depthDesc.Format, .comparison = comparison, .readOnly = readOnlyViewCreated };
            if (everLogged && lastLogged == shape) {
                return;
            }
            everLogged = true;
            lastLogged = shape;

            logger::info("scene depth captured: {}x{} format {} samples {} array {}, engine comparison {}, read-only view {}",
                depthDesc.Width,
                depthDesc.Height,
                static_cast<int>(depthDesc.Format),
                depthDesc.SampleDesc.Count,
                depthDesc.ArraySize,
                static_cast<int>(comparison),
                readOnlyViewCreated ? "created" : "FAILED");
        }

        /**
         * Name each distinct depth buffer bound at this seam, once, and say whether it is the size we
         * can use. Several passes bind depth per frame; this is what says how many DISTINCT buffers
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
            if (std::ranges::find(seen.begin(), seen.begin() + seenCount, shape) != seen.begin() + seenCount) {
                return;
            }
            if (seenCount >= seen.size()) {
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

        void reportFailure(const char* reason)
        {
            // sampled: this runs per committed graphics state, which is many times a frame
            logger::sample(10000, "scene depth capture unavailable: {}", reason);
        }

        /**
         * Read whatever depth is bound right now, and keep it if it belongs to a texture the game is
         * submitting. Runs on the render thread, inside the engine's own frame.
         */
        void captureBoundDepth()
        {
            if (!s_requested.load(std::memory_order_acquire)) {
                return;
            }
            // One capture per frame is enough, and this runs on dozens of commits per frame, so the
            // early-out has to be cheaper than a lock.
            const auto epoch = s_frameEpoch.load(std::memory_order_acquire);
            if (s_capturedEpoch.load(std::memory_order_acquire) == epoch) {
                return;
            }

            auto* context = getContext();
            if (!context) {
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorView;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
            context->OMGetRenderTargets(1, colorView.GetAddressOf(), depthView.GetAddressOf());
            // a depth-only pass is a shadow map, never the view we are drawing over
            if (!colorView || !depthView) {
                return; // far too common to be worth logging
            }

            Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
            depthView->GetResource(depthResource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
            if (!depthResource || FAILED(depthResource.As(&depthTexture)) || !depthTexture) {
                reportFailure("the bound depth view is not a 2D texture");
                return;
            }

            D3D11_TEXTURE2D_DESC depthDesc{};
            depthTexture->GetDesc(&depthDesc);

            // The engine renders the world into its own G-buffer and only resolves into the texture
            // it hands the compositor, so the colour target here is never the submitted one - which
            // makes matching by identity useless. Size is the criterion that actually matters: D3D
            // requires a depth view to match the dimensions of the render target it is bound beside,
            // and the world is rendered at exactly the submitted resolution.
            const bool usable = depthDesc.Width == s_submittedWidth.load(std::memory_order_acquire) && depthDesc.Height == s_submittedHeight.load(std::memory_order_acquire);
            logDepthTargetOnce(depthDesc, usable);
            if (!usable) {
                return;
            }
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
            depthView->GetDesc(&viewDesc);

            auto readOnlyView = makeReadOnlyView(depthTexture.Get(), viewDesc);
            const auto comparison = boundDepthComparison(context);
            logCaptureOnce(depthDesc, comparison, readOnlyView != nullptr);
            if (!readOnlyView) {
                reportFailure("a read-only depth view could not be created");
                return;
            }

            std::scoped_lock lock(s_captureMutex);
            // no separate reference to the texture: a view AddRefs its resource, so the read-only
            // view is what keeps the engine buffer alive for as long as we hold it
            s_capture.readOnlyView = std::move(readOnlyView);
            s_capture.textureDesc = depthDesc;
            s_capture.comparison = comparison;
            s_capture.frameEpoch = epoch;
            s_capturedEpoch.store(epoch, std::memory_order_release);
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
            captureBoundDepth();
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

    void setCaptureRequested(const bool requested)
    {
        s_requested.store(requested, std::memory_order_release);
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

    SceneDepth acquireForSubmittedTexture(ID3D11Texture2D* texture)
    {
        if (!texture) {
            return {};
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        std::scoped_lock lock(s_captureMutex);
        if (!s_capture.readOnlyView) {
            return {};
        }
        if (s_capture.frameEpoch != s_frameEpoch.load(std::memory_order_acquire)) {
            return {}; // captured for an earlier frame; occluding against it would lag the world
        }
        // re-checked here rather than trusted: binding a mismatched depth view is a device error
        if (s_capture.textureDesc.Width != desc.Width || s_capture.textureDesc.Height != desc.Height) {
            return {};
        }
        return SceneDepth{ .readOnlyView = s_capture.readOnlyView.Get(), .comparison = s_capture.comparison };
    }

    void advanceFrame()
    {
        s_frameEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
}
