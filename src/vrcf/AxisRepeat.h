#pragma once

#include <cstdint>
#include <optional>

namespace f4cf::vrcf
{
    // defined in VRControllersManager.h, which holds an AxisRepeat per axis
    enum class Direction : std::uint8_t;

    /**
     * The auto-repeat of one controller axis: when a push of it is a step, for every caller of that axis. Plain std,
     * so it is unit tested apart from the game.
     */
    class AxisRepeat
    {
    public:
        // the first repeat of a held push waits at least this long, so a short flick is one step
        static constexpr float INITIAL_DELAY = 0.35f;
        // after a push held this long its repeat interval is halved, and after the second quartered
        static constexpr float FASTER_AFTER = 1.0f;
        static constexpr float FASTEST_AFTER = 2.5f;
        // a hold ends when the axis falls below this share of the threshold, so a stick at the edge keeps its pace
        static constexpr float RELEASE_SHARE = 0.7f;
        // a hold ends when its axis is not checked for this long, as when a mod stops reading it
        static constexpr float UNCHECKED_RELEASE = 0.25f;

        static std::optional<Direction> pushedDirection(float x, float y, float threshold);
        static bool accepts(Direction accepted, Direction pushed);

        std::optional<Direction> step(float x, float y, float now, float threshold, float cooldown, bool accelerate, Direction accepted);
        void reset();

    private:
        float along(float x, float y) const;
        float interval(float now, float cooldown, bool accelerate) const;

        bool _holding = false;
        Direction _direction{};
        float _holdStart = 0.0f;
        float _lastStep = 0.0f;
        std::optional<float> _lastCheck;
        // steps of the current hold, counted to two: the first, and every repeat after it
        int _steps = 0;
    };
}
