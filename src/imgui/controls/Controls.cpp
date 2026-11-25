#include "Controls.h"

#include <cmath>

#include "../../vrcf/VRControllersManager.h"
#include "../ImGuiPointer.h"
#include "Sliders.h"

#include <imgui_internal.h>

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
     * Ends what a control holds open, for Back, one at a time: a slider's drag, with nothing written, as the trigger is
     * still held and the rest of its press changes nothing, or else the list that is open, with nothing picked, or else
     * the edit of a text input, with nothing more written, as the next frame begins. Returns whether there was one, so
     * Back does nothing else.
     */
    bool Controls::back()
    {
        if (_sliders.dragged && !_sliders.dragCancelled) {
            _sliders.dragCancelled = true;
            return true;
        }
        if (isListOpen()) {
            _choices.closeList = true;
            return true;
        }
        return endTextEdit();
    }

    /**
     * Ends the edit of a text input with nothing more written, as the next frame begins: for Back, and for a text
     * set by other means, as devbench sets a query. Returns false while none is edited.
     */
    bool Controls::endTextEdit()
    {
        if (!isTextEdited(_text.edited)) {
            return false;
        }
        _text.end = true;
        return true;
    }

    /**
     * Forgets what the controls hold, as a panel shows another page or opens an editor: their ids are the rows of what was
     * shown. A list that is open is closed as the next frame begins. A push of the thumbstick that is held is still not a
     * new one. The edit of a text input is kept, as a search typed in a header outlives the page: it ends once its field
     * is not drawn.
     */
    void Controls::reset()
    {
        _sliders = {};
        _choices.closeList = _choices.openList != 0;
        _choices.step = 0;
    }

    /**
     * Closes every popup but a modal one as the next frame begins, as a panel shows another page: its lists and its
     * editors.
     */
    void Controls::closePopups()
    {
        _choices.closeAll = true;
    }

    /**
     * A list of options is shown in this frame, by its popup's id, at a level among the open popups.
     */
    void Controls::listShown(const ImGuiID id, const int level)
    {
        _choices.openList = id;
        _choices.openLevel = level;
        _choices.openSeen = true;
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
     * Starts a frame, before any canvas's content: the steps the thumbstick moves the slider under the ray by, and the
     * stepper under it, which a push steps once, as it begins. The list Back closed is closed, and its popups over it,
     * and after a reset every popup but a modal one. The text edit Back ended leaves ImGui's field.
     */
    void Controls::beginFrame(const float deltaSeconds)
    {
        _sliders.dragHeld = false;
        _sliders.pointedSeen = false;
        _sliders.steps = internal::stepsOfPush(_sliders, _thumbstickSide, deltaSeconds);

        const int side = _thumbstickSide >= SLIDER_STEP_PUSH ? 1 : _thumbstickSide <= -SLIDER_STEP_PUSH ? -1 : 0;
        _choices.step = side != _choices.pushSide ? side : 0;
        _choices.pushSide = side;
        _choices.openSeen = false;
        if (_choices.closeAll) {
            ImGui::ClosePopupsExceptModals();
        } else if (_choices.closeList) {
            const auto& stack = ImGui::GetCurrentContext()->OpenPopupStack;
            if (_choices.openLevel < stack.Size && stack[_choices.openLevel].PopupId == _choices.openList) {
                ImGui::ClosePopupToLevel(_choices.openLevel, true);
            }
        }
        _choices.closeAll = false;
        _choices.closeList = false;

        if (_text.end) {
            if (ImGui::GetActiveID() == _text.field) {
                ImGui::ClearActiveID();
            }
            _text = {};
        }
        _text.seen = false;
    }

    /**
     * Ends a frame, after every canvas's content: a slider whose row was not held is not dragged any more, and one whose
     * row the ray has left is not the one the thumbstick steps. A text input that was not drawn is not edited any more,
     * with nothing written, as ImGui leaves a field that is not drawn.
     */
    void Controls::endFrame()
    {
        if (!_sliders.dragHeld) {
            _sliders.dragged = 0;
        }
        if (!_sliders.pointedSeen) {
            _sliders.pointed = 0;
        }
        if (!_choices.openSeen) {
            _choices.openList = 0;
        }
        if (_text.edited && !_text.seen) {
            _text = {};
        }
    }
}
