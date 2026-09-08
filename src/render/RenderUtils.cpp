#include "RenderUtils.h"

#include "../f4vr/F4VROffsets.h"

#include "RE/Bethesda/BSGraphics.h"

namespace f4cf::render
{
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
     * Read the engine's own per-eye view-projection + posAdjust for the frame being submitted.
     * Offsets live in f4vr::F4VROffsets.h (vrRenderCameraGlobals); if overlay geometry ever reads as
     * garbage after a runtime change, that block is the first suspect.
     */
    bool getStereoCameraConstants(StereoCameraConstants& out)
    {
        const std::uintptr_t cameraGlobals = *f4vr::vrRenderCameraGlobals;
        if (!cameraGlobals) {
            return false;
        }

        const auto cameraData = *reinterpret_cast<std::uintptr_t*>(cameraGlobals + f4vr::VR_RENDER_CAMERA_DATA_OFFSET);
        if (!cameraData) {
            return false;
        }

        out.viewProj[0] = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(cameraData + f4vr::VR_RENDER_CAMERA_EYE0_VIEW_PROJ_OFFSET));
        out.viewProj[1] = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(cameraData + f4vr::VR_RENDER_CAMERA_EYE1_VIEW_PROJ_OFFSET));

        const auto* adjust0 = reinterpret_cast<const float*>(cameraGlobals + f4vr::VR_RENDER_CAMERA_EYE0_POS_ADJUST_OFFSET);
        const auto* adjust1 = reinterpret_cast<const float*>(cameraGlobals + f4vr::VR_RENDER_CAMERA_EYE1_POS_ADJUST_OFFSET);
        out.posAdjust[0] = DirectX::XMFLOAT4(adjust0[0], adjust0[1], adjust0[2], 0.0f);
        out.posAdjust[1] = DirectX::XMFLOAT4(adjust1[0], adjust1[1], adjust1[2], 0.0f);
        return true;
    }

    /**
     * Snapshot every pipeline field an overlay callback may touch, then unbind the stages none of
     * them use so a leftover geometry/tessellation/compute shader cannot mangle the draw.
     */
    ScopedPipelineState::ScopedPipelineState(ID3D11DeviceContext* context)
        : _context(context)
    {
        _saved.vsInstanceCount = CLASS_INSTANCE_SLOTS;
        _saved.psInstanceCount = CLASS_INSTANCE_SLOTS;
        _context->VSGetShader(&_saved.vs, _saved.vsInstances, &_saved.vsInstanceCount);
        _context->PSGetShader(&_saved.ps, _saved.psInstances, &_saved.psInstanceCount);
        _context->GSGetShader(&_saved.gs, nullptr, nullptr);
        _context->HSGetShader(&_saved.hs, nullptr, nullptr);
        _context->DSGetShader(&_saved.ds, nullptr, nullptr);
        _context->CSGetShader(&_saved.cs, nullptr, nullptr);
        _context->VSGetConstantBuffers(0, SHADER_SLOTS, _saved.vsConstantBuffers);
        _context->PSGetConstantBuffers(0, SHADER_SLOTS, _saved.psConstantBuffers);
        _context->VSGetShaderResources(0, SHADER_SLOTS, _saved.vsResources);
        _context->PSGetShaderResources(0, SHADER_SLOTS, _saved.psResources);
        _context->VSGetSamplers(0, SHADER_SLOTS, _saved.vsSamplers);
        _context->PSGetSamplers(0, SHADER_SLOTS, _saved.psSamplers);
        _context->IAGetInputLayout(&_saved.inputLayout);
        _context->IAGetPrimitiveTopology(&_saved.topology);
        _context->RSGetState(&_saved.rasterizerState);
        _context->OMGetDepthStencilState(&_saved.depthStencilState, &_saved.stencilRef);
        _context->OMGetBlendState(&_saved.blendState, _saved.blendFactor, &_saved.sampleMask);
        _context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _saved.renderTargets, &_saved.depthStencilView);
        _saved.viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        _context->RSGetViewports(&_saved.viewportCount, _saved.viewports);
        _saved.scissorRectCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        _context->RSGetScissorRects(&_saved.scissorRectCount, _saved.scissorRects);
        _context->IAGetVertexBuffers(0, VERTEX_BUFFER_SLOTS, _saved.vertexBuffers, _saved.vertexStrides, _saved.vertexOffsets);
        _context->IAGetIndexBuffer(&_saved.indexBuffer, &_saved.indexFormat, &_saved.indexOffset);

        _context->GSSetShader(nullptr, nullptr, 0);
        _context->HSSetShader(nullptr, nullptr, 0);
        _context->DSSetShader(nullptr, nullptr, 0);
        _context->CSSetShader(nullptr, nullptr, 0);
    }

    /**
     * Put every captured field back and release the references the Get* calls added.
     */
    ScopedPipelineState::~ScopedPipelineState()
    {
        _context->VSSetShader(_saved.vs, _saved.vsInstances, _saved.vsInstanceCount);
        _context->PSSetShader(_saved.ps, _saved.psInstances, _saved.psInstanceCount);
        _context->GSSetShader(_saved.gs, nullptr, 0);
        _context->HSSetShader(_saved.hs, nullptr, 0);
        _context->DSSetShader(_saved.ds, nullptr, 0);
        _context->CSSetShader(_saved.cs, nullptr, 0);
        _context->VSSetConstantBuffers(0, SHADER_SLOTS, _saved.vsConstantBuffers);
        _context->PSSetConstantBuffers(0, SHADER_SLOTS, _saved.psConstantBuffers);
        _context->VSSetShaderResources(0, SHADER_SLOTS, _saved.vsResources);
        _context->PSSetShaderResources(0, SHADER_SLOTS, _saved.psResources);
        _context->VSSetSamplers(0, SHADER_SLOTS, _saved.vsSamplers);
        _context->PSSetSamplers(0, SHADER_SLOTS, _saved.psSamplers);
        _context->IASetInputLayout(_saved.inputLayout);
        _context->IASetPrimitiveTopology(_saved.topology);
        _context->RSSetState(_saved.rasterizerState);
        _context->OMSetDepthStencilState(_saved.depthStencilState, _saved.stencilRef);
        _context->OMSetBlendState(_saved.blendState, _saved.blendFactor, _saved.sampleMask);
        _context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _saved.renderTargets, _saved.depthStencilView);
        _context->RSSetViewports(_saved.viewportCount, _saved.viewports);
        _context->RSSetScissorRects(_saved.scissorRectCount, _saved.scissorRects);
        _context->IASetVertexBuffers(0, VERTEX_BUFFER_SLOTS, _saved.vertexBuffers, _saved.vertexStrides, _saved.vertexOffsets);
        _context->IASetIndexBuffer(_saved.indexBuffer, _saved.indexFormat, _saved.indexOffset);

        const auto release = [](IUnknown* obj) {
            if (obj) {
                obj->Release();
            }
        };
        release(_saved.vs);
        release(_saved.ps);
        release(_saved.gs);
        release(_saved.hs);
        release(_saved.ds);
        release(_saved.cs);
        for (UINT i = 0; i < _saved.vsInstanceCount; i++) {
            release(_saved.vsInstances[i]);
        }
        for (UINT i = 0; i < _saved.psInstanceCount; i++) {
            release(_saved.psInstances[i]);
        }
        for (UINT i = 0; i < SHADER_SLOTS; i++) {
            release(_saved.vsConstantBuffers[i]);
            release(_saved.psConstantBuffers[i]);
            release(_saved.vsResources[i]);
            release(_saved.psResources[i]);
            release(_saved.vsSamplers[i]);
            release(_saved.psSamplers[i]);
        }
        release(_saved.inputLayout);
        release(_saved.rasterizerState);
        release(_saved.depthStencilState);
        release(_saved.blendState);
        for (auto* renderTarget : _saved.renderTargets) {
            release(renderTarget);
        }
        release(_saved.depthStencilView);
        for (UINT i = 0; i < VERTEX_BUFFER_SLOTS; i++) {
            release(_saved.vertexBuffers[i]);
        }
        release(_saved.indexBuffer);
    }
}
