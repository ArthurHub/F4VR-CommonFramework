#pragma once

#include <d3d11.h>

namespace f4cf::render::sceneDepth::internal
{
    /**
     * Copy the world's depth out of the engine's depth buffer into one the framework owns, stretched
     * from the viewport the world was drawn into to the whole buffer.
     *
     * It exists for upscalers that drive the engine's dynamic resolution (DLSS, FSR). They keep the
     * depth buffer at the submitted size but draw the world into a smaller viewport of it, top-left,
     * upscale only the colour, and clear the buffer before the frame is submitted - so at submit an
     * overlay has no depth left to test against, and what was there would not line up with it. The
     * copy is made while the world is still in the buffer and lies over the submitted texture pixel
     * for pixel.
     *
     * One full-screen pass writing each pixel with the engine depth under it, point sampled, so a
     * silhouette is as coarse as the world's own render resolution. It draws in the middle of the
     * engine's frame and restores every pipeline state it binds, so the engine's draws that follow
     * find the state they committed. A pass with unordered-access views bound to the output merger is
     * left alone: restoring its render targets would unbind them.
     *
     * The copy is shared by every framework mod in the process (SceneDepthShared.h): a mod whose pass
     * comes after another's in the same frame finds that copy made and draws nothing.
     *
     * RENDER thread only.
     *
     * @param engineDepth the engine's depth texture, which has to be shader-readable; the copy is the
     *        same size.
     * @param worldViewport where in that texture the world was drawn, in its pixels.
     * @return whether this frame's copy is there, drawn now or by another mod; why not is logged.
     */
    bool resampleWorldDepth(ID3D11DeviceContext* context, ID3D11Texture2D* engineDepth, const D3D11_VIEWPORT& worldViewport);

    /**
     * A read-only view of the last copy any mod made, or null when there is none of this size - a
     * depth view only binds beside a render target of its own size. Valid for the submit it is
     * acquired in.
     */
    ID3D11DepthStencilView* resampledWorldDepth(UINT width, UINT height);

    /**
     * Free this mod's view of the engine's texture, as nothing of it draws against the copy. The copy
     * itself is another mod's to use still: the outermost Submit hook frees it once no mod has written
     * it for STALE_COPY_FRAMES.
     */
    void releaseResampledWorldDepth();
}
