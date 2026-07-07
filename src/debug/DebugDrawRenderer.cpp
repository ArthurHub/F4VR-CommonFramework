#include "DebugDrawRenderer.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>

#include "../../external/openvr/openvr.h"
#include "../f4vr/F4VROffsets.h"

#include "RE/Bethesda/BSGraphics.h"

// This file is a port of ROCK's DebugBodyOverlay wire/text renderer (reference library
// github-repos/gold/ROCK/src/physics-interaction/debug/DebugBodyOverlay.cpp) with the physics-body
// extraction and hknp shape decoding stripped, per knowledge-base/debug_draw_overlay.md. Line-level
// citations below reference that file.
namespace f4cf::debug::renderer
{
    namespace
    {
        /**
         * Vertex layout shared by the wire and text pipelines (POS float3).
         */
        struct Vertex
        {
            float x;
            float y;
            float z;
        };

        /**
         * Per-frame camera constants (register b0): both eyes' view-projection + posAdjust for the
         * stereo-instancing shader. ROCK DebugBodyOverlay.cpp:156-160.
         */
        struct alignas(16) PerFrameVSData
        {
            DirectX::XMMATRIX matProjView[2];
            DirectX::XMFLOAT4 posAdjust[2];
        };

        /**
         * Per-draw constants (register b1): model matrix + flat color. ROCK DebugBodyOverlay.cpp:162-166.
         */
        struct alignas(16) PerObjectVSData
        {
            DirectX::XMMATRIX matModel;
            float color[4];
        };

        /**
         * Full D3D pipeline state snapshot taken before drawing and restored after — we draw in the
         * middle of the game's own pipeline, so missing a single field visibly corrupts the game
         * frame. ROCK DebugBodyOverlay.cpp:168-195 (state list) — NOT optional.
         */
        struct SavedState
        {
            ID3D11VertexShader* vs = nullptr;
            ID3D11PixelShader* ps = nullptr;
            ID3D11ClassInstance* vsInstances[256] = {};
            ID3D11ClassInstance* psInstances[256] = {};
            UINT vsInstanceCount = 0;
            UINT psInstanceCount = 0;
            ID3D11Buffer* vsCBs[2] = {};
            ID3D11InputLayout* inputLayout = nullptr;
            D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            ID3D11RasterizerState* rasterizerState = nullptr;
            ID3D11DepthStencilState* depthStencilState = nullptr;
            UINT stencilRef = 0;
            ID3D11BlendState* blendState = nullptr;
            FLOAT blendFactor[4] = {};
            UINT sampleMask = 0;
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            ID3D11DepthStencilView* dsv = nullptr;
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            UINT numViewports = 0;
            ID3D11Buffer* vertexBuffer = nullptr;
            UINT vbStride = 0;
            UINT vbOffset = 0;
            ID3D11Buffer* indexBuffer = nullptr;
            DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
            UINT ibOffset = 0;
        };

        /**
         * RTV over the submitted eye texture, cached keyed by texture pointer + desc — the texture
         * is stable frame-to-frame so this avoids an RTV creation per frame. ROCK :197-202.
         */
        struct CachedRenderTargetView
        {
            ID3D11Texture2D* texture = nullptr;
            D3D11_TEXTURE2D_DESC desc{};
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        };

        // game->render thread handoff (ROCK :204-206): render side only reads s_frame under the
        // mutex and checks the atomic before doing any work at all
        internal::RenderFrame s_frame;
        std::mutex s_frameMutex;
        std::atomic<bool> s_enabled{ false };

        bool s_installed = false;
        bool s_d3dInitialized = false;
        bool s_loggedNotVR = false;
        bool s_loggedNoDevice = false;
        bool s_loggedNoCompositor = false;
        bool s_loggedD3dInitFailed = false;

        ID3D11VertexShader* s_vertexShader = nullptr;
        ID3D11VertexShader* s_screenTextVertexShader = nullptr;
        ID3D11PixelShader* s_pixelShader = nullptr;
        ID3D11InputLayout* s_inputLayout = nullptr;
        ID3D11Buffer* s_cameraCB = nullptr;
        ID3D11Buffer* s_modelCB = nullptr;
        ID3D11Buffer* s_lineVB = nullptr;
        ID3D11Buffer* s_textVB = nullptr;
        ID3D11RasterizerState* s_wireRasterizer = nullptr;
        ID3D11RasterizerState* s_solidRasterizer = nullptr;
        ID3D11DepthStencilState* s_depthStencil = nullptr;
        ID3D11BlendState* s_blendState = nullptr;
        SavedState s_saved{};
        CachedRenderTargetView s_submittedTextureRtv{};

        using VRSubmit_t = vr::EVRCompositorError(__thiscall*)(vr::IVRCompositor*, vr::EVREye, const vr::Texture_t*, const vr::VRTextureBounds_t*, vr::EVRSubmitFlags);
        VRSubmit_t s_originalVRSubmit = nullptr;
        void** s_vrCompositorVTable = nullptr;

