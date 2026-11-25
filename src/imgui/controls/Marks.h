#pragma once

#include <cstdint>

#include <imgui.h>

// The drawn marks and icons of the controls, each as its design is drawn in a box of its own size.
namespace f4cf::imgui::controls
{
    /**
     * A mark that stands for a state, with no text: a warning, an error, or an info.
     */
    enum class Mark : std::uint8_t
    {
        None,
        Warning,
        Error,
        Info,
    };

    /**
     * Where an arrow of a stepper's button and of a dropdown points.
     */
    enum class Arrow : std::uint8_t
    {
        Left,
        Right,
        Down,
    };

    // the box of the warning, the error and the info marks
    constexpr float MARK_WIDTH = 20.0f;

    // the box of the dot
    constexpr float DOT_WIDTH = 10.0f;

    // the box the arrow of a stepper's button and of a dropdown is drawn in
    constexpr float ARROW_SIZE = 14.0f;

    // the box of the arrow of a row that opens and closes
    constexpr float FOLD_ARROW_SIZE = 14.0f;

    // the box of the mark of a list's chosen option
    constexpr float CHECK_SIZE = 20.0f;

    // the strokes of a drawn icon, as its design draws it in a box of 24
    constexpr float ICON_THICKNESS = 2.5f;

    void drawWarningMark(const ImVec2& center);
    void drawErrorMark(const ImVec2& center);
    void drawInfoMark(const ImVec2& center);
    void drawMark(Mark mark, const ImVec2& center);
    void drawDot(const ImVec2& center, ImU32 color);
    void drawCheck(const ImVec2& min, ImU32 color);
    void drawResetIcon(const ImVec2& center, float scale = 1.0f);
    void drawUndoIcon(const ImVec2& center, float scale = 1.0f);
    void drawFoldArrow(const ImVec2& topLeft, bool open, ImU32 color);
    void drawControlArrow(const ImVec2& center, Arrow arrow, float scale = 1.0f);
}
