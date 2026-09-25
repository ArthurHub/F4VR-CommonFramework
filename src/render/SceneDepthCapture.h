#pragma once

#include <d3d11.h>

namespace f4cf::render
{
    /**
     * The engine's scene depth for the eye texture currently being submitted, as a READ-ONLY view
     * lined up with that texture pixel for pixel.
     *
     * Read-only is the whole safety story. The buffer belongs to the engine and is still referenced
     * by it; a read-only depth view lets the overlay test against world depth while making it
     * impossible to write into what the engine owns. Under an upscaler the view is onto the
     * framework's own copy instead, made during the frame (see acquireForSubmittedTexture), read-only
     * all the same.
     *
     * The view is owned by the capture and stays valid only for the submit it was acquired in.
     */
    struct SceneDepth
    {
        ID3D11DepthStencilView* readOnlyView = nullptr;

        // The comparison that hides an overlay behind the world, whose depth is conventional - near
        // surfaces smaller; see WORLD_DEPTH_COMPARISON in SceneDepthCapture.cpp.
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
     * A matching size does not guarantee matching pixels, though. An upscaler driving the engine's
     * dynamic resolution (DLSS, FSR) keeps the buffer at the submitted size but draws the world into
     * a smaller viewport of it, top-left, upscales only the colour, and clears the buffer before the
     * frame is submitted. So the capture also counts the viewports the frame's passes draw into the
     * buffer with, and when the one most of them use does not cover the buffer, the world's depth is
     * copied during the frame, at the last pass that draws with it, resampled to the submitted size.
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
         * Whether to capture the next frame's depth. Capturing costs a few D3D queries per committed
         * state, and under an upscaler a full-screen copy per frame, so it is on only while something
         * draws against it: turning it off also frees that copy. RENDER thread only.
         */
        void setCaptureRequested(bool requested);

        /**
         * Tell the capture what the overlay will be drawing into. Only a depth buffer of the same
         * size is kept, which is what ties a capture to the right view.
         */
        void setSubmittedTexture(ID3D11Texture2D* texture);

        /**
         * The depth captured for this texture during the current frame, lined up with it, or an
         * invalid SceneDepth if nothing matching was captured - or if the world was drawn into part of
         * the buffer and no copy of it was made this frame or the last, since depth that does not line
         * up hides the wrong things. RENDER thread only.
         *
         * The view is borrowed, not owned: it is good for this submit and must not be held past it.
         */
        SceneDepth acquireForSubmittedTexture(ID3D11Texture2D* texture);

        /**
         * Closes the capture frame on scope exit. Captures are keyed to a frame counter so a stale one
         * is never handed to the next frame submit.
         *
         * A destructor rather than a call once the drawing is done: an early return out of the submit
         * path, or a throw out of an overlay, would otherwise leave the counter where it was - and
         * every commit of the next frame would then be counted into this one and hand out depth that
         * is a frame old, with nothing to say so. Construct one for the whole of a submit.
         */
        class FrameScope
        {
        public:
            FrameScope() = default;
            ~FrameScope();

            FrameScope(const FrameScope&) = delete;
            FrameScope& operator=(const FrameScope&) = delete;
            FrameScope(FrameScope&&) = delete;
            FrameScope& operator=(FrameScope&&) = delete;
        };
    }
}