        // Stereo-instancing vertex shader: FO4VR renders both eyes into one double-wide target, so
        // each primitive is drawn instanced x2 and the VS picks the eye matrix by SV_InstanceID,
        // then packs X into the correct half with a clip/cull split. ROCK DebugBodyOverlay.cpp:239-280.
        const char* K_VERTEX_SHADER_SOURCE = R"(
struct VS_INPUT {
    float3 vPos : POS;
    uint instanceId : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
    float clipDistance : SV_ClipDistance0;
    float cullDistance : SV_CullDistance0;
};

cbuffer Camera : register(b0) {
    column_major float4x4 matProjView[2];
    float4 posAdjust[2];
};

cbuffer Model : register(b1) {
    row_major float4x4 matModel;
    float4 color;
};

VS_OUTPUT main(VS_INPUT input) {
    const float4 eyeClipEdge[2] = { { -1, 0, 0, 1 }, { 1, 0, 0, 1 } };
    const float eyeOffsetScale[2] = { -0.5, 0.5 };

    float4 pos = float4(input.vPos.xyz, 1.0f);
    pos = mul(pos, matModel);
    pos.xyz -= posAdjust[input.instanceId].xyz;
    pos = mul(matProjView[input.instanceId], pos);

    VS_OUTPUT output;
    output.vColor = color;
    output.clipDistance = dot(pos, eyeClipEdge[input.instanceId]);
    output.cullDistance = output.clipDistance;
    output.vPos = pos;
    output.vPos.x *= 0.5;
    output.vPos.x += eyeOffsetScale[input.instanceId] * output.vPos.w;
    return output;
}
)";

