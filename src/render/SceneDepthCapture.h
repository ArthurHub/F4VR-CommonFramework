#pragma once

#include <d3d11.h>

namespace f4cf::render
{
    /**
     * The engine's scene depth for the eye texture currently being submitted, as a READ-ONLY view.
     *
     * Read-only is the whole safety story. The buffer belongs to the engine and is still referenced
     * by it; a read-only depth view lets the overlay test against world depth while making it
     * impossible to write into what the engine owns.
     *
     * The view is owned by the capture and stays valid only for the submit it was acquired in.
     */
    struct SceneDepth
    {
        ID3D11DepthStencilView* readOnlyView = nullptr;

        // The comparison the engine itself was using when it drew the world, read from the bound
        // depth-stencil state rather than assumed - FO4 need not use a conventional LESS.
        D3D11_COMPARISON_FUNC comparison = D3D11_COMPARISON_LESS_EQUAL;

        bool isValid() const
        {
            return readOnlyView != nullptr;
        }
    };

    /**
     * Gets hold of the engine's scene depth buffer, so overlay draws can be occluded by the world.
     *
     * The problem it solves: by the time the frame reaches IVRCompositor::Submit - where the whole
     * overlay draws - the engine has unbound its depth buffer, so there is nothing to test against
     * and every overlay is unconditionally on top. Searching for the buffer at submit time finds
     * nothing, and reaching into BSGraphics::RendererData for it means trusting struct offsets that
     * nobody has verified against a running VR build.
     *
     * So instead we take it from the one place it is legitimately bound: a call site inside the
     * engine's graphics-state commit, hooked so the detour runs after the state is committed and
     * simply reads what is bound. No struct offsets, no guessing.
     *
     * A capture is only kept when the depth buffer is the same size as the texture the overlay will
     * draw into. That is not merely a heuristic: D3D requires a depth view to match the dimensions of
     * the render target it is bound beside, so anything else is unusable by definition. The engine
     * renders the world into its own G-buffer at exactly the submitted resolution and only resolves
     * into the submitted texture, so pairing by identity - the obvious approach - never matches.
     *
     * Provenance: the technique and both addresses were studied from PrismaUI's FO4VR port
     * (F4VR reference library, framework-F4-Conversion). That project's license permits study but
     * not redistribution of derived code, so this is an independent implementation; the addresses
     * and byte patterns are facts about Fallout4VR.exe 1.2.72 and were re-verified against it.
     */
    namespace sceneDepth
    {
        /**
         * Install the capture hook if it is not installed yet (idempotent). False when the game is
         * not VR, the addresses do not carry the instructions we expect, or the trampoline write
         * fails - all of which are non-fatal: the overlay simply keeps drawing on top.
         */
        bool ensureInstalled();

        bool isInstalled();

        /**
         * Whether to capture at all. Capturing costs a few D3D queries per committed state, so it
         * stays off until something actually wants to be occluded.
         */
        void setCaptureRequested(bool requested);

        /**
         * Tell the capture what the overlay will be drawing into. Only a depth buffer of the same
         * size is kept, which is what ties a capture to the right view.
         */
        void setSubmittedTexture(ID3D11Texture2D* texture);

        /**
         * The depth captured for this texture during the current frame, or an invalid SceneDepth if
         * nothing matching was captured.
         *
         * The view is borrowed, not owned: it is good for this submit and must not be held past it.
         */
        SceneDepth acquireForSubmittedTexture(ID3D11Texture2D* texture);

        /**
         * Close the current frame. Captures are keyed to a frame counter so a stale one is never
         * handed to the next frame's submit.
         */
        void advanceFrame();
    }
}
