#include "GameInput.h"

#include <algorithm>
#include <optional>

#include <windows.h>

namespace f4cf::f4vr
{
    namespace
    {
        // BSInputEventReceiver::PerformInputProcessing, handed the head of the frame's queue
        constexpr std::size_t PERFORM_INPUT_PROCESSING_SLOT = 0;

        // how many events are remembered as read: more than a frame's queue holds
        constexpr std::size_t READ_EVENTS_KEPT = 256;

        // the mouse buttons F4SE has a code for
        constexpr std::uint32_t MOUSE_BUTTONS = 8;

        // the wheel's ids in the queue, a button event of the mouse for each notch, with a value of 120 or -120 and then 0
        constexpr std::uint32_t WHEEL_UP_ID = 0x800;
        constexpr std::uint32_t WHEEL_DOWN_ID = 0x900;

        /**
         * F4SE's code of a key or mouse button event: the key's code as the game has it, a mouse button's after the keys,
         * then the wheel's. None for a gamepad's, or an id F4SE has no code for.
         */
        std::optional<std::uint32_t> keyCodeOf(const RE::ButtonEvent& button)
        {
            if (button.idCode < 0) {
                return std::nullopt;
            }
            const auto id = static_cast<std::uint32_t>(button.idCode);
            switch (button.device.get()) {
            case RE::INPUT_DEVICE::kKeyboard:
                return id < GameInput::MOUSE_BUTTON_OFFSET ? std::optional(id) : std::nullopt;
            case RE::INPUT_DEVICE::kMouse:
                if (id == WHEEL_UP_ID) {
                    return GameInput::WHEEL_UP;
                }
                if (id == WHEEL_DOWN_ID) {
                    return GameInput::WHEEL_DOWN;
                }
                return id < MOUSE_BUTTONS ? std::optional(id + GameInput::MOUSE_BUTTON_OFFSET) : std::nullopt;
            default:
                return std::nullopt;
            }
        }

        /**
         * Whether a canvas that takes the input stops an event: a button, a typed character and a thumbstick. A mouse
         * move is not stopped, as it moves the game's cursor, nor a device's connection, which switches the game between
         * the gamepad and the mouse and keys.
         */
        bool isStoppable(const RE::InputEvent& event)
        {
            switch (event.eventType.get()) {
            case RE::INPUT_EVENT_TYPE::kButton:
            case RE::INPUT_EVENT_TYPE::kChar:
            case RE::INPUT_EVENT_TYPE::kThumbstick:
                return true;
            default:
                return false;
            }
        }
    }

    /**
     * Whether the key or mouse button of the given F4SE code went down in the queue the frame reads, the one processed
     * since the frame before began. Game thread.
     */
    bool GameInput::wasPressed(const std::uint32_t keyCode) const
    {
        return std::ranges::any_of(_frameEdges, [keyCode](const ButtonEdge& edge) {
            return edge.down && edge.keyCode == keyCode;
        });
    }

    /**
     * Read the game's input queue from now on, once the game has loaded: the processing of MenuControls, PlayerControls
     * and PlayerCamera is swapped in their vtables for ours, which reads the queue and calls on to it. Each reads the same
     * queue, and any one may not run in a frame, so all three are hooked and an event is read once. Never restored.
     */
    void GameInput::install()
    {
        if (_installed) {
            return;
        }
        _installed = true;

        // PlayerControls' receiver is a private base at its start
        auto* menuControls = RE::MenuControls::GetSingleton();
        auto* playerControls = RE::PlayerControls::GetSingleton();
        auto* playerCamera = RE::PlayerCamera::GetSingleton();
        const bool menuHooked = hookReceiver("MenuControls", menuControls, 0, &performInputProcessing<0>);
        const bool playerHooked = hookReceiver("PlayerControls", reinterpret_cast<RE::BSInputEventReceiver*>(playerControls), 1, &performInputProcessing<1>);
        const bool cameraHooked = hookReceiver("PlayerCamera", playerCamera, 2, &performInputProcessing<2>);
        if (menuHooked && playerHooked && cameraHooked) {
            logger::info("The game's input queue is read");
        }
    }

