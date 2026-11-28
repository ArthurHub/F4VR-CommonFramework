#include "InputSource.h"

#include <cmath>

#include "../f4vr/F4VRUtils.h"
#include "../vrcf/VRControllersHaptic.h"
#include "../vrcf/VRControllersManager.h"
#include "ImGuiKeyboard.h"
#include "ImGuiPointer.h"
#include "ImGuiTheme.h"
#include "ImGuiWindowKeys.h"
#include "controls/Controls.h"

// VR's side of the input source, built for VR only: the wand pointer, the thumbstick of the hand that points, and the VR
// keyboard. Flat's is InputSourceFlat.cpp.
namespace f4cf::imgui::internal
{
    namespace
    {
        // the class of the game's window, which WinMain registers and creates it with
        constexpr auto GAME_WINDOW_CLASS = "Fallout4VR";
    }

    /**
     * Once the ImGui context is ready: the keys of the game's window, found by its class.
     */
    void InputSource::install()
    {
        WindowKeys::get().install(FindWindowA(GAME_WINDOW_CLASS, nullptr));
    }

    /**
     * Before each ImGui frame of the main context: the pointer first, then the keyboard, which reads the hands on it. While
     * a game menu takes the controllers' input the pointer is on none: the canvases are shown, and the game's menu has the
     * ray and the trigger. With no canvas shown, nothing is read from the game, and only a press that is still held is
     * waited out.
     */
    void InputSource::update(const std::vector<PointerTarget>& targets, const float deltaSeconds)
    {
        static const std::vector<PointerTarget> none;
        const auto& given = !targets.empty() && f4vr::isGameMenuTakingInput() ? none : targets;
        // a physical keyboard's Escape gives an edited field back its text, as ImGui does
        WindowKeys::get().setTakingKeys(!given.empty(), true);
        pointer().update(given);
        keyboard().update(given, given.empty() ? 0.0f : deltaSeconds);
    }

    /**
     * Whether ImGui uses the mouse, as the main context's frame begins: the pointer hides the wand from the game then.
     */
    void InputSource::setWanted(const bool wantCaptureMouse)
    {
        pointer().setWanted(wantCaptureMouse);
    }

    /**
     * After the main context's frame: a press of the frame that asked for its pulse gets it, once, on the hand that points.
     */
    void InputSource::afterFrame()
    {
        if (const auto pattern = controls::get().pressPulse(); pattern && controls::get().pulsed()) {
            vrcf::VRHaptics.trigger(pointer().state().primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand, *pattern);
        }
    }

    void InputSource::onCanvasRemoved(const Canvas* canvas)
    {
        pointer().onCanvasRemoved(canvas);
    }

    /**
     * Nothing: the window's keys are let go of by WindowKeys, and the trigger the pointer presses with is not the window's.
     */
    void InputSource::onFocusLost()
    {}

    /**
     * The keyboard in the theme's colors.
     */
    void InputSource::applyTheme(const Theme& theme)
    {
        keyboard().setStyle(theme.keyboardStyle(keyboard().style()));
    }

    /**
     * How far the thumbstick of the hand that points is pushed to a side. None while no hand points, as the thumbstick is
     * the game's then, and none for a push that is more up or down, which scrolls.
     */
    float InputSource::thumbstickSide() const
    {
        const auto& state = pointer().state();
        if (!state.canvas) {
            return 0.0f;
        }
        const auto push = vrcf::VRControllers.getThumbstickValue(state.primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand);
        return std::abs(push.x) > std::abs(push.y) ? push.x : 0.0f;
    }
}
