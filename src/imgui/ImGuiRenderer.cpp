#include "ImGuiRenderer.h"

#include <mutex>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <imgui_impl_dx11.h>

#include "../render/RenderUtils.h"

namespace f4cf::imgui::internal::renderer
{
    namespace
    {
        /**
         * One corner of a canvas quad, already in world space (the game thread resolved the
         * placement) plus its spot in the atlas. Putting world coordinates in the vertex buffer
         * rather than a per-quad model matrix means every canvas this frame draws in ONE call.
         */
        struct QuadVertex
        {
            float x;
            float y;
            float z;
            float u;
            float v;
        };

        constexpr UINT VERTICES_PER_QUAD = 6;
        constexpr UINT MAX_QUADS = 64;
        constexpr UINT MAX_QUAD_VERTICES = MAX_QUADS * VERTICES_PER_QUAD;

        // Same stereo split the rest of the framework's overlays use: FO4VR renders both eyes into
        // one double-wide target, so each quad is drawn instanced x2 and the shader picks the eye by
        // SV_InstanceID, then packs X into that eye's half with a clip/cull split.
        const char* K_QUAD_VERTEX_SHADER = R"(
struct VS_INPUT {
    float3 vPos : POS;
    float2 vUV : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float2 vUV : TEXCOORD0;
    float clipDistance : SV_ClipDistance0;
    float cullDistance : SV_CullDistance0;
};

cbuffer Camera : register(b0) {
    column_major float4x4 matProjView[2];
    float4 posAdjust[2];
};

VS_OUTPUT main(VS_INPUT input) {
    const float4 eyeClipEdge[2] = { { -1, 0, 0, 1 }, { 1, 0, 0, 1 } };
    const float eyeOffsetScale[2] = { -0.5, 0.5 };

    float4 pos = float4(input.vPos.xyz, 1.0f);
    pos.xyz -= posAdjust[input.instanceId].xyz;
    pos = mul(matProjView[input.instanceId], pos);

    VS_OUTPUT output;
    output.vUV = input.vUV;
    output.clipDistance = dot(pos, eyeClipEdge[input.instanceId]);
    output.cullDistance = output.clipDistance;
    output.vPos = pos;
    output.vPos.x *= 0.5;
    output.vPos.x += eyeOffsetScale[input.instanceId] * output.vPos.w;
    return output;
}
)";

