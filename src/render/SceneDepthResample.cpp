#include "SceneDepthResample.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include "../perf/GpuTimer.h"
#include "../perf/Perf.h"
#include "RenderUtils.h"
#include "SceneDepthShared.h"

namespace f4cf::render::sceneDepth::internal
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        // the function the copy is timed as
        constexpr const char* RESAMPLE_PERF_FUNCTION = "f4cf::render::sceneDepth::resampleWorldDepth";

        // The GPU time of the copy, a root of its own in the gpu tree: it runs inside the engine's frame, not under
        // Submit. The framework mods share one copy, so only the mod that drew a frame's copy records it. Constructed
        // with the DLL, so the first copy does not take the perf registry's lock.
        perf::Site s_resampleGpuSite(RESAMPLE_PERF_FUNCTION, nullptr, __FILE__, __LINE__, perf::SiteKind::Gpu);
        perf::Site s_resampleDrawGpuSite(RESAMPLE_PERF_FUNCTION, "draw", __FILE__, __LINE__, perf::SiteKind::Gpu);
        perf::GpuTimer s_resampleGpuTimer(s_resampleGpuSite, "gpu.sceneDepthCopyMs");

        // One triangle that covers the whole target, from the vertex id alone, so the pass needs no
        // vertex buffer or input layout: (-1,1), (3,1), (-1,-3) in clip space.
        constexpr const char* K_VERTEX_SHADER_SOURCE = R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

        // Each output pixel takes the engine depth at the matching point of the world's viewport. Load
        // rather than Sample: depth must not be filtered, since an average of a near and a far surface
        // is a surface that does not exist.
        constexpr const char* K_PIXEL_SHADER_SOURCE = R"(
Texture2D<float> engineDepth : register(t0);

cbuffer Resample : register(b0)
{
    float2 scale;
    float2 offset;
    float2 lastTexel;
    float2 padding;
};

