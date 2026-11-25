#include "Controls.h"

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
            .popup = packed(style.popup) };
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
}