        const char* K_QUAD_PIXEL_SHADER = R"(
Texture2D atlas : register(t0);
SamplerState atlasSampler : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target {
    return atlas.Sample(atlasSampler, input.uv);
}
)";

        // --- shared by both threads ------------------------------------------------------------
        RenderFrame s_frame;
        std::mutex s_frameMutex;

        // --- game thread only -------------------------------------------------------------------
        bool s_installed = false;
        bool s_loggedInitFailed = false;
        render::DrawCallbackId s_drawCallback = render::INVALID_DRAW_CALLBACK;

        // --- D3D objects, created once ----------------------------------------------------------
        ID3D11Texture2D* s_atlasTexture = nullptr;
        ID3D11RenderTargetView* s_atlasRtv = nullptr;
        ID3D11ShaderResourceView* s_atlasSrv = nullptr;
        ID3D11VertexShader* s_quadVertexShader = nullptr;
        ID3D11PixelShader* s_quadPixelShader = nullptr;
        ID3D11InputLayout* s_quadInputLayout = nullptr;
        ID3D11Buffer* s_quadVertexBuffer = nullptr;
        ID3D11SamplerState* s_atlasSampler = nullptr;
        ID3D11RasterizerState* s_quadRasterizer = nullptr;
        ID3D11DepthStencilState* s_quadDepthStencil = nullptr;

        // One depth-testing state per comparison function, built on demand: the engine's comparison
        // is read from the captured frame rather than assumed, so which one is needed is not known
        // until the first draw.
        std::array<ID3D11DepthStencilState*, 9> s_quadDepthTestStates{};
        ID3D11BlendState* s_quadBlend = nullptr;

        bool compileShader(const char* source, const char* name, const char* target, ID3DBlob** outBlob)
        {
            ID3DBlob* errorBlob = nullptr;
            const HRESULT hr = D3DCompile(source,
                std::strlen(source),
                name,
                nullptr,
                nullptr,
                "main",
                target,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,
                0,
                outBlob,
                &errorBlob);
            if (FAILED(hr)) {
                if (errorBlob) {
                    logger::error("{} compile failed: {}", name, static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }
            return true;
        }

        /**
         * The offscreen atlas every canvas rasterizes into, plus the pipeline that composites slices
         * of it into the world.
         */
        bool createDeviceObjects(ID3D11Device* device, const int atlasWidth, const int atlasHeight)
        {
            D3D11_TEXTURE2D_DESC atlasDesc{};
            atlasDesc.Width = static_cast<UINT>(atlasWidth);
            atlasDesc.Height = static_cast<UINT>(atlasHeight);
            atlasDesc.MipLevels = 1;
            atlasDesc.ArraySize = 1;
            atlasDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            atlasDesc.SampleDesc.Count = 1;
            atlasDesc.Usage = D3D11_USAGE_DEFAULT;
            atlasDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&atlasDesc, nullptr, &s_atlasTexture))) {
                return false;
            }
            if (FAILED(device->CreateRenderTargetView(s_atlasTexture, nullptr, &s_atlasRtv))) {
                return false;
            }
            if (FAILED(device->CreateShaderResourceView(s_atlasTexture, nullptr, &s_atlasSrv))) {
                return false;
            }

            ID3DBlob* vsBlob = nullptr;
            if (!compileShader(K_QUAD_VERTEX_SHADER, "F4CFImGuiQuadVS", "vs_5_0", &vsBlob)) {
                return false;
            }
            HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_quadVertexShader);
            if (FAILED(hr)) {
                vsBlob->Release();
                return false;
            }
            const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
                { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            hr = device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &s_quadInputLayout);
            vsBlob->Release();
            if (FAILED(hr)) {
                return false;
            }

            ID3DBlob* psBlob = nullptr;
            if (!compileShader(K_QUAD_PIXEL_SHADER, "F4CFImGuiQuadPS", "ps_5_0", &psBlob)) {
                return false;
            }
            hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &s_quadPixelShader);
            psBlob->Release();
            if (FAILED(hr)) {
                return false;
            }

            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
            vertexDesc.ByteWidth = sizeof(QuadVertex) * MAX_QUAD_VERTICES;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&vertexDesc, nullptr, &s_quadVertexBuffer))) {
                return false;
            }

            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
            if (FAILED(device->CreateSamplerState(&samplerDesc, &s_atlasSampler))) {
                return false;
            }

            D3D11_RASTERIZER_DESC rasterDesc{};
            rasterDesc.FillMode = D3D11_FILL_SOLID;
            // no back-face culling: a canvas seen from behind should still render rather than vanish
            rasterDesc.CullMode = D3D11_CULL_NONE;
            rasterDesc.FrontCounterClockwise = TRUE;
            rasterDesc.DepthClipEnable = TRUE;
            if (FAILED(device->CreateRasterizerState(&rasterDesc, &s_quadRasterizer))) {
                return false;
            }

            // the Submit path has no scene depth bound; overlays draw on top
            D3D11_DEPTH_STENCIL_DESC depthDesc{};
            depthDesc.DepthEnable = FALSE;
            depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (FAILED(device->CreateDepthStencilState(&depthDesc, &s_quadDepthStencil))) {
                return false;
            }

            // PREMULTIPLIED alpha (ONE / INV_SRC_ALPHA), not the usual SRC_ALPHA / INV_SRC_ALPHA:
            // ImGui's DX11 backend blends alpha as ONE / INV_SRC_ALPHA, so rasterizing onto a
            // cleared transparent atlas leaves colour already multiplied by coverage. Compositing
            // that with straight alpha would apply coverage twice and halo every glyph edge.
            D3D11_BLEND_DESC blendDesc{};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&blendDesc, &s_quadBlend))) {
                return false;
            }

            return true;
        }

        void appendQuad(std::vector<QuadVertex>& vertices, const CanvasQuad& quad)
        {
            const auto vertex = [](const RE::NiPoint3& p, const float u, const float v) {
                return QuadVertex{ .x = p.x, .y = p.y, .z = p.z, .u = u, .v = v };
            };
            const QuadVertex topLeft = vertex(quad.topLeft, quad.u0, quad.v0);
            const QuadVertex topRight = vertex(quad.topRight, quad.u1, quad.v0);
            const QuadVertex bottomRight = vertex(quad.bottomRight, quad.u1, quad.v1);
            const QuadVertex bottomLeft = vertex(quad.bottomLeft, quad.u0, quad.v1);
            vertices.push_back(topLeft);
            vertices.push_back(topRight);
            vertices.push_back(bottomRight);
            vertices.push_back(topLeft);
            vertices.push_back(bottomRight);
            vertices.push_back(bottomLeft);
        }

        /**
         * The state that lets the world hide a canvas: test against the engine's own depth with its
         * own comparison, and never write. The write mask matters as much as the read-only view the
         * host binds - together they make it impossible for a canvas to disturb the scene's depth.
         *
         * Falls back to the always-on-top state if the state cannot be built, so a canvas still draws.
         */
        ID3D11DepthStencilState* quadDepthTestState(ID3D11Device* device, const D3D11_COMPARISON_FUNC comparison)
        {
            const auto index = static_cast<std::size_t>(comparison);
            if (index >= s_quadDepthTestStates.size()) {
                return s_quadDepthStencil;
            }
            if (!s_quadDepthTestStates[index]) {
                D3D11_DEPTH_STENCIL_DESC desc{};
                desc.DepthEnable = TRUE;
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = comparison;
                if (FAILED(device->CreateDepthStencilState(&desc, &s_quadDepthTestStates[index]))) {
                    return s_quadDepthStencil;
                }
            }
            return s_quadDepthTestStates[index];
        }

        /**
         * Composite the canvases: every quad samples the same atlas with the same shaders, so the only
         * thing that splits a draw is whether the world may hide it. The quads arrive sorted with the
         * occluded ones first, so that is at most two draws (x2 each for the eye split) - and exactly
         * one whenever every canvas agrees, which is the usual case.
         */
        void drawQuads(const render::SubmitFrame& submitFrame, const std::vector<CanvasQuad>& quads)
        {
            std::vector<QuadVertex> vertices;
            vertices.reserve(quads.size() * VERTICES_PER_QUAD);
            std::size_t occludedVertices = 0;
            for (const auto& quad : quads) {
                if (vertices.size() + VERTICES_PER_QUAD > MAX_QUAD_VERTICES) {
                    break;
                }
                appendQuad(vertices, quad);
                if (quad.occluded) {
                    occludedVertices = vertices.size(); // the sort keeps these contiguous, at the front
                }
            }
            if (vertices.empty()) {
                return;
            }

            auto* context = submitFrame.context;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(s_quadVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return;
            }
            std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(QuadVertex));
            context->Unmap(s_quadVertexBuffer, 0);

            context->IASetInputLayout(s_quadInputLayout);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            constexpr UINT stride = sizeof(QuadVertex);
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = s_quadVertexBuffer;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            context->VSSetShader(s_quadVertexShader, nullptr, 0);
            context->PSSetShader(s_quadPixelShader, nullptr, 0);
            context->PSSetShaderResources(0, 1, &s_atlasSrv);
            context->PSSetSamplers(0, 1, &s_atlasSampler);
            context->RSSetState(s_quadRasterizer);
            FLOAT blendFactor[4] = {};
            context->OMSetBlendState(s_quadBlend, blendFactor, 0xFFFFFFFF);

            // with no scene depth captured there is nothing to test against, so everything draws on
            // top exactly as it did before occlusion existed
            const std::size_t occludedCount = submitFrame.sceneDepth ? occludedVertices : 0;
            if (occludedCount > 0) {
                context->OMSetDepthStencilState(quadDepthTestState(submitFrame.device, submitFrame.sceneDepthComparison), 0);
                context->DrawInstanced(static_cast<UINT>(occludedCount), 2, 0, 0); // x2: the shader splits the eyes
            }
            if (occludedCount < vertices.size()) {
                context->OMSetDepthStencilState(s_quadDepthStencil, 0);
                context->DrawInstanced(static_cast<UINT>(vertices.size() - occludedCount), 2, static_cast<UINT>(occludedCount), 0);
            }
        }

        /**
         * The registered draw callback. Two passes: rasterize the ImGui frame flat into the atlas,
         * then place slices of the atlas in the world.
         */
        void drawFrame(const render::SubmitFrame& submitFrame)
        {
            // take a reference and draw outside the lock, so publishing never waits on a draw.
            // ImGui_ImplDX11_RenderDrawData reads its backend state off the ImGui context, which the
            // game thread sets once at init and never changes - a stable read from here.
            std::shared_ptr<ClonedDrawData> drawData;
            std::vector<CanvasQuad> quads;
            {
                std::scoped_lock lock(s_frameMutex);
                drawData = s_frame.drawData;
                quads = s_frame.quads;
            }
            if (!drawData || quads.empty()) {
                return;
            }

            auto* context = submitFrame.context;

            // pass 1: ImGui into the atlas. ImGui_ImplDX11_RenderDrawData sets its own viewport
            // and projection from the draw data's display size, and backs up / restores the
            // pipeline around itself - including vertex-shader constant buffer b0, which is where
            // the hook host put the camera matrices pass 2 needs. It does NOT touch render targets,
            // hence the rebind below.
            constexpr FLOAT transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            context->OMSetRenderTargets(1, &s_atlasRtv, nullptr);
            context->ClearRenderTargetView(s_atlasRtv, transparent);
            ImGui_ImplDX11_RenderDrawData(&drawData->drawData);

            // pass 2: back onto the eye texture the hook host bound for us. Rebinding it is this
            // callback's responsibility - the host binds it once for the whole callback list, so a
            // callback that redirects the render target and does not put it back would silently
            // steal every overlay drawn after it.
            // Restores the host's DEPTH view as well as its render target. Binding null here would
            // not just skip occlusion for this pass - it would strip the depth view out from under
            // every callback drawn after this one, which is the same silent theft the comment above
            // warns about, one binding further along.
            context->OMSetRenderTargets(1, &submitFrame.renderTarget, submitFrame.sceneDepth);
            D3D11_VIEWPORT viewport{};
            viewport.Width = submitFrame.width;
            viewport.Height = submitFrame.height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);

            drawQuads(submitFrame, quads);
        }
    }

    bool ensureInstalled(const int atlasWidth, const int atlasHeight)
    {
        if (!s_installed) {
            auto* device = render::getDevice();
            if (!device) {
                return false;
            }
            if (!createDeviceObjects(device, atlasWidth, atlasHeight)) {
                if (!s_loggedInitFailed) {
                    s_loggedInitFailed = true;
                    logger::error("D3D initialization failed; ImGui canvases disabled");
                }
                return false;
            }
            s_installed = true;
            s_drawCallback = render::registerDrawCallback("ImGuiCanvases", &drawFrame, render::DRAW_ORDER_PANELS);
        }

        return render::ensureInstalled();
    }

    void publish(RenderFrame&& frame)
    {
        const bool hasContent = !frame.empty();
        {
            std::scoped_lock lock(s_frameMutex);
            s_frame = std::move(frame);
        }
        render::setDrawCallbackActive(s_drawCallback, hasContent);
    }
}
