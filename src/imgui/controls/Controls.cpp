#include "Controls.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "../../vrcf/VRControllersManager.h"
#include "../ImGuiKeyboard.h"
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

    imgui::controls::ControlsColors packedColors(const imgui::Theme& theme)
    {
        return { .accent = packed(theme.accent),
            .muted = packed(theme.muted),
            .track = packed(theme.track),
            .hover = packed(theme.hover),
            .pressed = packed(theme.pressed),
            .glow = packed(theme.glow),
            .rule = packed(theme.rule),
            .frameBorder = packed(theme.frameBorder),
            .onAccent = packed(theme.onAccent),
            .warning = packed(theme.warning),
            .onWarning = packed(theme.onWarning),
            .error = packed(theme.error),
            .onError = packed(theme.onError),
            .popup = packed(theme.popup),
            .scrim = packed(theme.scrim),
            .glass = packed(theme.glass),
            .plate = packed(theme.plate),
            .important = packed(theme.important) };
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
        : _colors(packedColors(_theme))
    {}

    /**
     * Sets the whole theme, which the next frame is drawn in: packs its colors once for drawing, gives the keyboard its
     * colors, and marks ImGui's style to be set from it as the next frame begins. A mod that wants its own keyboard look
     * sets it after this. Game thread, as the frames are built there.
     * The fields devbench's theme action set stay over it, until that action's reset.
     */
    void Controls::setTheme(const Theme& theme)
    {
        _modTheme = theme;
        drawTheme();
    }

    /**
     * Sets the given fields over the mod's theme, in themeJson's shapes, which the next frame is drawn in: devbench's
     * theme action, tuning a look live. They stay over every theme the mod sets until clearThemeOverride. A field that is
     * no field or a value of the wrong shape is an error, and nothing is set.
     */
    std::expected<void, std::string> Controls::overrideTheme(const nlohmann::json& fields)
    {
        const auto values = themeWithFields(_overrideValues, fields);
        if (!values) {
            return std::unexpected(values.error());
        }
        _overrideValues = *values;
        std::vector<std::string_view> overridden;
        for (const auto& field : themeFields()) {
            if (std::ranges::contains(_overridden, field.name) || fields.contains(std::string(field.name))) {
                overridden.push_back(field.name);
            }
        }
        _overridden = std::move(overridden);
        drawTheme();
        return {};
    }

    /**
     * Drops the fields devbench set, so the mod's theme is drawn as it set it.
     */
    void Controls::clearThemeOverride()
    {
        _overridden.clear();
        drawTheme();
    }

    /**
     * Makes the theme drawn from the mod's and the fields set over it: packs its colors once for drawing, gives the
     * keyboard its colors, and marks ImGui's style to be set from it as the next frame begins.
     */
    void Controls::drawTheme()
    {
        _theme = _modTheme;
        for (const auto& field : themeFields()) {
            if (!std::ranges::contains(_overridden, field.name)) {
                continue;
            }
            if (field.color) {
                _theme.*field.color = _overrideValues.*field.color;
            } else {
                _theme.*field.number = _overrideValues.*field.number;
            }
        }
        _colors = packedColors(_theme);
        _imGuiStyleApplied = false;
        keyboard().setStyle(_theme.keyboardStyle(keyboard().style()));
    }

    /**
     * Sets the current ImGui context's style from the theme, before its frame begins: a test's own context. The layer's
     * main context is set by updateImGuiStyle.
     */
    void Controls::applyImGuiStyle() const
    {
        _theme.applyTo(ImGui::GetStyle());
    }

    /**
     * Sets the main context's style from the theme before its frame begins, in the first frame and after a new theme:
     * ImGui reads the body font size as the frame begins, and a size set later in the frame is lost.
     */
    void Controls::updateImGuiStyle()
    {
        if (!_imGuiStyleApplied) {
            applyImGuiStyle();
            _imGuiStyleApplied = true;
        }
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
