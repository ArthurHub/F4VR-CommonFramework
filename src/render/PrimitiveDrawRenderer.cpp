#include "PrimitiveDrawRenderer.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>

#include "TextFont.h"

// The wire/text drawing here is a port of ROCK's DebugBodyOverlay renderer (reference library
// github-repos/gold/ROCK/src/physics-interaction/debug/DebugBodyOverlay.cpp) with the physics-body
// extraction and hknp shape decoding stripped, per knowledge-base/debug_draw_overlay.md. Line-level
// citations below reference that file. It arrived here as the debug overlay's private renderer and
// was generalized out of it: what to draw is the producer's business, how to get lines and glyphs
// onto the submitted stereo target is this file's.
namespace f4cf::render
{
    namespace
    {
        /**
         * Vertex layout shared by every pipeline (POS float3, TEXCOORD float2).
         *
         * Everything samples a texture: text the font atlas for its glyphs, lines and fills the
         * atlas's solid block, which comes out fully opaque, and images their own texture. One shader
         * therefore draws all four, so a fill and the text over it stay in one batch instead of
         * splitting on a shader change.
         */
        struct Vertex
        {
            float x;
            float y;
            float z;
            float u;
            float v;
        };

        /**
         * How the pixel shader turns what it samples into colour, uploaded with every draw.
         */
        enum class ShadeMode : std::uint8_t
        {
            // the sample is a distance to a glyph's outline: text, and lines and fills through the
            // atlas's solid block
            DistanceField = 0,
            // the sample is the colour itself: an image
            Image = 1,
            // an image whose view decodes sRGB, encoded back so its colours survive the non-sRGB target
            ImageSRGB = 2,
        };

        /**
         * Per-draw constants (register b1): model matrix + flat color for the vertex shader, and the
         * shade mode for the pixel shader. ROCK DebugBodyOverlay.cpp:162-166, plus the mode.
         */
        struct alignas(16) PerObjectVSData
        {
            DirectX::XMMATRIX matModel;
            float color[4];
            // x: the ShadeMode; the rest pads to the 16-byte boundary constant buffers are laid out on
            float params[4];
        };

        // One set of shaders / buffers / states serves every PrimitiveDrawRenderer instance: they
        // are stateless between draws, and the Submit hook serializes the callbacks that use them.
        bool s_pipelineReady = false;
        bool s_loggedPipelineFailed = false;

        ID3D11VertexShader* s_vertexShader = nullptr;
        ID3D11VertexShader* s_screenTextVertexShader = nullptr;
        ID3D11PixelShader* s_pixelShader = nullptr;
        ID3D11InputLayout* s_inputLayout = nullptr;
        ID3D11Buffer* s_modelCB = nullptr;
        ID3D11Buffer* s_lineVB = nullptr;
        ID3D11Buffer* s_textVB = nullptr;
        ID3D11RasterizerState* s_wireRasterizer = nullptr;
        ID3D11RasterizerState* s_solidRasterizer = nullptr;
        ID3D11DepthStencilState* s_depthStencil = nullptr;

        // One state per comparison function, built on demand. The engine's comparison is read from
        // the frame rather than assumed, so we cannot know which we need until the first draw - and
        // indexing by the enum keeps that to one cheap lookup per frame.
        std::array<ID3D11DepthStencilState*, 9> s_depthTestStates{};
        ID3D11BlendState* s_blendState = nullptr;

        // the font atlas every pipeline samples, and the spot in it lines and fills read their
        // solid block from; see TextFont.h
        ID3D11ShaderResourceView* s_fontView = nullptr;
        ID3D11SamplerState* s_fontSampler = nullptr;
        float s_solidU = 0.0f;
        float s_solidV = 0.0f;

        /**
         * A vertex for untextured geometry: it samples the atlas's solid block, so it draws opaque.
         */
        Vertex solidVertex(const RE::NiPoint3& p)
        {
            return Vertex{ p.x, p.y, p.z, s_solidU, s_solidV };
        }

        // Stereo-instancing vertex shader: FO4VR renders both eyes into one double-wide target, so
        // each primitive is drawn instanced x2 and the VS picks the eye matrix by SV_InstanceID,
        // then packs X into the correct half with a clip/cull split. ROCK DebugBodyOverlay.cpp:239-280.
        const char* K_VERTEX_SHADER_SOURCE = R"(
struct VS_INPUT {
    float3 vPos : POS;
    float2 vUV : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
    float2 vUV : TEXCOORD0;
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
    float4 params;
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
    output.vUV = input.vUV;
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
    float2 vUV : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
    float2 vUV : TEXCOORD0;
};

cbuffer Model : register(b1) {
    row_major float4x4 matModel;
    float4 color;
    float4 params;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.vPos = float4(input.vPos.xy, 0.0f, 1.0f);
    output.vColor = color;
    output.vUV = input.vUV;
    return output;
}
)";

