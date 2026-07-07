#pragma once

#include "DebugDraw.h"

namespace f4cf::debug::renderer
{
    /**
     * Internal render-thread side of the DebugDraw overlay: D3D11 wire/text renderer injected via
     * the OpenVR IVRCompositor::Submit vtable hook, drawing on the submitted left-eye texture with
     * the engine's own per-eye view-projection matrices (stereo-instanced into both halves of the
     * double-wide target). Port of ROCK's DebugBodyOverlay minus the physics-body parts — see
     * reference library knowledge-base/debug_draw_overlay.md and Analysis/gold/ROCK_RE_REFERENCE.md.
     *
     * Not part of the public API — mods use f4cf::debug::DebugDraw; only DebugDraw.cpp calls this.
     *
     * Credit: based on brunocatani work in https://github.com/brunocatani/ROCK
     */

    /**
     * Install the Submit hook + D3D resources if not yet installed (idempotent). Returns false when
     * the D3D device or the OpenVR compositor is not available yet — safe to retry every frame.
     */
    bool ensureInstalled();

    bool isInstalled();

    /**
     * Publish this frame's draws (game thread). The frame is swapped into the render-side buffer
     * under a mutex; an empty frame turns the render-side enabled flag off so the Submit hook is a
     * single atomic read when there is nothing to draw.
     */
    void publish(internal::RenderFrame&& frame);
}
