#pragma once

#include <mutex>
#include <string>

#include "PrimitiveDraw.h"
#include "SubmitHook.h"

namespace f4cf::render
{
    /**
     * Draws PrimitiveDraw frames over the VR view: colored world-space lines and glyph text, through
     * the engine's own per-eye matrices so world coordinates land exactly where the game drew the
     * world. One instance is one independent overlay layer - it registers its own draw callback on
     * the shared Submit hook, owns the game -> render handoff, and goes dormant when it has nothing
     * to draw. The D3D pipeline objects and the font are shared across instances.
     *
     * Usage from the game thread, every frame:
     *
     *     PrimitiveDraw frame;
     *     frame.addLine(a, b, colors::Red);
     *     frame.addBillboardText("142", hitPos, colors::Yellow);
     *     frame.viewerPosition = headPosition;
     *     _renderer.ensureInstalled();
     *     _renderer.publish(std::move(frame));
     *
     * Text is the framework's font - the embedded Roboto Medium, unless the mod ships its own at
     * CUSTOM_TEXT_FONT_PATH - drawn from a distance-field atlas, so it stays sharp at any size,
     * distance or angle; TextFont.h measures it.
     *
     * The instance must outlive the render thread's use of it: draw callbacks can never be
     * unregistered (see SubmitHook), so hold it as a static or a member of something that lives for
     * the process. Destroying one only deactivates it, which is best-effort against an in-flight
     * draw.
     */
    class PrimitiveDrawRenderer
    {
    public:
        /**
         * @param name identifies this layer in logs and in the hook's callback table.
         * @param drawOrder painter order against the other overlays; see DRAW_ORDER_* in
         *        SubmitHook.h. Lower draws first, i.e. ends up underneath.
         */
        /**
         * @param occluded whether the world hides what this layer draws. Off by default, which is
         *        what diagnostics want - a debug shape behind a wall is exactly the one you need to
         *        see. It also degrades safely: with no scene depth captured, an occluded layer simply
         *        draws on top as before.
         */
        explicit PrimitiveDrawRenderer(std::string name, int drawOrder = DRAW_ORDER_DEFAULT, bool occluded = false);
        ~PrimitiveDrawRenderer();

        PrimitiveDrawRenderer(const PrimitiveDrawRenderer&) = delete;
        PrimitiveDrawRenderer& operator=(const PrimitiveDrawRenderer&) = delete;
        PrimitiveDrawRenderer(PrimitiveDrawRenderer&&) = delete;
        PrimitiveDrawRenderer& operator=(PrimitiveDrawRenderer&&) = delete;

        /**
         * Build the shared D3D resources and register with the Submit hook host if not done yet
         * (idempotent). False while the D3D device or the OpenVR compositor is still unavailable -
         * safe to call every frame, and doing so also drives the hook's orphan detection.
         */
        bool ensureInstalled();

        bool isInstalled() const;

        /**
         * Build what every layer shares - the D3D pipeline, the font atlas upload with it, and the
         * Submit hook host - ahead of the first draw, so that draw does not stall its frame for it.
         * Registers no layer, so nothing draws and the hook stays dormant until one publishes.
         *
         * False when the D3D device or the OpenVR compositor is not up yet; the first
         * ensureInstalled() then does the work as it would have without a preload. ModBase calls
         * this on game loaded when Settings::preloadRendering is set.
         */
        static bool preload();

        /**
         * Hand this frame's primitives to the render thread (game thread). An empty frame goes
         * dormant, so the Submit hook stays a single atomic read when there is nothing to draw.
         */
        void publish(PrimitiveDraw&& frame);

    private:
        void drawFrame(const SubmitFrame& submitFrame);

        std::string _name;
        int _drawOrder;
        bool _occluded;
        DrawCallbackId _callbackId = INVALID_DRAW_CALLBACK;

        // read on the render thread under the mutex, written on the game thread by publish()
        PrimitiveDraw _frame;
        std::mutex _frameMutex;
    };
}
