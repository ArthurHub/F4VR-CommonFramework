#pragma once

#include <DirectXMath.h>
#include <d3d11.h>

namespace f4cf::render
{
    /**
     * Per-frame stereo camera constants, uploaded to vertex-shader register b0 by the Submit hook
     * host before it invokes any draw callback: the engine's own per-eye view-projection matrices
     * and the world -> VR-origin offsets it used to render the frame being submitted. Drawing
     * world-space geometry through these lands it exactly where the game drew the world, with the
     * HMD pose already baked in - no OpenVR pose math on the render path (querying OpenVR from a
     * Submit hook double-drives vrclient and is what forces other overlays to stand down).
     */
    struct alignas(16) StereoCameraConstants
    {
        DirectX::XMMATRIX viewProj[2];
        DirectX::XMFLOAT4 posAdjust[2];
    };

    /**
     * The game's own D3D11 device / immediate context, straight off the renderer singleton - no
     * swapchain creation needed. Null before the renderer is up.
     */
    ID3D11Device* getDevice();
    ID3D11DeviceContext* getContext();

    /**
     * Read the engine's per-eye view-projection + posAdjust for the frame being submitted. False
     * when the camera globals are not populated yet.
     */
    bool getStereoCameraConstants(StereoCameraConstants& out);

    /**
     * RAII snapshot of the D3D11 pipeline state, taken before any overlay drawing and restored on
     * scope exit. We draw in the middle of the game's own pipeline and, once other mods chain onto
     * the same Submit hook, in the middle of theirs: a field we clobber and fail to restore corrupts
     * the game frame or the next overlay down the chain - and it presents as *their* bug. So the
     * surface is deliberately wider than what any one callback binds, and it covers the
     * easy-to-forget shader resources, samplers and constant buffers.
     *
     * Also unbinds the geometry/hull/domain/compute stages for the duration (and restores them),
     * so an overlay draw never runs through whatever the game had bound there.
     */
    class ScopedPipelineState
    {
    public:
        explicit ScopedPipelineState(ID3D11DeviceContext* context);
        ~ScopedPipelineState();

        ScopedPipelineState(const ScopedPipelineState&) = delete;
        ScopedPipelineState& operator=(const ScopedPipelineState&) = delete;
        ScopedPipelineState(ScopedPipelineState&&) = delete;
        ScopedPipelineState& operator=(ScopedPipelineState&&) = delete;

    private:
        // Per-stage slot count captured for constant buffers / shader resources / samplers. The
        // overlay pipelines only ever bind the first couple of slots; the margin is cheap insurance.
        static constexpr UINT SHADER_SLOTS = 4;
        static constexpr UINT CLASS_INSTANCE_SLOTS = 256;
        static constexpr UINT VERTEX_BUFFER_SLOTS = 2;

        struct SavedState
        {
            ID3D11VertexShader* vs = nullptr;
            ID3D11PixelShader* ps = nullptr;
            ID3D11GeometryShader* gs = nullptr;
            ID3D11HullShader* hs = nullptr;
            ID3D11DomainShader* ds = nullptr;
            ID3D11ComputeShader* cs = nullptr;
            ID3D11ClassInstance* vsInstances[CLASS_INSTANCE_SLOTS] = {};
            ID3D11ClassInstance* psInstances[CLASS_INSTANCE_SLOTS] = {};
            UINT vsInstanceCount = 0;
            UINT psInstanceCount = 0;
            ID3D11Buffer* vsConstantBuffers[SHADER_SLOTS] = {};
            ID3D11Buffer* psConstantBuffers[SHADER_SLOTS] = {};
            ID3D11ShaderResourceView* vsResources[SHADER_SLOTS] = {};
            ID3D11ShaderResourceView* psResources[SHADER_SLOTS] = {};
            ID3D11SamplerState* vsSamplers[SHADER_SLOTS] = {};
            ID3D11SamplerState* psSamplers[SHADER_SLOTS] = {};
            ID3D11InputLayout* inputLayout = nullptr;
            D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            ID3D11RasterizerState* rasterizerState = nullptr;
            ID3D11DepthStencilState* depthStencilState = nullptr;
            UINT stencilRef = 0;
            ID3D11BlendState* blendState = nullptr;
            FLOAT blendFactor[4] = {};
            UINT sampleMask = 0;
            ID3D11RenderTargetView* renderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            ID3D11DepthStencilView* depthStencilView = nullptr;
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            UINT viewportCount = 0;
            D3D11_RECT scissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            UINT scissorRectCount = 0;
            ID3D11Buffer* vertexBuffers[VERTEX_BUFFER_SLOTS] = {};
            UINT vertexStrides[VERTEX_BUFFER_SLOTS] = {};
            UINT vertexOffsets[VERTEX_BUFFER_SLOTS] = {};
            ID3D11Buffer* indexBuffer = nullptr;
            DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
            UINT indexOffset = 0;
        };

        ID3D11DeviceContext* _context;
        SavedState _saved;
    };
}
