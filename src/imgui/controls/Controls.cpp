#include "Controls.h"

#include <cmath>

#include "../../vrcf/VRControllersManager.h"
#include "../ImGuiPointer.h"
#include "Sliders.h"

namespace
{
    using namespace f4cf;

    ImU32 packed(const render::Color& color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    }

    imgui::controls::ControlsColors packedColors(const imgui::controls::ControlsStyle& style)
    {
        return { .accent = packed(style.accent),
            .muted = packed(style.muted),
            .track = packed(style.track),
            .hover = packed(style.hover),
            .pressed = packed(style.pressed),
            .glow = packed(style.glow),
            .rule = packed(style.rule),
            .frameBorder = packed(style.frameBorder),
            .onAccent = packed(style.onAccent),
            .warning = packed(style.warning),
            .onWarning = packed(style.onWarning),
            .error = packed(style.error),
            .onError = packed(style.onError),
            .popup = packed(style.popup),
            .scrim = packed(style.scrim) };
    }
}

namespace f4cf::imgui::controls
{
    Controls& Controls::get()
    {
        static Controls instance;
        return instance;
    }

    Controls::Controls()
        : _colors(packedColors(_style))
    {}

    /**
     * Sets the whole style, and packs its colors once for drawing.
     */
    void Controls::setStyle(const ControlsStyle& style)
    {
        _style = style;
        _colors = packedColors(_style);
    }

    /**
     * The colors of a tone: the accent with the text on it, the warning with its own, or the error with its own.
     */
    ToneColors Controls::tone(const Tone tone) const
    {
        switch (tone) {
        case Tone::Warning:
            return { .color = _colors.warning, .onColor = _colors.onWarning };
        case Tone::Error:
            return { .color = _colors.error, .onColor = _colors.onError };
        case Tone::Normal:
            break;
        }
        return { .color = _colors.accent, .onColor = _colors.onAccent };
    }

    /**
     * Ends what a control holds open, for Back: a slider's drag, with nothing written. The trigger is still held then,
     * and the rest of its press changes nothing. Returns whether there was one, so Back does nothing else.
     */
    bool Controls::back()
    {
        if (!_sliders.dragged || _sliders.dragCancelled) {
            return false;
        }
        _sliders.dragCancelled = true;
        return true;
    }

    /**
     * Forgets what the controls hold, as a panel shows another page: their ids are the rows of the page before.
     */
    void Controls::reset()
    {
        _sliders = {};
    }

    /**
     * Reads the thumbstick of the hand that points, before the frame: how far it is pushed to a side. None while no hand
     * points, as the thumbstick is the game's then, and none for a push that is more up or down, which scrolls.
     */
    void Controls::updateInput()
    {
        const auto& state = pointer().state();
        if (!state.canvas) {
            _thumbstickSide = 0.0f;
            return;
        }
        const auto push = vrcf::VRControllers.getThumbstickValue(state.primaryHand ? vrcf::Hand::Primary : vrcf::Hand::Offhand);
        _thumbstickSide = std::abs(push.x) > std::abs(push.y) ? push.x : 0.0f;
    }

    /**
     * Starts a frame, before any canvas's content: the steps the thumbstick moves the slider under the ray by.
     */
    void Controls::beginFrame(const float deltaSeconds)
    {
        _sliders.dragHeld = false;
        _sliders.pointedSeen = false;
        _sliders.steps = internal::stepsOfPush(_sliders, _thumbstickSide, deltaSeconds);
    }

    /**
     * Ends a frame, after every canvas's content: a slider whose row was not held is not dragged any more, and one whose
     * row the ray has left is not the one the thumbstick steps.
     */
    void Controls::endFrame()
    {
        if (!_sliders.dragHeld) {
            _sliders.dragged = 0;
        }
        if (!_sliders.pointedSeen) {
            _sliders.pointed = 0;
        }
    }
}