float main(float4 position : SV_Position) : SV_Depth
{
    float2 texel = min(position.xy * scale + offset, lastTexel);
    return engineDepth.Load(int3(texel, 0));
}
)";

        struct alignas(16) ResampleConstants
        {
            float scale[2];
            float offset[2];
            float lastTexel[2];
            float padding[2];

            bool operator==(const ResampleConstants&) const = default;
        };

        // Render thread only. The pipeline is built once and kept - it is small; the view of the engine's
        // texture follows that texture, and is released when nothing of this mod draws against the copy.
        bool s_pipelineFailed = false;
        ComPtr<ID3D11VertexShader> s_vertexShader;
        ComPtr<ID3D11PixelShader> s_pixelShader;
        ComPtr<ID3D11Buffer> s_constants;
        ComPtr<ID3D11DepthStencilState> s_writeAlways;
        ComPtr<ID3D11RasterizerState> s_rasterizer;

        // what the constant buffer holds, so a frame whose viewport has not changed skips the upload
        std::optional<ResampleConstants> s_uploadedConstants;

        // The copy itself is in the struct every framework mod shares (SharedSceneDepthV1); this mod's view of the
        // engine's texture is its own.
        ID3D11Texture2D* s_engineTexture = nullptr;
        ComPtr<ID3D11ShaderResourceView> s_engineView;

        // viewports within this many pixels are one, as in the capture's election, so two mods that round the same
        // world differently still share one copy
        constexpr float SHARED_VIEWPORT_PIXELS = 8.0f;

        // logged once each: using another mod's copy, and two mods' worlds that differ
        bool s_loggedSharedCopy = false;
        bool s_loggedViewportConflict = false;

        bool sameViewport(const D3D11_VIEWPORT& a, const D3D11_VIEWPORT& b)
        {
            return std::abs(a.TopLeftX - b.TopLeftX) <= SHARED_VIEWPORT_PIXELS && std::abs(a.TopLeftY - b.TopLeftY) <= SHARED_VIEWPORT_PIXELS &&
                   std::abs(a.Width - b.Width) <= SHARED_VIEWPORT_PIXELS && std::abs(a.Height - b.Height) <= SHARED_VIEWPORT_PIXELS;
        }

        /**
         * This mod's DLL, which a copy is marked with.
         */
        HMODULE ownModule()
        {
            static const HMODULE module = [] {
                HMODULE found = nullptr;
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&ownModule), &found);
                return found;
            }();
            return module;
        }

        std::string moduleName(const HMODULE module)
        {
            std::array<char, MAX_PATH> path{};
            const DWORD length = module ? GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size())) : 0;
            const std::string_view fullPath(path.data(), length);
            const auto separator = fullPath.find_last_of("\\/");
            return std::string(separator == std::string_view::npos ? fullPath : fullPath.substr(separator + 1));
        }

        /**
         * Whether this frame's copy of this world is in the shared struct already, made by a mod whose pass came
         * first. The caller holds the lock.
         */
        bool copiedThisFrame(const SharedSceneDepthV1& shared, const D3D11_TEXTURE2D_DESC& engineDesc, const D3D11_VIEWPORT& worldViewport)
        {
            if (!shared.copy || shared.copiedFrame != currentFrame(shared) || shared.copyWidth != engineDesc.Width || shared.copyHeight != engineDesc.Height) {
                return false;
            }
            if (!sameViewport(shared.copiedViewport, worldViewport)) {
                if (!s_loggedViewportConflict) {
                    s_loggedViewportConflict = true;
                    logger::warn("scene depth resample: {} copied the world at {:.0f}x{:.0f} this frame, this mod elected {:.0f}x{:.0f}; the copy is drawn again",
                        moduleName(shared.copiedBy),
                        shared.copiedViewport.Width,
                        shared.copiedViewport.Height,
                        worldViewport.Width,
                        worldViewport.Height);
                }
                return false;
            }
            return true;
        }

        bool compileShader(const char* source, const char* name, const char* target, ComPtr<ID3DBlob>& blob)
        {
            ComPtr<ID3DBlob> errors;
            const HRESULT hr =
                D3DCompile(source, std::strlen(source), name, nullptr, nullptr, "main", target, D3DCOMPILE_ENABLE_STRICTNESS, 0, blob.GetAddressOf(), errors.GetAddressOf());
            if (FAILED(hr)) {
                logger::error("{} compile failed: {}", name, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler output");
                return false;
            }
            return true;
        }

        /**
         * The shaders and states, which never change. A failure is permanent, so it is logged once
         * rather than retried every frame.
         */
        bool ensurePipeline(ID3D11Device* device)
        {
            if (s_pixelShader) {
                return true;
            }
            if (s_pipelineFailed) {
                return false;
            }
            s_pipelineFailed = true;

            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            if (!compileShader(K_VERTEX_SHADER_SOURCE, "F4CFSceneDepthResampleVS", "vs_5_0", vsBlob) ||
                !compileShader(K_PIXEL_SHADER_SOURCE, "F4CFSceneDepthResamplePS", "ps_5_0", psBlob)) {
                return false;
            }

            D3D11_BUFFER_DESC constantsDesc{};
            constantsDesc.ByteWidth = sizeof(ResampleConstants);
            constantsDesc.Usage = D3D11_USAGE_DYNAMIC;
            constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            constantsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            // every pixel is written whatever was there, which is also what clears the previous frame's
            D3D11_DEPTH_STENCIL_DESC depthDesc{};
            depthDesc.DepthEnable = TRUE;
            depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

            D3D11_RASTERIZER_DESC rasterizerDesc{};
            rasterizerDesc.FillMode = D3D11_FILL_SOLID;
            rasterizerDesc.CullMode = D3D11_CULL_NONE;
            rasterizerDesc.DepthClipEnable = FALSE;

            if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, s_vertexShader.GetAddressOf())) ||
                FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, s_pixelShader.GetAddressOf())) ||
                FAILED(device->CreateBuffer(&constantsDesc, nullptr, s_constants.GetAddressOf())) ||
                FAILED(device->CreateDepthStencilState(&depthDesc, s_writeAlways.GetAddressOf())) ||
                FAILED(device->CreateRasterizerState(&rasterizerDesc, s_rasterizer.GetAddressOf()))) {
                logger::error("scene depth resample: pipeline creation failed");
                s_pixelShader.Reset();
                return false;
            }

            s_pipelineFailed = false;
            return true;
        }

        /**
         * The format a shader reads a depth texture's depth through - only a typeless texture can be
         * both a depth target and shader-readable, and each typeless depth format has one such view.
         */
        DXGI_FORMAT depthReadFormat(const DXGI_FORMAT textureFormat)
        {
            switch (textureFormat) {
            case DXGI_FORMAT_R24G8_TYPELESS:
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            case DXGI_FORMAT_R32_TYPELESS:
                return DXGI_FORMAT_R32_FLOAT;
            case DXGI_FORMAT_R16_TYPELESS:
                return DXGI_FORMAT_R16_UNORM;
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        /**
         * A shader view of the engine's depth, kept while the engine keeps the same texture. The view
         * holds a reference, so the texture cannot be freed and replaced at the same address under it.
         */
        bool ensureEngineView(ID3D11Device* device, ID3D11Texture2D* engineDepth, const D3D11_TEXTURE2D_DESC& engineDesc)
        {
            if (s_engineView && s_engineTexture == engineDepth) {
                return true;
            }
            s_engineView.Reset();
            s_engineTexture = nullptr;

            const DXGI_FORMAT readFormat = depthReadFormat(engineDesc.Format);
            if ((engineDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 || readFormat == DXGI_FORMAT_UNKNOWN || engineDesc.SampleDesc.Count != 1) {
                logger::sample(10000,
                    "scene depth resample: the engine depth (format {}, bind flags 0x{:X}, samples {}) cannot be read by a shader",
                    static_cast<int>(engineDesc.Format),
                    engineDesc.BindFlags,
                    engineDesc.SampleDesc.Count);
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
            viewDesc.Format = readFormat;
            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipLevels = 1;
            if (FAILED(device->CreateShaderResourceView(engineDepth, &viewDesc, s_engineView.GetAddressOf()))) {
                logger::sample(10000, "scene depth resample: could not create a shader view of the engine depth (format {})", static_cast<int>(engineDesc.Format));
                s_engineView.Reset();
                return false;
            }
            s_engineTexture = engineDepth;
            return true;
        }

        /**
         * The copy's depth buffer in the shared struct, the size of the engine's. A float format holds whatever
         * precision the engine's has, so the copy compares as the original would. A new one has undefined contents,
         * so it is not handed out before a pass writes it. The caller holds the lock.
         */
        bool ensureCopy(SharedSceneDepthV1& shared, ID3D11Device* device, const UINT width, const UINT height)
        {
            if (shared.copy && shared.copyWidth == width && shared.copyHeight == height) {
                return true;
            }
            releaseSharedCopy(shared);

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_D32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            D3D11_DEPTH_STENCIL_VIEW_DESC readOnlyDesc{};
            readOnlyDesc.Format = DXGI_FORMAT_D32_FLOAT;
            readOnlyDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            readOnlyDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;

            if (FAILED(device->CreateTexture2D(&desc, nullptr, &shared.copy)) || FAILED(device->CreateDepthStencilView(shared.copy, nullptr, &shared.copyWriteView)) ||
                FAILED(device->CreateDepthStencilView(shared.copy, &readOnlyDesc, &shared.copyReadOnlyView))) {
                logger::sample(10000, "scene depth resample: could not create the {}x{} depth copy", width, height);
                releaseSharedCopy(shared);
                return false;
            }
            shared.copyWidth = width;
            shared.copyHeight = height;
            return true;
        }

        /**
         * Whether the pass has unordered-access views bound to the output merger. Restoring render
         * targets unbinds the unordered-access views in their slots, so drawing in the middle of such a
         * pass would take them from the draw the engine is about to make.
         */
        bool outputMergerHasUnorderedAccessViews(ID3D11DeviceContext* context)
        {
            std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> views{};
            context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, static_cast<UINT>(views.size()), views.data());
            bool any = false;
            for (auto* view : views) {
                if (view) {
                    any = true;
                    view->Release();
                }
            }
            return any;
        }

        /**
         * Map the output pixels onto the world's viewport, clamped to its last texel so no pixel reads
         * past it into the rest of the buffer. Uploaded only when it changed, which for a fixed upscaler
         * quality is once.
         */
        bool uploadConstants(ID3D11DeviceContext* context, const D3D11_VIEWPORT& worldViewport, const D3D11_TEXTURE2D_DESC& engineDesc)
        {
            const auto width = static_cast<float>(engineDesc.Width);
            const auto height = static_cast<float>(engineDesc.Height);
            const ResampleConstants constants{
                .scale = { worldViewport.Width / width, worldViewport.Height / height },
                .offset = { worldViewport.TopLeftX, worldViewport.TopLeftY },
                .lastTexel = { (std::max)(0.0f, (std::min)(worldViewport.TopLeftX + worldViewport.Width, width) - 1.0f),
                    (std::max)(0.0f, (std::min)(worldViewport.TopLeftY + worldViewport.Height, height) - 1.0f) },
                .padding = { 0.0f, 0.0f },
            };
            if (s_uploadedConstants == constants) {
                return true;
            }

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(s_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                s_uploadedConstants.reset();
                return false;
            }
            *static_cast<ResampleConstants*>(mapped.pData) = constants;
            context->Unmap(s_constants.Get(), 0);
            s_uploadedConstants = constants;
            return true;
        }
    }

    /**
     * Draw the resample pass inside a pipeline snapshot of its own, into the copy every framework mod shares, or
     * use the one another mod drew this frame. Our target is bound before the engine's depth, since D3D drops a
     * shader view of a texture still bound as an output, and the engine's depth may be bound as one right now.
     */
    bool resampleWorldDepth(ID3D11DeviceContext* context, ID3D11Texture2D* engineDepth, const D3D11_VIEWPORT& worldViewport)
    {
        if (!context || !engineDepth) {
            return false;
        }
        D3D11_TEXTURE2D_DESC engineDesc{};
        engineDepth->GetDesc(&engineDesc);

        auto& shared = sharedSceneDepth();
        const SharedSceneDepthLock lock(shared);
        if (copiedThisFrame(shared, engineDesc, worldViewport)) {
            if (!s_loggedSharedCopy && shared.copiedBy != ownModule()) {
                s_loggedSharedCopy = true;
                logger::info("scene depth resample: using the depth copy {} made this frame, which the framework mods share", moduleName(shared.copiedBy));
            }
            return true;
        }

        if (outputMergerHasUnorderedAccessViews(context)) {
            logger::sample(10000, "scene depth resample: skipped a pass with unordered-access views bound, which restoring its render targets would unbind");
            return false;
        }

        ComPtr<ID3D11Device> device;
        context->GetDevice(device.GetAddressOf());
        if (!device || !ensurePipeline(device.Get()) || !ensureEngineView(device.Get(), engineDepth, engineDesc) ||
            !ensureCopy(shared, device.Get(), engineDesc.Width, engineDesc.Height)) {
            return false;
        }

        const ScopedPipelineState passState(context);
        if (!uploadConstants(context, worldViewport, engineDesc)) {
            return false;
        }

        const auto gpuFrame = s_resampleGpuTimer.begin(context);
        context->OMSetRenderTargets(0, nullptr, shared.copyWriteView);
        context->OMSetDepthStencilState(s_writeAlways.Get(), 0);
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(engineDesc.Width);
        viewport.Height = static_cast<float>(engineDesc.Height);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(s_rasterizer.Get());

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(s_vertexShader.Get(), nullptr, 0);
        context->PSSetShader(s_pixelShader.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, s_constants.GetAddressOf());
        ID3D11ShaderResourceView* engineView = s_engineView.Get();
        context->PSSetShaderResources(0, 1, &engineView);

        context->Draw(3, 0);
        gpuFrame.mark(s_resampleDrawGpuSite);
        shared.copiedFrame = currentFrame(shared);
        shared.copiedViewport = worldViewport;
        shared.copiedBy = ownModule();
        return true;
    }

    ID3D11DepthStencilView* resampledWorldDepth(const UINT width, const UINT height)
    {
        auto& shared = sharedSceneDepth();
        const SharedSceneDepthLock lock(shared);
        return shared.copiedFrame != 0 && shared.copyWidth == width && shared.copyHeight == height ? shared.copyReadOnlyView : nullptr;
    }

    void releaseResampledWorldDepth()
    {
        s_engineView.Reset();
        s_engineTexture = nullptr;
    }
}
