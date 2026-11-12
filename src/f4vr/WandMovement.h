#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace f4cf::f4vr
{
    /**
     * Keeps controller-direction movement ("bUseWandDirectionalMovement") going where the player means while the offhand
     * controller is held reversed, e.g. a flashlight in an overhand grip whose controller points backward.
     *
     * The game turns the offhand stick by the heading of that controller's nose. A mod that knows which way the player
     * means as forward passes it to alignTo() each frame; when the controller points more than 90 degrees away from it,
     * the offhand stick the game reads is inverted (vrcf::VRControllersSuppress::setAxisInverted, under the mod's key).
     *
     * A call lasts until the end of the next frame: a mod stops the correction by not calling, so a skipped update (main
     * menu, loading) can't leave the stick inverted. Game thread only.
     */
    class WandMovement
    {
    public:
        static void alignTo(std::string_view key, const RE::NiPoint3& worldForward);

        // Driven by ModBase at each frame start: ends the corrections not renewed in the last frame.
        static void onFrameStart();

    private:
        struct StringHash
        {
            using is_transparent = void;

            std::size_t operator()(const std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }
        };

        // The frame each owner last called alignTo() in.
        inline static std::unordered_map<std::string, std::uint64_t, StringHash, std::equal_to<>> _lastCallFrame;
        inline static std::uint64_t _frame = 0;
    };
}
