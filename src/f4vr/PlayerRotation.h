#pragma once

#include <chrono>
#include <cstdint>

namespace f4cf::f4vr
{
    /**
     * The turning style the player picked in the VR comfort settings ("iRotationType:VR").
     */
    enum class VRRotationType : std::uint8_t
    {
        None = 0, // turning is off
        SnapSmoothed = 1, // snap by a fixed angle, interpolated there at fAngleSnapSmoothingSpeed:VR
        SnapInstant = 2, // snap by a fixed angle in one frame (vanilla also plays a comfort fade)
        Smooth = 3, // continuous turn at fRotationSpeed:VR while the stick is held
    };

    /**
     * Turn the player the way the game itself would, including where the game refuses to: the vanilla turn is
     * driven by a player-controls input handler, so anything that takes the controls away (dialogue, most
     * menus) also takes turning away, while the mod can still see the stick through OpenVR.
     *
     * Turning in VR rotates the VR world (room) transform, not the actor — the actor's heading follows the
     * HMD — so this writes the same transform the engine's own turn code writes (f4vr::VRWorld_SetYaw), and
     * reads the style / angle / speed straight off the engine's parsed Fallout4Prefs.ini [VR] settings, so a
     * change made in the in-game settings menu applies without a restart.
     *
     * The interpolation of a smoothed snap is run here rather than handed to the engine's per-frame applier,
     * which is gated behind a latch only the vanilla input handler clears — see the note in F4VROffsets.h.
     * The one vanilla behaviour not reproduced is the comfort fade of the instant-snap style.
     *
     * Main thread only, and `turnByThumbstick()` (or `onFrameUpdate()`) must be called every frame for a
     * smoothed snap to finish.
     */
    class PlayerRotation
    {
    public:
        // The player's configured turning style and its tuning, read live.
        static VRRotationType getRotationType();
        static float getSnapAngleDegrees();
        static float getSmoothSpeedDegreesPerSec();
        static float getSnapSmoothingSpeedDegreesPerSec();

        /**
         * Feed the thumbstick X axis (-1 left .. 1 right) every frame to turn exactly as the player's settings
         * say: a snap per flick for the snap styles, a continuous turn while held for the smooth style, and
         * nothing at all when turning is off. `threshold` is how far the stick must be pushed; the vanilla
         * handler asks for 0.5. Also advances an in-progress smoothed snap, so this is the only per-frame call
         * a caller needs.
         */
        static void turnByThumbstick(float axisX, float threshold = 0.5f);

        /**
         * Advance an in-progress smoothed snap. Only needed when turning is driven by something other than
         * `turnByThumbstick()`, which does it already.
         */
        static void onFrameUpdate();

        // One configured snap, honouring the smoothed / instant style. Ignores the stick threshold and latch.
        static void snapTurn(bool right);

        // Continuous turn for one frame of `deltaSeconds` at the configured smooth speed.
        static void smoothTurn(bool right, float deltaSeconds);

        // Raw access to the VR world yaw in radians, bypassing the configured style entirely.
        static float getYaw();
        static void setYaw(float yawRadians);
        static void rotateBy(float deltaRadians);

        static bool isSnapInProgress();

        /**
         * Drop an in-progress smoothed snap where it is, and un-latch the stick. Worth calling when whatever
         * was driving the turn goes away mid-snap (the menu closed, a save was loaded).
         */
        static void cancel();

    private:
        static void stepPendingSnap(float deltaSeconds);
        static float frameDeltaSeconds();

        // Radians still owed by a smoothed snap, signed; 0 = none in progress.
        inline static float _pendingSnap = 0.0f;

        // Set while the stick is held past the threshold, so one flick is one snap.
        inline static bool _turnLatched = false;

        inline static std::chrono::steady_clock::time_point _lastFrameTime{};
    };
}
