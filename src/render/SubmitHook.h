#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "RenderUtils.h"

namespace f4cf::render
{
    /**
     * What a draw callback is handed for the frame the game is submitting to the compositor. The
     * render target is the submitted eye texture itself: FO4VR renders both eyes into one
     * double-wide target, so `width` spans both halves and world-space geometry reaches its eye
     * through `camera` plus the shader's clip/cull split (see StereoCameraConstants).
     */
    struct SubmitFrame
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        ID3D11RenderTargetView* renderTarget = nullptr;
        float width = 0;
        float height = 0;
        const StereoCameraConstants* camera = nullptr;
    };

    /**
     * A draw callback, invoked on the RENDER thread from inside the Submit hook.
     *
     * Contract, and it is not negotiable:
     * - Never touch game-thread-owned state (nodes, forms, config). Snapshot it game-side and hand
     *   the snapshot over yourself; reading node->world here races the skeleton update.
     * - Never call into OpenVR. Two overlays both querying the runtime every frame double-drive
     *   vrclient; the engine's own matrices in SubmitFrame::camera are there so nobody has to.
     * - Leave the pipeline as you found it only for what you bind beyond the shared save/restore
     *   (the host snapshots and restores the standard surface around the whole callback list).
     */
    using SubmitDrawCallback = std::function<void(const SubmitFrame&)>;

    /**
     * Painter order for overlay draw callbacks: LOWER draws first and therefore ends up UNDERNEATH.
     *
     * Overlays are not depth-tested against each other, so whichever draws last wins the pixel. The
     * order has to be declared rather than inherited from registration order, because registration
     * is lazy - each overlay registers the first time it actually draws - which would otherwise make
     * the layering depend on which one the player happened to trigger first in a session.
     */
    inline constexpr int DRAW_ORDER_PANELS = 100;
    inline constexpr int DRAW_ORDER_DEFAULT = 500;
    // deliberately last, i.e. on top: diagnostics must never end up hidden behind a mod's UI
    inline constexpr int DRAW_ORDER_DEBUG = 900;

    /**
     * Handle returned by registerDrawCallback, used to flip that callback active/dormant.
     */
    using DrawCallbackId = std::uint32_t;

    inline constexpr DrawCallbackId INVALID_DRAW_CALLBACK = static_cast<DrawCallbackId>(-1);

    /**
     * Register a render-thread draw callback. Call once, from the game thread, before first use;
     * callbacks are never removed (see "never restore" in the hook docs). `name` shows up in logs
     * and `order` is the painter order above. A newly registered callback starts dormant - call
     * setDrawCallbackActive to make it draw.
     */
    DrawCallbackId registerDrawCallback(std::string name, SubmitDrawCallback callback, int order);

    /**
     * Flip one callback between drawing and dormant. Dormant is genuinely free: when no callback is
     * active the hook is one relaxed atomic read and a tail call to the original Submit.
     */
    void setDrawCallbackActive(DrawCallbackId id, bool active);

    /**
     * Install the D3D resources and the OpenVR IVRCompositor::Submit vtable hook if not already
     * installed. Idempotent and safe to call every frame from the game thread - each unavailable
     * dependency (VR runtime, D3D device, compositor) just retries - and it also drives the
     * orphan-detection heartbeat, so an active consumer should keep calling it.
     */
    bool ensureInstalled();

    bool isInstalled();
}