    /**
     * Swap a receiver's processing in its vtable for the given hook, which calls the one it replaced. A receiver whose
     * slot is ours already, or empty, is left alone.
     */
    bool GameInput::hookReceiver(const char* name, RE::BSInputEventReceiver* receiver, const std::size_t index, const PerformInputProcessing_t hook)
    {
        if (!receiver) {
            logger::warn("{} is not there; the game's input queue is not read", name);
            return false;
        }
        auto** slots = *reinterpret_cast<void***>(receiver);
        auto* const current = slots[PERFORM_INPUT_PROCESSING_SLOT];
        if (!current || current == reinterpret_cast<void*>(hook)) {
            logger::warn("{}'s input processing is not hooked: its vtable slot is {}", name, current ? "ours already" : "empty");
            return false;
        }
        DWORD oldProtect = 0;
        if (!VirtualProtect(&slots[PERFORM_INPUT_PROCESSING_SLOT], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            logger::warn("{}'s input processing is not hooked: VirtualProtect failed", name);
            return false;
        }
        s_originals[index].store(reinterpret_cast<PerformInputProcessing_t>(current), std::memory_order_release);
        slots[PERFORM_INPUT_PROCESSING_SLOT] = reinterpret_cast<void*>(hook);
        VirtualProtect(&slots[PERFORM_INPUT_PROCESSING_SLOT], sizeof(void*), oldProtect, &oldProtect);
        return true;
    }

    /**
     * A receiver's processing: the queue is read, and then processed as before, from the same head.
     */
    template <std::size_t Index>
    void GameInput::performInputProcessing(RE::BSInputEventReceiver* receiver, const RE::InputEvent* queueHead)
    {
        get().read(queueHead);
        if (const auto original = s_originals[Index].load(std::memory_order_acquire)) {
            original(receiver, queueHead);
        }
    }

    /**
     * Note the keys and mouse buttons that go down or up in the queue, and stop the events a canvas takes, each event
     * once: the stop stays on the event, and every receiver and menu skips a stopped event.
     */
    void GameInput::read(const RE::InputEvent* queueHead)
    {
        const bool stopping = _stopping.load(std::memory_order_relaxed);
        std::lock_guard lock(_lock);
        for (auto* event = const_cast<RE::InputEvent*>(queueHead); event; event = event->next) {
            const std::pair<const RE::InputEvent*, std::uint32_t> id{ event, event->timeCode };
            if (std::ranges::find(_read, id) != _read.end()) {
                continue;
            }
            _read.push_back(id);
            if (_read.size() > READ_EVENTS_KEPT) {
                _read.erase(_read.begin());
            }

            if (stop(*event, stopping)) {
                event->handled = RE::InputEvent::HANDLED_RESULT::kStop;
            }

            const auto* button = event->As<RE::ButtonEvent>();
            const auto keyCode = button ? keyCodeOf(*button) : std::nullopt;
            if (!keyCode) {
                continue;
            }
            if (button->QJustPressed()) {
                _pendingEdges.push_back({ .keyCode = *keyCode, .down = true });
            } else if (button->QReleased()) {
                _pendingEdges.push_back({ .keyCode = *keyCode, .down = false });
            }
        }
    }

    /**
     * Whether the event is stopped: while a canvas takes the input, each button, character and thumbstick event. A button
     * pressed then stays stopped until it is released, also once the canvas no longer takes the input, so the game never
     * sees only its release: Escape, which closes a panel, would open the pause menu as it is let go of. The release of a
     * button pressed before is not stopped, so the game, which saw it go down, sees it go up.
     */
    bool GameInput::stop(const RE::InputEvent& event, const bool stopping)
    {
        const auto* button = event.As<RE::ButtonEvent>();
        if (!button) {
            return stopping && isStoppable(event);
        }
        const auto key = std::pair{ static_cast<std::int32_t>(button->device.get()), button->idCode };
        const auto held = std::ranges::find(_stoppedButtons, key);
        if (!button->QPressed()) {
            if (held == _stoppedButtons.end()) {
                return false;
            }
            _stoppedButtons.erase(held);
            return true;
        }
        if (held != _stoppedButtons.end()) {
            return true;
        }
        if (stopping && button->QJustPressed()) {
            _stoppedButtons.push_back(key);
        }
        return stopping;
    }

    /**
     * As the frame begins: what the queue had since the frame before began is what the frame reads.
     */
    void GameInput::onFrameStart()
    {
        std::lock_guard lock(_lock);
        _frameEdges.clear();
        _frameEdges.swap(_pendingEdges);
    }

    /**
     * Whether the queue's buttons, characters and thumbsticks are stopped from now on, while a canvas takes the input.
     */
    void GameInput::setStopping(const bool stopping)
    {
        _stopping.store(stopping, std::memory_order_relaxed);
    }
}