        // Pixel shader shared by every pipeline, switched per draw by the shade mode in Model.params.x.
        //
        // Distance field (0) - text, and lines and fills through the atlas's solid block: the sampled
        // distance to the glyph's outline is converted from atlas texels to screen pixels through the
        // uv derivatives, and the pixel is covered by however much of it lies inside - one pixel of
        // antialiasing at any size, distance or angle. Solid geometry samples far inside a shape, so
        // it comes out opaque. SDF_ON_EDGE and SDF_DISTANCE_SCALE are defined at compile time, from
        // TextFont.h.
        //
        // Image (1, or 2 for a view that decodes sRGB): the sample is the colour, times the tint. The
        // target is not sRGB-encoded, so a view that linearized its sample is encoded back, or the
        // image would come out darker than the file.
        //
        // Both results are computed and one selected, rather than branched between, so the
        // derivatives the distance field takes never sit inside flow control.
        const char* K_PIXEL_SHADER_SOURCE = R"(
Texture2D shaderTexture : register(t0);
SamplerState textureSampler : register(s0);

cbuffer Model : register(b1) {
    row_major float4x4 matModel;
    float4 color;
    float4 params;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float3 linearToSrgb(float3 value) {
    value = saturate(value);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(max(value, 1.0e-6), 1.0 / 2.4) - 0.055;
}

float4 main(PS_INPUT input) : SV_Target {
    float4 texel = shaderTexture.Sample(textureSampler, input.uv);

    float textureWidth;
    float textureHeight;
    shaderTexture.GetDimensions(textureWidth, textureHeight);
    float2 texelsAlongX = ddx(input.uv) * float2(textureWidth, textureHeight);
    float2 texelsAlongY = ddy(input.uv) * float2(textureWidth, textureHeight);
    float texelsPerPixel = sqrt(0.5 * (dot(texelsAlongX, texelsAlongX) + dot(texelsAlongY, texelsAlongY)));

    float texelsInside = (texel.r * 255.0 - SDF_ON_EDGE) / SDF_DISTANCE_SCALE;
    float coverage = saturate(texelsInside / max(texelsPerPixel, 1.0e-4) + 0.5);
    float4 glyph = float4(input.color.rgb, input.color.a * coverage);

    float3 imageRgb = params.x > 1.5 ? linearToSrgb(texel.rgb) : texel.rgb;
    float4 image = float4(imageRgb, texel.a) * input.color;

    return params.x > 0.5 ? image : glyph;
}
)";

        /**
         * Upload the font atlas with its whole mip chain, and the sampler that reads it.
         */
        bool createFontTexture(ID3D11Device* device)
        {
            const internal::FontAtlas& atlas = internal::fontAtlas();

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = atlas.width;
            desc.Height = atlas.height;
            desc.MipLevels = static_cast<UINT>(atlas.mips.size());
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            std::vector<D3D11_SUBRESOURCE_DATA> levels(atlas.mips.size());
            for (std::size_t level = 0; level < levels.size(); ++level) {
                levels[level].pSysMem = atlas.mips[level].data();
                levels[level].SysMemPitch = (std::max)(1u, atlas.width >> level);
            }

            ID3D11Texture2D* texture = nullptr;
            if (FAILED(device->CreateTexture2D(&desc, levels.data(), &texture))) {
                return false;
            }
            const HRESULT hr = device->CreateShaderResourceView(texture, nullptr, &s_fontView);
            texture->Release(); // the view keeps its own reference
            if (FAILED(hr)) {
                return false;
            }

            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(device->CreateSamplerState(&samplerDesc, &s_fontSampler))) {
                return false;
            }

