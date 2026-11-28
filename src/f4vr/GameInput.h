#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace f4cf::f4vr
{
    /**
     * Flat: the keys and mouse buttons pressed in the game's input queue, read before anything stops them, so a mod's key
     * is seen while an ImGui canvas takes the input as well. Codes are F4SE's: a key's virtual-key code (0x79 F10, 0x1B
     * Escape), a mouse button 256 and up, the wheel 264 up and 265 down. Gamepad buttons are not read.
     *
     *     if (f4vr::GameInput::get().wasPressed(0x79)) { toggleMenu(); }
     */
    class GameInput
    {
    public:
        static GameInput& get()
        {
            static GameInput instance;
            return instance;
        }

        // a mouse button's code is this plus its number: 0 left, 1 right, 2 middle
        static constexpr std::uint32_t MOUSE_BUTTON_OFFSET = 256;
        static constexpr std::uint32_t WHEEL_UP = 264;
        static constexpr std::uint32_t WHEEL_DOWN = 265;

        bool wasPressed(std::uint32_t keyCode) const;

        // Internal: a button going down or up in the queue.
        struct ButtonEdge
        {
            std::uint32_t keyCode = 0;
            bool down = false;
        };

        // Internal: GameRuntime and the ImGui input source call these, on the game thread.
        void install();
        void onFrameStart();
        void setStopping(bool stopping);

        std::span<const ButtonEdge> edges() const
        {
            return _frameEdges;
        }

    private:
        GameInput() = default;

        using PerformInputProcessing_t = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);

        // the receivers whose queue is read: MenuControls, PlayerControls and PlayerCamera
        static constexpr std::size_t RECEIVERS = 3;

        template <std::size_t Index>
        static void performInputProcessing(RE::BSInputEventReceiver* receiver, const RE::InputEvent* queueHead);

        static bool hookReceiver(const char* name, RE::BSInputEventReceiver* receiver, std::size_t index, PerformInputProcessing_t hook);
        void read(const RE::InputEvent* queueHead);
        bool stop(const RE::InputEvent& event, bool stopping);

        // the receivers' processing before ours: the game's, or another framework mod's that was put there first
        static inline std::atomic<PerformInputProcessing_t> s_originals[RECEIVERS]{};

        bool _installed = false;
        std::atomic<bool> _stopping{ false };

        // what the queue had since the frame began, and what it had in the frame before, which the frame reads
        mutable std::mutex _lock;
        std::vector<ButtonEdge> _pendingEdges;
        std::vector<ButtonEdge> _frameEdges;

        // the events read last, by their address and time code, so each is read once: the receivers read the same queue,
        // and the game reuses an event's place for a later event, with its own time code
        std::vector<std::pair<const RE::InputEvent*, std::uint32_t>> _read;

        // the buttons pressed while the queue was stopped and not released since, by device and id: their release is
        // stopped too
        std::vector<std::pair<std::int32_t, std::int32_t>> _stoppedButtons;
    };
}