        // Screen-space text vertex shader: quads already in clip space. ROCK DebugBodyOverlay.cpp:282-303.
        const char* K_SCREEN_TEXT_VERTEX_SHADER_SOURCE = R"(
struct VS_INPUT {
    float3 vPos : POS;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
};

cbuffer Model : register(b1) {
    row_major float4x4 matModel;
    float4 color;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.vPos = float4(input.vPos.xy, 0.0f, 1.0f);
    output.vColor = color;
    return output;
}
)";

        const char* K_PIXEL_SHADER_SOURCE = R"(
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target {
    return input.color;
}
)";

        /**
         * D3D11 device straight off the game renderer singleton — no swapchain creation needed.
         * ROCK DebugBodyOverlay.cpp:1142-1146.
         */
        ID3D11Device* getDevice()
        {
            auto* renderer = RE::BSGraphics::RendererData::GetSingleton();
            return renderer ? reinterpret_cast<ID3D11Device*>(renderer->device) : nullptr;
        }

        ID3D11DeviceContext* getContext()
        {
            auto* renderer = RE::BSGraphics::RendererData::GetSingleton();
            return renderer ? reinterpret_cast<ID3D11DeviceContext*>(renderer->context) : nullptr;
        }

        /**
         * Read the engine's own per-eye view-projection + posAdjust for the frame being submitted,
         * so anything drawn in game-world coords lands exactly where the game drew it (HMD pose is
         * already baked in — no OpenVR pose math). ROCK DebugBodyOverlay.cpp:981-1002; offsets are
         * kept in f4vr::F4VROffsets.h (vrRenderCameraGlobals).
         */
        bool getEyeViewProjMatrices(DirectX::XMMATRIX& outEye0, DirectX::XMMATRIX& outEye1, DirectX::XMFLOAT4& outAdjust0, DirectX::XMFLOAT4& outAdjust1)
        {
            const std::uintptr_t cameraGlobals = *f4vr::vrRenderCameraGlobals;
            if (!cameraGlobals) {
                return false;
            }

            const auto cameraData = *reinterpret_cast<std::uintptr_t*>(cameraGlobals + f4vr::VR_RENDER_CAMERA_DATA_OFFSET);
            if (!cameraData) {
                return false;
            }

            outEye0 = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(cameraData + f4vr::VR_RENDER_CAMERA_EYE0_VIEW_PROJ_OFFSET));
            outEye1 = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(cameraData + f4vr::VR_RENDER_CAMERA_EYE1_VIEW_PROJ_OFFSET));

            const auto* adjust0 = reinterpret_cast<const float*>(cameraGlobals + f4vr::VR_RENDER_CAMERA_EYE0_POS_ADJUST_OFFSET);
            const auto* adjust1 = reinterpret_cast<const float*>(cameraGlobals + f4vr::VR_RENDER_CAMERA_EYE1_POS_ADJUST_OFFSET);
            outAdjust0 = DirectX::XMFLOAT4(adjust0[0], adjust0[1], adjust0[2], 0.0f);
            outAdjust1 = DirectX::XMFLOAT4(adjust1[0], adjust1[1], adjust1[2], 0.0f);
            return true;
        }

        /**
         * Compile the three shaders and create all fixed pipeline objects (constant buffers, dynamic
         * vertex buffers, rasterizer/depth/blend states). ROCK DebugBodyOverlay.cpp:1004-1140.
         */
        bool initializeD3D(ID3D11Device* device)
        {
            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* psBlob = nullptr;
            ID3DBlob* errorBlob = nullptr;
            HRESULT hr = D3DCompile(K_VERTEX_SHADER_SOURCE,
                std::strlen(K_VERTEX_SHADER_SOURCE),
                "F4CFDebugDrawVS",
                nullptr,
                nullptr,
                "main",
                "vs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,
                0,
                &vsBlob,
                &errorBlob);
            if (FAILED(hr)) {
                if (errorBlob) {
                    logger::error("DebugDraw: vertex shader compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_vertexShader);
            if (FAILED(hr)) {
                vsBlob->Release();
                return false;
            }

            D3D11_INPUT_ELEMENT_DESC layoutDesc[] = { { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
            hr = device->CreateInputLayout(layoutDesc, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &s_inputLayout);
            vsBlob->Release();
            if (FAILED(hr)) {
                return false;
            }

            hr = D3DCompile(K_SCREEN_TEXT_VERTEX_SHADER_SOURCE,
                std::strlen(K_SCREEN_TEXT_VERTEX_SHADER_SOURCE),
                "F4CFDebugDrawTextVS",
                nullptr,
                nullptr,
                "main",
                "vs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,
                0,
                &vsBlob,
                &errorBlob);
            if (FAILED(hr)) {
                if (errorBlob) {
                    logger::error("DebugDraw: text vertex shader compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_screenTextVertexShader);
            vsBlob->Release();
            if (FAILED(hr)) {
                return false;
            }

            hr = D3DCompile(K_PIXEL_SHADER_SOURCE,
                std::strlen(K_PIXEL_SHADER_SOURCE),
                "F4CFDebugDrawPS",
                nullptr,
                nullptr,
                "main",
                "ps_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,
                0,
                &psBlob,
                &errorBlob);
            if (FAILED(hr)) {
                if (errorBlob) {
                    logger::error("DebugDraw: pixel shader compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &s_pixelShader);
            psBlob->Release();
            if (FAILED(hr)) {
                return false;
            }

            D3D11_BUFFER_DESC cameraDesc{};
            cameraDesc.Usage = D3D11_USAGE_DYNAMIC;
            cameraDesc.ByteWidth = sizeof(PerFrameVSData);
            cameraDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cameraDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&cameraDesc, nullptr, &s_cameraCB))) {
                return false;
            }

            D3D11_BUFFER_DESC modelDesc{};
            modelDesc.Usage = D3D11_USAGE_DYNAMIC;
            modelDesc.ByteWidth = sizeof(PerObjectVSData);
            modelDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            modelDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&modelDesc, nullptr, &s_modelCB))) {
                return false;
            }

            D3D11_BUFFER_DESC lineDesc{};
            lineDesc.Usage = D3D11_USAGE_DYNAMIC;
            lineDesc.ByteWidth = sizeof(Vertex) * internal::MAX_LINE_VERTICES;
            lineDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            lineDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&lineDesc, nullptr, &s_lineVB))) {
                return false;
            }

            D3D11_BUFFER_DESC textDesc{};
            textDesc.Usage = D3D11_USAGE_DYNAMIC;
            textDesc.ByteWidth = sizeof(Vertex) * internal::TEXT_VERTEX_CAPACITY;
            textDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            textDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&textDesc, nullptr, &s_textVB))) {
                return false;
            }

            D3D11_RASTERIZER_DESC rasterDesc{};
            rasterDesc.FillMode = D3D11_FILL_WIREFRAME;
            rasterDesc.CullMode = D3D11_CULL_NONE;
            rasterDesc.FrontCounterClockwise = TRUE;
            rasterDesc.DepthClipEnable = TRUE;
            if (FAILED(device->CreateRasterizerState(&rasterDesc, &s_wireRasterizer))) {
                return false;
            }

            rasterDesc.FillMode = D3D11_FILL_SOLID;
            if (FAILED(device->CreateRasterizerState(&rasterDesc, &s_solidRasterizer))) {
                return false;
            }

            // depth always-pass: the Submit path has no scene depth bound, the overlay draws on top
            D3D11_DEPTH_STENCIL_DESC depthDesc{};
            depthDesc.DepthEnable = FALSE;
            depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (FAILED(device->CreateDepthStencilState(&depthDesc, &s_depthStencil))) {
                return false;
            }

            D3D11_BLEND_DESC blendDesc{};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&blendDesc, &s_blendState))) {
                return false;
            }

            return true;
        }

        bool sameSubmittedTextureDesc(const D3D11_TEXTURE2D_DESC& lhs, const D3D11_TEXTURE2D_DESC& rhs)
        {
            return lhs.Width == rhs.Width && lhs.Height == rhs.Height && lhs.MipLevels == rhs.MipLevels && lhs.ArraySize == rhs.ArraySize && lhs.Format == rhs.Format &&
                   lhs.SampleDesc.Count == rhs.SampleDesc.Count && lhs.SampleDesc.Quality == rhs.SampleDesc.Quality;
        }

        /**
         * RTV over the submitted texture, cached by texture+desc so it is created once, not per
         * frame. ROCK DebugBodyOverlay.cpp:1167-1185.
         */
        ID3D11RenderTargetView* getSubmittedTextureRtv(ID3D11Device* device, ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& desc)
        {
            if (s_submittedTextureRtv.texture == texture && s_submittedTextureRtv.rtv && sameSubmittedTextureDesc(s_submittedTextureRtv.desc, desc)) {
                return s_submittedTextureRtv.rtv.Get();
            }

            s_submittedTextureRtv.rtv.Reset();
            s_submittedTextureRtv.texture = nullptr;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
            if (FAILED(device->CreateRenderTargetView(texture, nullptr, rtv.GetAddressOf())) || !rtv) {
                return nullptr;
            }

            s_submittedTextureRtv.texture = texture;
            s_submittedTextureRtv.desc = desc;
            s_submittedTextureRtv.rtv = std::move(rtv);
            return s_submittedTextureRtv.rtv.Get();
        }

        /**
         * Release every AddRef'd pointer captured by beginFrame. ROCK DebugBodyOverlay.cpp:929-979.
         */
        void releaseSavedState()
        {
            if (s_saved.vs) {
                s_saved.vs->Release();
            }
            if (s_saved.ps) {
                s_saved.ps->Release();
            }
            for (UINT i = 0; i < s_saved.vsInstanceCount; i++) {
                if (s_saved.vsInstances[i]) {
                    s_saved.vsInstances[i]->Release();
                }
            }
            for (UINT i = 0; i < s_saved.psInstanceCount; i++) {
                if (s_saved.psInstances[i]) {
                    s_saved.psInstances[i]->Release();
                }
            }
            for (auto* cb : s_saved.vsCBs) {
                if (cb) {
                    cb->Release();
                }
            }
            if (s_saved.inputLayout) {
                s_saved.inputLayout->Release();
            }
            if (s_saved.rasterizerState) {
                s_saved.rasterizerState->Release();
            }
            if (s_saved.depthStencilState) {
                s_saved.depthStencilState->Release();
            }
            if (s_saved.blendState) {
                s_saved.blendState->Release();
            }
            for (auto* rtv : s_saved.rtvs) {
                if (rtv) {
                    rtv->Release();
                }
            }
            if (s_saved.dsv) {
                s_saved.dsv->Release();
            }
            if (s_saved.vertexBuffer) {
                s_saved.vertexBuffer->Release();
            }
            if (s_saved.indexBuffer) {
                s_saved.indexBuffer->Release();
            }
            std::memset(&s_saved, 0, sizeof(s_saved));
        }

        /**
         * Snapshot the game's pipeline state and bind ours. ROCK DebugBodyOverlay.cpp:1187-1214.
         */
        void beginFrame(ID3D11DeviceContext* context)
        {
            std::memset(&s_saved, 0, sizeof(s_saved));
            s_saved.vsInstanceCount = 256;
            s_saved.psInstanceCount = 256;
            context->VSGetShader(&s_saved.vs, s_saved.vsInstances, &s_saved.vsInstanceCount);
            context->PSGetShader(&s_saved.ps, s_saved.psInstances, &s_saved.psInstanceCount);
            context->VSGetConstantBuffers(0, 2, s_saved.vsCBs);
            context->IAGetInputLayout(&s_saved.inputLayout);
            context->IAGetPrimitiveTopology(&s_saved.topology);
            context->RSGetState(&s_saved.rasterizerState);
            context->OMGetDepthStencilState(&s_saved.depthStencilState, &s_saved.stencilRef);
            context->OMGetBlendState(&s_saved.blendState, s_saved.blendFactor, &s_saved.sampleMask);
            context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s_saved.rtvs, &s_saved.dsv);
            s_saved.numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            context->RSGetViewports(&s_saved.numViewports, s_saved.viewports);
            context->IAGetVertexBuffers(0, 1, &s_saved.vertexBuffer, &s_saved.vbStride, &s_saved.vbOffset);
            context->IAGetIndexBuffer(&s_saved.indexBuffer, &s_saved.ibFormat, &s_saved.ibOffset);

            context->IASetInputLayout(s_inputLayout);
            context->VSSetShader(s_vertexShader, nullptr, 0);
            context->PSSetShader(s_pixelShader, nullptr, 0);
            context->RSSetState(s_wireRasterizer);
            FLOAT blendFactor[4] = {};
            context->OMSetBlendState(s_blendState, blendFactor, 0xFFFFFFFF);
            context->OMSetDepthStencilState(s_depthStencil, 0);
        }

        /**
         * Restore the game's pipeline state captured by beginFrame. ROCK DebugBodyOverlay.cpp:1216-1231.
         */
        void endFrame(ID3D11DeviceContext* context)
        {
            context->VSSetShader(s_saved.vs, s_saved.vsInstances, s_saved.vsInstanceCount);
            context->PSSetShader(s_saved.ps, s_saved.psInstances, s_saved.psInstanceCount);
            context->VSSetConstantBuffers(0, 2, s_saved.vsCBs);
            context->IASetInputLayout(s_saved.inputLayout);
            context->IASetPrimitiveTopology(s_saved.topology);
            context->RSSetState(s_saved.rasterizerState);
            context->OMSetDepthStencilState(s_saved.depthStencilState, s_saved.stencilRef);
            context->OMSetBlendState(s_saved.blendState, s_saved.blendFactor, s_saved.sampleMask);
            context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s_saved.rtvs, s_saved.dsv);
            context->RSSetViewports(s_saved.numViewports, s_saved.viewports);
            context->IASetVertexBuffers(0, 1, &s_saved.vertexBuffer, &s_saved.vbStride, &s_saved.vbOffset);
            context->IASetIndexBuffer(s_saved.indexBuffer, s_saved.ibFormat, s_saved.ibOffset);
            releaseSavedState();
        }

        /**
         * Upload both eyes' camera constants once per frame. ROCK DebugBodyOverlay.cpp:1233-1246.
         */
        void uploadCamera(ID3D11DeviceContext* context, const DirectX::XMMATRIX& eye0, const DirectX::XMMATRIX& eye1, const DirectX::XMFLOAT4& adjust0,
            const DirectX::XMFLOAT4& adjust1)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(s_cameraCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                auto* data = static_cast<PerFrameVSData*>(mapped.pData);
                data->matProjView[0] = eye0;
                data->matProjView[1] = eye1;
                data->posAdjust[0] = adjust0;
                data->posAdjust[1] = adjust1;
                context->Unmap(s_cameraCB, 0);
            }
            context->VSSetConstantBuffers(0, 1, &s_cameraCB);
        }

        /**
         * Upload the per-draw model matrix + color. ROCK DebugBodyOverlay.cpp:1248-1261.
         */
        void uploadColorModel(ID3D11DeviceContext* context, const DirectX::XMMATRIX& model, const Color& color)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(s_modelCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                auto* data = static_cast<PerObjectVSData*>(mapped.pData);
                data->matModel = model;
                data->color[0] = color.r;
                data->color[1] = color.g;
                data->color[2] = color.b;
                data->color[3] = color.a;
                context->Unmap(s_modelCB, 0);
            }
            context->VSSetConstantBuffers(1, 1, &s_modelCB);
        }

        /**
         * Draw all wire segments: one VB upload, then one instanced draw per same-color run (the
         * producer publishes the list color-sorted). ROCK DebugBodyOverlay.cpp:1961-2006.
         */
        void drawLines(ID3D11DeviceContext* context, const std::vector<internal::LineSegment>& lines)
        {
            if (lines.empty() || !s_lineVB) {
                return;
            }

            context->VSSetShader(s_vertexShader, nullptr, 0);
            context->PSSetShader(s_pixelShader, nullptr, 0);
            context->RSSetState(s_wireRasterizer);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(s_lineVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return;
            }
            auto* vertices = static_cast<Vertex*>(mapped.pData);
            const std::size_t lineCount = (std::min)(lines.size(), internal::MAX_LINE_VERTICES / 2);
            for (std::size_t i = 0; i < lineCount; ++i) {
                vertices[i * 2] = Vertex{ lines[i].start.x, lines[i].start.y, lines[i].start.z };
                vertices[i * 2 + 1] = Vertex{ lines[i].end.x, lines[i].end.y, lines[i].end.z };
            }
            context->Unmap(s_lineVB, 0);

            constexpr UINT stride = sizeof(Vertex);
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = s_lineVB;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

            std::size_t runStart = 0;
            while (runStart < lineCount) {
                std::size_t runEnd = runStart + 1;
                while (runEnd < lineCount && lines[runEnd].color == lines[runStart].color) {
                    ++runEnd;
                }
                uploadColorModel(context, DirectX::XMMatrixIdentity(), lines[runStart].color);
                context->DrawInstanced(static_cast<UINT>((runEnd - runStart) * 2), 2, static_cast<UINT>(runStart * 2), 0);
                runStart = runEnd;
            }
        }

        /**
         * Self-contained 5x7 bitmap font: 7 bit-rows per glyph, no texture, no asset. ROCK
         * DebugBodyOverlay.cpp:2129-2229.
         */
        std::array<std::uint8_t, 7> glyphRows(char ch)
        {
            if (ch >= 'a' && ch <= 'z') {
                ch = static_cast<char>(ch - 'a' + 'A');
            }

            switch (ch) {
            case '0':
                return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
            case '1':
                return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case '2':
                return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
            case '3':
                return { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
            case '4':
                return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
            case '5':
                return { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
            case '6':
                return { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E };
            case '7':
                return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
            case '8':
                return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
            case '9':
                return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
            case 'A':
                return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'B':
                return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
            case 'C':
                return { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F };
            case 'D':
                return { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
            case 'E':
                return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
            case 'F':
                return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
            case 'G':
                return { 0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F };
            case 'H':
                return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'I':
                return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case 'J':
                return { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
            case 'K':
                return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
            case 'L':
                return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
            case 'M':
                return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
            case 'N':
                return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
            case 'O':
                return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'P':
                return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
            case 'Q':
                return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
            case 'R':
                return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
            case 'S':
                return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
            case 'T':
                return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
            case 'U':
                return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'V':
                return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
            case 'W':
                return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
            case 'X':
                return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
            case 'Y':
                return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
            case 'Z':
                return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
            case '-':
                return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
            case '+':
                return { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
            case '=':
                return { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 };
            case '.':
                return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
            case ',':
                return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x08 };
            case ':':
                return { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
            case '/':
                return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
            case '(':
                return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
            case ')':
                return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
            case '%':
                return { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
            default:
                return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            }
        }

        /**
         * Emit one screen-pixel quad (two triangles) in clip space. ROCK DebugBodyOverlay.cpp:2231-2251.
         */
        void appendTextQuad(std::vector<Vertex>& vertices, const float x, const float y, const float size, const float textureWidth, const float textureHeight)
        {
            if (vertices.size() + 6 > internal::TEXT_VERTEX_CAPACITY) {
                return;
            }

            const auto toClip = [&](const float px, const float py) {
                return Vertex{ (px / textureWidth) * 2.0f - 1.0f, 1.0f - (py / textureHeight) * 2.0f, 0.0f };
            };

            const Vertex a = toClip(x, y);
            const Vertex b = toClip(x + size, y);
            const Vertex c = toClip(x + size, y + size);
            const Vertex d = toClip(x, y + size);
            vertices.push_back(a);
            vertices.push_back(b);
            vertices.push_back(c);
            vertices.push_back(a);
            vertices.push_back(c);
            vertices.push_back(d);
        }

        float textPixelWidth(const internal::TextEntry& entry)
        {
            return static_cast<float>(entry.text.size()) * 6.0f * (std::max)(1.0f, entry.size);
        }

        /**
         * Project a world anchor through one eye's matrix to its half of the double-wide target.
         * ROCK DebugBodyOverlay.cpp:2262-2290.
         */
        bool projectWorldAnchorToScreen(const RE::NiPoint3& anchor, const DirectX::XMMATRIX& eyeViewProj, const DirectX::XMFLOAT4& adjust, const std::uint32_t eyeIndex,
            const float textureWidth, const float textureHeight, float& outX, float& outY)
        {
            DirectX::XMFLOAT4 clip{};
            const DirectX::XMVECTOR world = DirectX::XMVectorSet(anchor.x - adjust.x, anchor.y - adjust.y, anchor.z - adjust.z, 1.0f);
            DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(world, eyeViewProj));
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) || std::fabs(clip.w) < 1.0e-5f) {
                return false;
            }

            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            if (clip.w < 0.0f || ndcX < -2.0f || ndcX > 2.0f || ndcY < -2.0f || ndcY > 2.0f) {
                return false;
            }

            const float halfWidth = textureWidth * 0.5f;
            const float eyeMinX = eyeIndex == 0 ? 0.0f : halfWidth;
            outX = eyeMinX + (ndcX * 0.5f + 0.5f) * halfWidth;
            outY = (-ndcY * 0.5f + 0.5f) * textureHeight;
            return true;
        }

        /**
         * Emit quads for each lit font pixel of the string starting at base. ROCK DebugBodyOverlay.cpp:2292-2314.
         */
        void appendTextGlyphs(std::vector<Vertex>& vertices, const internal::TextEntry& entry, const float baseX, const float baseY, const float maxX, const float textureWidth,
            const float textureHeight)
        {
            const float pixel = (std::max)(1.0f, entry.size);
            float cursorX = baseX;
            const float cursorY = baseY;
            for (const char ch : entry.text) {
                constexpr float GLYPH_ADVANCE_COLUMNS = 6.0f;
                const auto rows = glyphRows(ch);
                for (std::size_t row = 0; row < rows.size(); ++row) {
                    constexpr float GLYPH_COLUMNS = 5.0f;
                    for (std::uint8_t col = 0; col < static_cast<std::uint8_t>(GLYPH_COLUMNS); ++col) {
                        const auto bit = static_cast<std::uint8_t>(1u << (4u - col));
                        if ((rows[row] & bit) != 0) {
                            appendTextQuad(vertices, cursorX + static_cast<float>(col) * pixel, cursorY + static_cast<float>(row) * pixel, pixel, textureWidth, textureHeight);
                        }
                    }
                }
                cursorX += GLYPH_ADVANCE_COLUMNS * pixel;
                if (cursorX >= maxX || cursorX >= textureWidth - 8.0f) {
                    break;
                }
            }
        }

        /**
         * World-anchored label: project the anchor per eye and emit the glyphs into that eye's half,
         * clamped inside it. ROCK DebugBodyOverlay.cpp:2316-2348 (always stereo here).
         */
        void appendWorldAnchoredTextGlyphs(std::vector<Vertex>& vertices, const internal::TextEntry& entry, const DirectX::XMMATRIX& eye0, const DirectX::XMMATRIX& eye1,
            const DirectX::XMFLOAT4& adjust0, const DirectX::XMFLOAT4& adjust1, const float textureWidth, const float textureHeight)
        {
            const float halfWidth = textureWidth * 0.5f;
            const float approximateWidth = textPixelWidth(entry);
            const auto appendEye = [&](const std::uint32_t eyeIndex, const DirectX::XMMATRIX& eye, const DirectX::XMFLOAT4& adjust) {
                float projectedX = 0.0f;
                float projectedY = 0.0f;
                if (!projectWorldAnchorToScreen(entry.worldAnchor, eye, adjust, eyeIndex, textureWidth, textureHeight, projectedX, projectedY)) {
                    return;
                }

                const float eyeMinX = eyeIndex == 0 ? 0.0f : halfWidth;
                const float eyeMaxX = eyeMinX + halfWidth;
                const float minX = eyeMinX + 24.0f;
                const float maxX = (std::max)(minX, eyeMaxX - approximateWidth - 24.0f);
                // Left: start at the anchor (+entry.x); Center: straddle it; Right: end at it.
                float xOffset = entry.x;
                if (entry.align == internal::TextAlign::Center) {
                    xOffset = -approximateWidth * 0.5f;
                } else if (entry.align == internal::TextAlign::Right) {
                    xOffset = -approximateWidth - entry.x;
                }
                const float baseX = std::clamp(projectedX + xOffset, minX, maxX);
                const float baseY = std::clamp(projectedY + entry.y, 24.0f, (std::max)(24.0f, textureHeight - 64.0f));
                appendTextGlyphs(vertices, entry, baseX, baseY, eyeMaxX - 8.0f, textureWidth, textureHeight);
            };

            appendEye(0, eye0, adjust0);
            appendEye(1, eye1, adjust1);
        }

        RE::NiPoint3 billboardCross(const RE::NiPoint3& a, const RE::NiPoint3& b)
        {
            return RE::NiPoint3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        }

        RE::NiPoint3 billboardNorm(const RE::NiPoint3& v)
        {
            const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return length > 1.0e-6f ? RE::NiPoint3(v.x / length, v.y / length, v.z / length) : RE::NiPoint3(0.0f, 0.0f, 0.0f);
        }

        Vertex toVertex(const RE::NiPoint3& p)
        {
            return Vertex{ p.x, p.y, p.z };
        }

        // World units per font pixel, per unit of distance to the viewer — so a billboard label keeps a
        // roughly constant apparent size at any range (scaled again by the entry's text size).
        constexpr float BILLBOARD_TEXT_SCALE = 0.0008f;

        /**
         * Build a world-space, camera-facing billboard for one label: a filled quad per lit glyph pixel
         * laid out on the plane facing the viewer (world-up-based right/up basis), floated just above
         * the anchor and centred on it. Drawn through the geometry vertex shader, so it shares the exact
         * projection/depth of the shapes and stays welded to the world object.
         */
        void appendBillboardGlyphs(std::vector<Vertex>& verts, const internal::TextEntry& entry, const RE::NiPoint3& cameraPos)
        {
            const RE::NiPoint3 toCam = cameraPos - entry.worldAnchor;
            const float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
            if (dist < 1.0f) {
                return;
            }
            const RE::NiPoint3 viewDir = toCam * (1.0f / dist);
            RE::NiPoint3 right = billboardNorm(billboardCross(RE::NiPoint3(0.0f, 0.0f, 1.0f), viewDir));
            if (right.x == 0.0f && right.y == 0.0f && right.z == 0.0f) {
                right = RE::NiPoint3(1.0f, 0.0f, 0.0f); // viewer straight above/below — pick any horizontal
            }
            const RE::NiPoint3 up = billboardNorm(billboardCross(viewDir, right));

            const float pixel = dist * BILLBOARD_TEXT_SCALE * (std::max)(1.0f, entry.size);
            const float textWidth = static_cast<float>(entry.text.size()) * 6.0f * pixel;
            RE::NiPoint3 cursor = entry.worldAnchor + up * (pixel * 3.0f) - right * (textWidth * 0.5f);

            for (const char ch : entry.text) {
                if (verts.size() + 7 * 5 * 6 > internal::TEXT_VERTEX_CAPACITY) {
                    break;
                }
                const auto rows = glyphRows(ch);
                for (std::size_t row = 0; row < rows.size(); ++row) {
                    for (std::uint8_t col = 0; col < 5; ++col) {
                        const auto bit = static_cast<std::uint8_t>(1u << (4u - col));
                        if ((rows[row] & bit) == 0) {
                            continue;
                        }
                        const RE::NiPoint3 c = cursor + right * (static_cast<float>(col) * pixel) - up * (static_cast<float>(row) * pixel);
                        const RE::NiPoint3 cr = c + right * pixel;
                        const RE::NiPoint3 cd = c - up * pixel;
                        const RE::NiPoint3 crd = cr - up * pixel;
                        verts.push_back(toVertex(c));
                        verts.push_back(toVertex(cr));
                        verts.push_back(toVertex(crd));
                        verts.push_back(toVertex(c));
                        verts.push_back(toVertex(crd));
                        verts.push_back(toVertex(cd));
                    }
                }
                cursor = cursor + right * (6.0f * pixel);
            }
        }

        /**
         * Draw billboard labels as world-space geometry through the stereo shader (identical projection
         * to the shapes), one instanced draw per label. Camera CB (b0) must already be uploaded.
         */
        void drawBillboardTextEntries(ID3D11DeviceContext* context, const std::vector<internal::TextEntry>& texts, const RE::NiPoint3& cameraPos)
        {
            const bool any = std::ranges::any_of(texts, [](const internal::TextEntry& e) {
                return e.billboard;
            });
            if (!any || !s_textVB || !s_vertexShader) {
                return;
            }

            context->IASetInputLayout(s_inputLayout);
            context->VSSetShader(s_vertexShader, nullptr, 0);
            context->PSSetShader(s_pixelShader, nullptr, 0);
            context->RSSetState(s_solidRasterizer);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            constexpr UINT stride = sizeof(Vertex);
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = s_textVB;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

            for (const auto& entry : texts) {
                if (!entry.billboard) {
                    continue;
                }
                std::vector<Vertex> vertices;
                vertices.reserve(4096);
                appendBillboardGlyphs(vertices, entry, cameraPos);
                if (vertices.empty()) {
                    continue;
                }
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context->Map(s_textVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    continue;
                }
                std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
                context->Unmap(s_textVB, 0);
                uploadColorModel(context, DirectX::XMMatrixIdentity(), entry.color);
                context->DrawInstanced(static_cast<UINT>(vertices.size()), 2, 0, 0); // ×2: the shader splits the eyes
            }
        }

        /**
         * Draw all text entries as solid glyph-pixel quads; screen-space entries are duplicated into
         * both eye halves, world-anchored entries projected per eye. Billboard entries are handled
         * separately (drawBillboardTextEntries). ROCK DebugBodyOverlay.cpp:2350-2410.
         */
        void drawTextEntries(ID3D11DeviceContext* context, const float textureWidth, const float textureHeight, const std::vector<internal::TextEntry>& texts,
            const DirectX::XMMATRIX& eye0, const DirectX::XMMATRIX& eye1, const DirectX::XMFLOAT4& adjust0, const DirectX::XMFLOAT4& adjust1)
        {
            if (texts.empty() || !s_textVB || !s_screenTextVertexShader || textureWidth <= 0.0f || textureHeight <= 0.0f) {
                return;
            }

            context->IASetInputLayout(s_inputLayout);
            context->VSSetShader(s_screenTextVertexShader, nullptr, 0);
            context->PSSetShader(s_pixelShader, nullptr, 0);
            context->RSSetState(s_solidRasterizer);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            constexpr UINT stride = sizeof(Vertex);
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = s_textVB;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

            const float eyeWidth = textureWidth * 0.5f;
            for (const auto& entry : texts) {
                if (entry.billboard) {
                    continue; // world-space billboard, drawn by drawBillboardTextEntries
                }
                std::vector<Vertex> vertices;
                vertices.reserve(4096);
                if (entry.worldAnchored) {
                    appendWorldAnchoredTextGlyphs(vertices, entry, eye0, eye1, adjust0, adjust1, textureWidth, textureHeight);
                } else {
                    appendTextGlyphs(vertices, entry, entry.x, entry.y, eyeWidth - 8.0f, textureWidth, textureHeight);
                    appendTextGlyphs(vertices, entry, entry.x + eyeWidth, entry.y, textureWidth - 8.0f, textureWidth, textureHeight);
                }
                if (vertices.empty()) {
                    continue;
                }
                if (vertices.size() > internal::TEXT_VERTEX_CAPACITY) {
                    vertices.resize(internal::TEXT_VERTEX_CAPACITY);
                }

                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context->Map(s_textVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    continue;
                }
                std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
                context->Unmap(s_textVB, 0);
                uploadColorModel(context, DirectX::XMMatrixIdentity(), entry.color);
                context->Draw(static_cast<UINT>(vertices.size()), 0);
            }
        }

        /**
         * The actual draw, on the render thread, into the left-eye texture the game just handed to
         * OpenVR (the double-wide target carries both eyes via the shader's half split). ROCK
         * DebugBodyOverlay.cpp:2412-2589 minus the physics-body pass.
         */
        void drawToSubmittedTexture(const vr::Texture_t* texture)
        {
            internal::RenderFrame frame;
            {
                std::scoped_lock lock(s_frameMutex);
                frame = s_frame;
            }
            if (frame.empty()) {
                return;
            }

            auto* device = getDevice();
            auto* context = getContext();
            if (!device || !context || !texture || !texture->handle || texture->eType != vr::TextureType_DirectX) {
                return;
            }

            auto* submittedTexture = static_cast<ID3D11Texture2D*>(texture->handle);
            D3D11_TEXTURE2D_DESC textureDesc{};
            submittedTexture->GetDesc(&textureDesc);

            ID3D11RenderTargetView* rtv = getSubmittedTextureRtv(device, submittedTexture, textureDesc);
            if (!rtv) {
                return;
            }

            DirectX::XMMATRIX eye0;
            DirectX::XMMATRIX eye1;
            DirectX::XMFLOAT4 adjust0;
            DirectX::XMFLOAT4 adjust1;
            if (!getEyeViewProjMatrices(eye0, eye1, adjust0, adjust1)) {
                return;
            }

            // beginFrame snapshots the game's RTVs/viewports before we override them, so endFrame
            // restores everything including the render target
            beginFrame(context);

            context->OMSetRenderTargets(1, &rtv, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(textureDesc.Width);
            viewport.Height = static_cast<float>(textureDesc.Height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);

            uploadCamera(context, eye0, eye1, adjust0, adjust1);
            drawLines(context, frame.lines);
            drawBillboardTextEntries(context, frame.texts, frame.cameraPos);
            drawTextEntries(context, static_cast<float>(textureDesc.Width), static_cast<float>(textureDesc.Height), frame.texts, eye0, eye1, adjust0, adjust1);

            endFrame(context);
        }

        /**
         * The Submit hook: a single relaxed atomic read when there is nothing to draw; never touches
         * game-thread state (only the swapped frame under its mutex). ROCK DebugBodyOverlay.cpp:2591-2598.
         */
        vr::EVRCompositorError vrSubmitHook(vr::IVRCompositor* compositor, const vr::EVREye eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
            const vr::EVRSubmitFlags flags)
        {
            if (s_enabled.load(std::memory_order_relaxed) && eye == vr::Eye_Left) {
                drawToSubmittedTexture(texture);
            }
            return s_originalVRSubmit(compositor, eye, texture, bounds, flags);
        }

        /**
         * Patch IVRCompositor vtable index 5 (Submit) on the live compositor — the same vtable-swap
         * technique the framework uses for controller input suppression. ROCK DebugBodyOverlay.cpp:2600-2628.
         */
        bool installSubmitHook()
        {
            auto* compositor = vr::VRCompositor();
            if (!compositor) {
                if (!s_loggedNoCompositor) {
                    s_loggedNoCompositor = true;
                    logger::warn("DebugDraw: OpenVR compositor unavailable; will retry on frame update");
                }
                return false;
            }

            auto*** objectVTable = reinterpret_cast<void***>(compositor);
            s_vrCompositorVTable = *objectVTable;
            constexpr std::size_t SUBMIT_VTABLE_INDEX = 5;

            DWORD oldProtect = 0;
            if (!VirtualProtect(&s_vrCompositorVTable[SUBMIT_VTABLE_INDEX], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                logger::warn("DebugDraw: VirtualProtect failed; Submit hook not installed");
                return false;
            }

            s_originalVRSubmit = reinterpret_cast<VRSubmit_t>(s_vrCompositorVTable[SUBMIT_VTABLE_INDEX]);
            s_vrCompositorVTable[SUBMIT_VTABLE_INDEX] = reinterpret_cast<void*>(&vrSubmitHook);
            VirtualProtect(&s_vrCompositorVTable[SUBMIT_VTABLE_INDEX], sizeof(void*), oldProtect, &oldProtect);
            return true;
        }
    }

    /**
     * Lazily install everything on first use: D3D pipeline objects off the game device, then the
     * OpenVR Submit hook. Safe to call every frame — each unavailable dependency just retries.
     */
    bool ensureInstalled()
    {
        if (s_installed) {
            return true;
        }

        if (!REL::Module::IsVR()) {
            if (!s_loggedNotVR) {
                s_loggedNotVR = true;
                logger::warn("DebugDraw: only supported on Fallout 4 VR; overlay disabled");
            }
            return false;
        }

        auto* device = getDevice();
        if (!device) {
            if (!s_loggedNoDevice) {
                s_loggedNoDevice = true;
                logger::warn("DebugDraw: D3D11 device unavailable; will retry on frame update");
            }
            return false;
        }

        if (!s_d3dInitialized) {
            if (!initializeD3D(device)) {
                if (!s_loggedD3dInitFailed) {
                    s_loggedD3dInitFailed = true;
                    logger::error("DebugDraw: D3D initialization failed; overlay disabled");
                }
                return false;
            }
            s_d3dInitialized = true;
        }

        if (!installSubmitHook()) {
            return false;
        }

        s_installed = true;
        logger::info("DebugDraw: OpenVR Submit hook + D3D renderer installed");
        return true;
    }

    bool isInstalled()
    {
        return s_installed;
    }

    /**
     * Swap this frame's draws into the render-side buffer and flip the enabled atomic (the producer
     * publishes lines pre-sorted by color for run batching). ROCK DebugBodyOverlay.cpp:2674-2686.
     */
    void publish(internal::RenderFrame&& frame)
    {
        const bool hasContent = !frame.empty();
        {
            std::scoped_lock lock(s_frameMutex);
            s_frame = std::move(frame);
        }
        s_enabled.store(hasContent && s_installed, std::memory_order_release);
    }
}