            s_solidU = atlas.solidU;
            s_solidV = atlas.solidV;
            return true;
        }

        /**
         * Compile the three shaders and create the fixed pipeline objects shared by every instance
         * (constant buffer, dynamic vertex buffers, rasterizer/depth/blend states, font atlas). The
         * camera constants at b0 belong to the hook host. ROCK DebugBodyOverlay.cpp:1004-1140.
         */
        bool createSharedPipeline(ID3D11Device* device)
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
                    logger::error("Vertex shader compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_vertexShader);
            if (FAILED(hr)) {
                vsBlob->Release();
                return false;
            }

            const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
                { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, u)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            hr = device->CreateInputLayout(layoutDesc, static_cast<UINT>(std::size(layoutDesc)), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &s_inputLayout);
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
                    logger::error("Text vertex shader compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_screenTextVertexShader);
            vsBlob->Release();
            if (FAILED(hr)) {
                return false;
            }

            // the atlas's distance encoding, from the one place it is defined
            const std::string onEdge = std::format("{:.6f}", internal::SDF_ON_EDGE);
            const std::string distanceScale = std::format("{:.6f}", internal::SDF_DISTANCE_SCALE);
            const D3D_SHADER_MACRO pixelDefines[] = { { "SDF_ON_EDGE", onEdge.c_str() }, { "SDF_DISTANCE_SCALE", distanceScale.c_str() }, { nullptr, nullptr } };
            hr = D3DCompile(K_PIXEL_SHADER_SOURCE,
                std::strlen(K_PIXEL_SHADER_SOURCE),
                "F4CFDebugDrawPS",
                pixelDefines,
                nullptr,
                "main",
                "ps_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,
                0,
                &psBlob,
                &errorBlob);
            if (FAILED(hr)) {
                if (errorBlob) {
                    logger::error("Pixel shader compile failed: {}", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &s_pixelShader);
            psBlob->Release();
            if (FAILED(hr)) {
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
            lineDesc.ByteWidth = sizeof(Vertex) * MAX_LINE_VERTICES;
            lineDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            lineDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&lineDesc, nullptr, &s_lineVB))) {
                return false;
            }

            D3D11_BUFFER_DESC textDesc{};
            textDesc.Usage = D3D11_USAGE_DYNAMIC;
            textDesc.ByteWidth = sizeof(Vertex) * TEXT_VERTEX_CAPACITY;
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

            return createFontTexture(device);
        }

        /**
         * Upload the per-draw model matrix, color and shade mode, and bind them to both stages - the
         * pixel shader reads the mode. ROCK DebugBodyOverlay.cpp:1248-1261.
         */
        void uploadColorModel(ID3D11DeviceContext* context, const DirectX::XMMATRIX& model, const Color& color, const ShadeMode mode = ShadeMode::DistanceField)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(s_modelCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                auto* data = static_cast<PerObjectVSData*>(mapped.pData);
                data->matModel = model;
                data->color[0] = color.r;
                data->color[1] = color.g;
                data->color[2] = color.b;
                data->color[3] = color.a;
                data->params[0] = static_cast<float>(mode);
                data->params[1] = 0.0f;
                data->params[2] = 0.0f;
                data->params[3] = 0.0f;
                context->Unmap(s_modelCB, 0);
            }
            context->VSSetConstantBuffers(1, 1, &s_modelCB);
            context->PSSetConstantBuffers(1, 1, &s_modelCB);
        }

        /**
         * Draw all wire segments: one VB upload, then one instanced draw per same-color run (the
         * producer publishes the list color-sorted). ROCK DebugBodyOverlay.cpp:1961-2006.
         */
        void drawLines(ID3D11DeviceContext* context, const std::vector<LineSegment>& lines)
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
            const std::size_t lineCount = (std::min)(lines.size(), MAX_LINE_VERTICES / 2);
            for (std::size_t i = 0; i < lineCount; ++i) {
                vertices[i * 2] = solidVertex(lines[i].start);
                vertices[i * 2 + 1] = solidVertex(lines[i].end);
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

        // Screen and WorldAnchored text sizes go in steps of this many pixels of capital height, so the
        // default size 2 draws 14px capitals.
        constexpr float TEXT_PIXELS_PER_SIZE = 7.0f;

        float screenTextHeight(const TextEntry& entry)
        {
            return TEXT_PIXELS_PER_SIZE * (std::max)(1.0f, entry.size);
        }

        /**
         * Scratch space for one run's glyph quads, reused so laying text out does not allocate per
         * label; thread_local, so it stays safe whichever thread lays text out.
         */
        std::vector<internal::GlyphQuad>& glyphScratch()
        {
            thread_local std::vector<internal::GlyphQuad> quads;
            quads.clear();
            return quads;
        }

        // Where an underline sits below the baseline and how thick it is, in capital heights. Heavier and
        // lower than the font's own tables ask for, which read as a hairline hugging the letters in VR.
        constexpr float UNDERLINE_OFFSET = 0.15f;
        constexpr float UNDERLINE_THICKNESS = 0.12f;

        /**
         * Add a laid-out run's decoration to its quads, as one more quad spanning the ink that was laid
         * out. It samples the atlas's solid block, the way lines and fills do, so it draws solid and
         * goes out with the glyphs in the run's own colour and draw.
         */
        void appendDecoration(std::vector<internal::GlyphQuad>& quads, const TextDecoration decoration, const float inkWidth)
        {
            if (decoration != TextDecoration::Underline || inkWidth <= 0.0f) {
                return;
            }

            // quad y runs down from the top of the capitals, so the baseline is at 1
            quads.push_back(internal::GlyphQuad{ .x0 = 0.0f,
                .y0 = 1.0f + UNDERLINE_OFFSET,
                .x1 = inkWidth,
                .y1 = 1.0f + UNDERLINE_OFFSET + UNDERLINE_THICKNESS,
                .u0 = s_solidU,
                .v0 = s_solidV,
                .u1 = s_solidU,
                .v1 = s_solidV });
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
         * Emit one run of screen text, in target pixels, with the top of its capitals at baseY and
         * its first letter's ink starting at baseX, cut after the last letter that ends before maxX.
         */
        void appendTextGlyphs(std::vector<Vertex>& vertices, const TextEntry& entry, const float baseX, const float baseY, const float maxX, const float textureWidth,
            const float textureHeight)
        {
            const float textHeight = screenTextHeight(entry);
            const float limitX = (std::min)(maxX, textureWidth - 8.0f);
            auto& quads = glyphScratch();
            const float inkWidth = internal::layoutText(entry.text, (limitX - baseX) / textHeight, quads);
            appendDecoration(quads, entry.decoration, inkWidth);

            const auto toClip = [&](const float x, const float y, const float u, const float v) {
                const float px = baseX + x * textHeight;
                const float py = baseY + y * textHeight;
                return Vertex{ (px / textureWidth) * 2.0f - 1.0f, 1.0f - (py / textureHeight) * 2.0f, 0.0f, u, v };
            };
            for (const auto& quad : quads) {
                if (vertices.size() + 6 > TEXT_VERTEX_CAPACITY) {
                    break;
                }
                const Vertex topLeft = toClip(quad.x0, quad.y0, quad.u0, quad.v0);
                const Vertex topRight = toClip(quad.x1, quad.y0, quad.u1, quad.v0);
                const Vertex bottomRight = toClip(quad.x1, quad.y1, quad.u1, quad.v1);
                const Vertex bottomLeft = toClip(quad.x0, quad.y1, quad.u0, quad.v1);
                vertices.push_back(topLeft);
                vertices.push_back(topRight);
                vertices.push_back(bottomRight);
                vertices.push_back(topLeft);
                vertices.push_back(bottomRight);
                vertices.push_back(bottomLeft);
            }
        }

        /**
         * World-anchored label: project the anchor per eye and emit the glyphs into that eye's half,
         * clamped inside it. ROCK DebugBodyOverlay.cpp:2316-2348 (always stereo here).
         */
        void appendWorldAnchoredTextGlyphs(std::vector<Vertex>& vertices, const TextEntry& entry, const DirectX::XMMATRIX& eye0, const DirectX::XMMATRIX& eye1,
            const DirectX::XMFLOAT4& adjust0, const DirectX::XMFLOAT4& adjust1, const float textureWidth, const float textureHeight)
        {
            const float halfWidth = textureWidth * 0.5f;
            const float textWidth = measureText(entry.text, screenTextHeight(entry));
            const auto appendEye = [&](const std::uint32_t eyeIndex, const DirectX::XMMATRIX& eye, const DirectX::XMFLOAT4& adjust) {
                float projectedX = 0.0f;
                float projectedY = 0.0f;
                if (!projectWorldAnchorToScreen(entry.worldAnchor, eye, adjust, eyeIndex, textureWidth, textureHeight, projectedX, projectedY)) {
                    return;
                }

                const float eyeMinX = eyeIndex == 0 ? 0.0f : halfWidth;
                const float eyeMaxX = eyeMinX + halfWidth;
                const float minX = eyeMinX + 24.0f;
                const float maxX = (std::max)(minX, eyeMaxX - textWidth - 24.0f);
                // Left: start at the anchor (+entry.x); Center: straddle it; Right: end at it.
                float xOffset = entry.x;
                if (entry.align == TextAlign::Center) {
                    xOffset = -textWidth * 0.5f;
                } else if (entry.align == TextAlign::Right) {
                    xOffset = -textWidth - entry.x;
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

        // billboardNorm collapses a degenerate vector to zero, which is how a bad basis is detected.
        bool isZeroVector(const RE::NiPoint3& v)
        {
            return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
        }

        /**
         * True for placements built as world-space geometry rather than projected into a screen half.
         */
        constexpr bool isWorldTextPlacement(const TextPlacement placement)
        {
            return placement == TextPlacement::Billboard || placement == TextPlacement::Oriented;
        }

        // World height of a capital letter at size 1, per unit of distance to the viewer - so a
        // billboard label keeps a roughly constant apparent size at any range.
        constexpr float BILLBOARD_TEXT_HEIGHT_PER_DISTANCE = 0.0056f;

        /**
         * Lay one text run out as glyph quads on an arbitrary world plane: the run advances along
         * +right and its capitals hang down along -up from `cursor`, the point where the top of the
         * capitals meets the left edge of the first letter's ink.
         *
         * Camera-facing billboards and plane-welded panel text both reduce to this - all that differs
         * is who picks the basis and the text height. The vertex budget is checked per glyph, so a
         * run that would overflow stops mid-string rather than dropping the whole label.
         */
        void appendPlanarGlyphs(std::vector<Vertex>& vertices, const std::string& text, const RE::NiPoint3& cursor, const RE::NiPoint3& right, const RE::NiPoint3& up,
            const float textHeight, const TextDecoration decoration)
        {
            auto& quads = glyphScratch();
            const float inkWidth = internal::layoutText(text, (std::numeric_limits<float>::max)(), quads);
            appendDecoration(quads, decoration, inkWidth);

            const auto at = [&](const float x, const float y, const float u, const float v) {
                const RE::NiPoint3 point = cursor + right * (x * textHeight) - up * (y * textHeight);
                return Vertex{ point.x, point.y, point.z, u, v };
            };
            for (const auto& quad : quads) {
                if (vertices.size() + 6 > TEXT_VERTEX_CAPACITY) {
                    break;
                }
                const Vertex topLeft = at(quad.x0, quad.y0, quad.u0, quad.v0);
                const Vertex topRight = at(quad.x1, quad.y0, quad.u1, quad.v0);
                const Vertex bottomRight = at(quad.x1, quad.y1, quad.u1, quad.v1);
                const Vertex bottomLeft = at(quad.x0, quad.y1, quad.u0, quad.v1);
                vertices.push_back(topLeft);
                vertices.push_back(topRight);
                vertices.push_back(bottomRight);
                vertices.push_back(topLeft);
                vertices.push_back(bottomRight);
                vertices.push_back(bottomLeft);
            }
        }

        /**
         * Build a world-space, camera-facing billboard for one label, laid out on the plane facing the
         * viewer (world-up-based right/up basis) and centred on the anchor. Drawn through the geometry
         * vertex shader, so it shares the exact projection/depth of the shapes and stays welded to the
         * world object.
         */
        void appendBillboardGlyphs(std::vector<Vertex>& vertices, const TextEntry& entry, const RE::NiPoint3& cameraPos)
        {
            const RE::NiPoint3 toCam = cameraPos - entry.worldAnchor;
            const float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
            if (dist < 1.0f) {
                return;
            }
            const RE::NiPoint3 viewDir = toCam * (1.0f / dist);
            RE::NiPoint3 right = billboardNorm(billboardCross(RE::NiPoint3(0.0f, 0.0f, 1.0f), viewDir));
            if (isZeroVector(right)) {
                right = RE::NiPoint3(1.0f, 0.0f, 0.0f); // viewer straight above/below — pick any horizontal
            }
            const RE::NiPoint3 up = billboardNorm(billboardCross(viewDir, right));

            const float textHeight = dist * BILLBOARD_TEXT_HEIGHT_PER_DISTANCE * (std::max)(1.0f, entry.size);
            const float textWidth = measureText(entry.text, textHeight);
            const RE::NiPoint3 cursor = entry.worldAnchor + up * (textHeight * 0.5f) - right * (textWidth * 0.5f);
            appendPlanarGlyphs(vertices, entry.text, cursor, right, up, textHeight, entry.decoration);
        }

        /**
         * Build a plane-welded run for one Oriented label. The caller's right/up pick the plane, so
         * the text keeps a fixed world size and foreshortens as the viewer moves around it, instead
         * of turning to face them.
         *
         * entry.size is the world height of a capital letter, and x/y offset the run along the
         * plane's own axes before alignment.
         */
        void appendOrientedGlyphs(std::vector<Vertex>& vertices, const TextEntry& entry)
        {
            const RE::NiPoint3 right = billboardNorm(entry.right);
            const RE::NiPoint3 up = billboardNorm(entry.up);
            if (isZeroVector(right) || isZeroVector(up) || entry.size <= 0.0f) {
                return; // degenerate plane, or text with no extent to draw
            }

            const float textWidth = measureText(entry.text, entry.size);
            float alignShift = 0.0f;
            if (entry.align == TextAlign::Center) {
                alignShift = textWidth * -0.5f;
            } else if (entry.align == TextAlign::Right) {
                alignShift = -textWidth;
            }

            const RE::NiPoint3 cursor = entry.worldAnchor + right * (entry.x + alignShift) + up * entry.y;
            appendPlanarGlyphs(vertices, entry.text, cursor, right, up, entry.size, entry.decoration);
        }

        /**
         * Draw all solid world-space geometry - filled triangles first, then images, then
         * camera-facing billboards and plane-welded Oriented text - through the stereo shader, so it
         * shares the exact projection and depth of the shapes. Camera CB (b0) must already be
         * uploaded, and the font atlas bound.
         *
         * Everything goes into ONE vertex-buffer upload, and consecutive runs of the same texture,
         * shading and color collapse into a single draw: those are constant-buffer and binding
         * changes rather than vertex attributes, so a CHANGE of any of them is what forces a new
         * draw, not a new shape. A bordered text panel therefore costs two draws - one for the
         * border, one for the rows - not one per row, and each distinct image one more.
         *
         * Fills, images and world text share the one vertex buffer, so they also share its budget;
         * whichever would overflow it stops early rather than growing the buffer.
         */
        void drawWorldGeometry(ID3D11DeviceContext* context, const PrimitiveDraw& frame)
        {
            if (!s_textVB || !s_vertexShader) {
                return;
            }

            // one contiguous span of the buffer, drawn in one go
            struct DrawRun
            {
                std::size_t start;
                std::size_t count;
                ID3D11ShaderResourceView* texture;
                ShadeMode mode;
                Color color;
            };

            // enough for a bordered panel of text without a reallocation; it grows if a frame needs
            // more, and the budget check below is what actually bounds it
            std::vector<Vertex> vertices;
            vertices.reserve(8192);
            std::vector<DrawRun> runs;

            // runs are appended in buffer order, so extending the last one keeps it contiguous
            const auto appendRun = [&runs](const std::size_t start, const std::size_t count, ID3D11ShaderResourceView* texture, const ShadeMode mode, const Color& color) {
                if (count == 0) {
                    return; // degenerate or budget-exhausted, and merging it would corrupt the runs
                }
                if (!runs.empty() && runs.back().texture == texture && runs.back().mode == mode && runs.back().color == color) {
                    runs.back().count += count;
                } else {
                    runs.push_back(DrawRun{ .start = start, .count = count, .texture = texture, .mode = mode, .color = color });
                }
            };

            // fills first: depth testing is off, so this is what puts a border or background UNDER
            // the images and text drawn over it
            for (const auto& triangle : frame.triangles) {
                if (vertices.size() + 3 > TEXT_VERTEX_CAPACITY) {
                    break;
                }
                const std::size_t start = vertices.size();
                vertices.push_back(solidVertex(triangle.a));
                vertices.push_back(solidVertex(triangle.b));
                vertices.push_back(solidVertex(triangle.c));
                appendRun(start, 3, s_fontView, ShadeMode::DistanceField, triangle.color);
            }

            // images between the two, so a panel's image sits on its background and under its labels.
            // The frame being replayed holds a reference to every view, so the raw pointers in the runs
            // stay valid for the whole draw.
            for (const auto& image : frame.images) {
                if (vertices.size() + 6 > TEXT_VERTEX_CAPACITY) {
                    break;
                }
                if (!image.texture) {
                    continue;
                }
                const std::size_t start = vertices.size();
                const Vertex topLeft{ image.topLeft.x, image.topLeft.y, image.topLeft.z, image.u0, image.v0 };
                const Vertex topRight{ image.topRight.x, image.topRight.y, image.topRight.z, image.u1, image.v0 };
                const Vertex bottomRight{ image.bottomRight.x, image.bottomRight.y, image.bottomRight.z, image.u1, image.v1 };
                const Vertex bottomLeft{ image.bottomLeft.x, image.bottomLeft.y, image.bottomLeft.z, image.u0, image.v1 };
                vertices.push_back(topLeft);
                vertices.push_back(topRight);
                vertices.push_back(bottomRight);
                vertices.push_back(topLeft);
                vertices.push_back(bottomRight);
                vertices.push_back(bottomLeft);
                appendRun(start, 6, image.texture.Get(), image.srgb ? ShadeMode::ImageSRGB : ShadeMode::Image, image.tint);
            }

            for (const auto& entry : frame.texts) {
                if (!isWorldTextPlacement(entry.placement)) {
                    continue;
                }
                const std::size_t start = vertices.size();
                if (entry.placement == TextPlacement::Billboard) {
                    appendBillboardGlyphs(vertices, entry, frame.viewerPosition);
                } else {
                    appendOrientedGlyphs(vertices, entry);
                }
                appendRun(start, vertices.size() - start, s_fontView, ShadeMode::DistanceField, entry.color);
            }
            if (runs.empty()) {
                return;
            }

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(s_textVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return;
            }
            std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
            context->Unmap(s_textVB, 0);

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

            // the caller bound the font atlas, so a texture only needs binding when a run changes it
            ID3D11ShaderResourceView* boundTexture = s_fontView;
            for (const auto& run : runs) {
                if (run.texture != boundTexture) {
                    context->PSSetShaderResources(0, 1, &run.texture);
                    boundTexture = run.texture;
                }
                uploadColorModel(context, DirectX::XMMatrixIdentity(), run.color, run.mode);
                context->DrawInstanced(static_cast<UINT>(run.count), 2, static_cast<UINT>(run.start), 0); // ×2: the shader splits the eyes
            }
            if (boundTexture != s_fontView) {
                context->PSSetShaderResources(0, 1, &s_fontView); // screen text, drawn next, expects the atlas
            }
        }

        /**
         * Draw all text entries as glyph quads; screen-space entries are duplicated into both eye
         * halves, world-anchored entries projected per eye. Billboard and Oriented entries are
         * world-space geometry, drawn by drawWorldGeometry. ROCK DebugBodyOverlay.cpp:2350-2410.
         */
        void drawTextEntries(ID3D11DeviceContext* context, const float textureWidth, const float textureHeight, const std::vector<TextEntry>& texts, const DirectX::XMMATRIX& eye0,
            const DirectX::XMMATRIX& eye1, const DirectX::XMFLOAT4& adjust0, const DirectX::XMFLOAT4& adjust1)
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
                if (isWorldTextPlacement(entry.placement)) {
                    continue; // world-space geometry, drawn by drawWorldGeometry
                }
                std::vector<Vertex> vertices;
                vertices.reserve(4096);
                if (entry.placement == TextPlacement::WorldAnchored) {
                    appendWorldAnchoredTextGlyphs(vertices, entry, eye0, eye1, adjust0, adjust1, textureWidth, textureHeight);
                } else {
                    appendTextGlyphs(vertices, entry, entry.x, entry.y, eyeWidth - 8.0f, textureWidth, textureHeight);
                    appendTextGlyphs(vertices, entry, entry.x + eyeWidth, entry.y, textureWidth - 8.0f, textureWidth, textureHeight);
                }
                if (vertices.empty()) {
                    continue;
                }
                if (vertices.size() > TEXT_VERTEX_CAPACITY) {
                    vertices.resize(TEXT_VERTEX_CAPACITY);
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
         * The state that lets the world hide our geometry: test against the engine's depth with its
         * own comparison, and never write. The write mask matters as much as the read-only view -
         * together they make it impossible for an overlay to disturb the scene's depth.
         *
         * Falls back to the always-on-top state when there is no depth to test against, so a frame
         * where capture failed still draws.
         */
        ID3D11DepthStencilState* depthTestState(ID3D11Device* device, const D3D11_COMPARISON_FUNC comparison)
        {
            const auto index = static_cast<std::size_t>(comparison);
            if (index >= s_depthTestStates.size()) {
                return s_depthStencil;
            }
            if (!s_depthTestStates[index]) {
                D3D11_DEPTH_STENCIL_DESC desc{};
                desc.DepthEnable = TRUE;
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = comparison;
                if (FAILED(device->CreateDepthStencilState(&desc, &s_depthTestStates[index]))) {
                    return s_depthStencil;
                }
            }
            return s_depthTestStates[index];
        }

        /**
         * Replay one published frame, on the render thread, into the double-wide eye texture the game
         * just handed to OpenVR (the shader's half split puts each primitive in its own eye). The hook
         * host has already snapshotted the pipeline, bound the render target and uploaded the camera
         * constants at b0. ROCK DebugBodyOverlay.cpp:2412-2589 minus the physics-body pass.
         */
        void drawPrimitives(const SubmitFrame& submitFrame, const PrimitiveDraw& frame, const bool occluded)
        {
            auto* context = submitFrame.context;
            const auto& eye0 = submitFrame.camera->viewProj[0];
            const auto& eye1 = submitFrame.camera->viewProj[1];
            const auto& adjust0 = submitFrame.camera->posAdjust[0];
            const auto& adjust1 = submitFrame.camera->posAdjust[1];

            context->IASetInputLayout(s_inputLayout);
            context->VSSetShader(s_vertexShader, nullptr, 0);
            context->PSSetShader(s_pixelShader, nullptr, 0);
            context->RSSetState(s_wireRasterizer);
            FLOAT blendFactor[4] = {};
            context->OMSetBlendState(s_blendState, blendFactor, 0xFFFFFFFF);
            // lines, fills and text all sample the font atlas - text for its glyphs, the rest for its
            // solid block - so it is the default binding; images swap their own texture in and back out
            context->PSSetShaderResources(0, 1, &s_fontView);
            context->PSSetSamplers(0, 1, &s_fontSampler);
            const bool testDepth = occluded && submitFrame.sceneDepth != nullptr;
            if (occluded) {
                // one line the first time, and again if the answer changes: it separates "the layer
                // never asked for depth" from "it asked and there was none to bind"
                static bool everLogged = false;
                static bool lastHadDepth = false;
                if (!everLogged || lastHadDepth != testDepth) {
                    everLogged = true;
                    lastHadDepth = testDepth;
                    logger::info("occluded overlay: scene depth {}, comparison {}",
                        testDepth ? "bound, testing against the world" : "NOT AVAILABLE, drawing on top",
                        static_cast<int>(submitFrame.sceneDepthComparison));
                }
            }
            context->OMSetDepthStencilState(testDepth ? depthTestState(submitFrame.device, submitFrame.sceneDepthComparison) : s_depthStencil, 0);

            drawLines(context, frame.lines);
            drawWorldGeometry(context, frame);
            drawTextEntries(context, submitFrame.width, submitFrame.height, frame.texts, eye0, eye1, adjust0, adjust1);
        }
    }

    PrimitiveDrawRenderer::PrimitiveDrawRenderer(std::string name, const int drawOrder, const bool occluded)
        : _name(std::move(name)),
          _drawOrder(drawOrder),
          _occluded(occluded)
    {}

    /**
     * Best-effort only: a registered draw callback can never be removed, so an instance that dies
     * while the render thread is inside its callback is still a race. Hold instances for the
     * process lifetime.
     */
    PrimitiveDrawRenderer::~PrimitiveDrawRenderer()
    {
        setDrawCallbackActive(_callbackId, false);
    }

    /**
     * Lazily build the shared pipeline off the game device on first use and register this layer's
     * draw with the Submit hook host, which installs the hook itself. Safe to call every frame -
     * each unavailable dependency just retries.
     */
    bool PrimitiveDrawRenderer::ensureInstalled()
    {
        if (_callbackId == INVALID_DRAW_CALLBACK) {
            auto* device = getDevice();
            if (!device) {
                return false;
            }
            if (!s_pipelineReady) {
                if (!createSharedPipeline(device)) {
                    if (!s_loggedPipelineFailed) {
                        s_loggedPipelineFailed = true;
                        logger::error("D3D initialization failed; primitive drawing disabled");
                    }
                    return false;
                }
                s_pipelineReady = true;
            }
            _callbackId = registerDrawCallback(
                _name,
                [this](const SubmitFrame& submitFrame) {
                    drawFrame(submitFrame);
                },
                _drawOrder);
        }

        return render::ensureInstalled(); // the hook host's, not this class's
    }

    bool PrimitiveDrawRenderer::isInstalled() const
    {
        return _callbackId != INVALID_DRAW_CALLBACK && render::isInstalled();
    }

    /**
     * Swap this frame's primitives into the render-side buffer and flip this layer active or
     * dormant. ROCK DebugBodyOverlay.cpp:2674-2686.
     */
    void PrimitiveDrawRenderer::publish(PrimitiveDraw&& frame)
    {
        const bool hasContent = !frame.empty();
        {
            std::scoped_lock lock(_frameMutex);
            _frame = std::move(frame);
        }
        setDrawCallbackActive(_callbackId, hasContent);
    }

    /**
     * The registered draw callback: copy the published frame out from under the mutex (never hold a
     * game-thread-contended lock across the draw) and replay it.
     */
    void PrimitiveDrawRenderer::drawFrame(const SubmitFrame& submitFrame)
    {
        PrimitiveDraw frame;
        {
            std::scoped_lock lock(_frameMutex);
            frame = _frame;
        }
        if (frame.empty()) {
            return;
        }
        drawPrimitives(submitFrame, frame, _occluded);
    }
}
