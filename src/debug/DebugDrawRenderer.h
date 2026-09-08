#pragma once

#include "DebugDraw.h"

namespace f4cf::debug::renderer
{
    /**
     * Internal render-thread side of the DebugDraw overlay: a D3D11 wire/text renderer registered as
     * a draw callback on the shared f4cf::render Submit hook, drawing on the submitted double-wide
     * eye texture with the engine's own per-eye view-projection matrices (stereo-instanced into both
     * halves). Port of ROCK's DebugBodyOverlay minus the physics-body parts — see reference library
     * knowledge-base/debug_draw_overlay.md and Analysis/gold/ROCK_RE_REFERENCE.md.
     *
     * Not part of the public API — mods use f4cf::debug::DebugDraw; only DebugDraw.cpp calls this.
     *
     * Credit: based on brunocatani work in https://github.com/brunocatani/ROCK
     */

    /**
     * Build the D3D resources and register with the shared Submit hook host if not done yet
     * (idempotent). Returns false while the D3D device or the OpenVR compositor is not available —
     * safe to retry every frame.
     */
    bool ensureInstalled();

    bool isInstalled();

    /**
     * Publish this frame's draws (game thread). The frame is swapped into the render-side buffer
     * under a mutex; an empty frame goes dormant on the hook host, so the Submit hook stays a single
     * atomic read when there is nothing to draw.
     */
    void publish(internal::RenderFrame&& frame);
}
