#pragma once

#include <cstdint>
#include <string_view>

#include "ConsoleLines.h"

/**
 * The game console's output and commands. The output is copied at the call that hands each printed line to the
 * console's buffer, installed on the first use, not at load, so a game that never asks pays nothing.
 */
namespace f4cf::f4vr::console
{
    bool startCapture();
    [[nodiscard]] bool isCapturing();
    [[nodiscard]] const ConsoleLines& lines();
    std::uint64_t executeCommand(std::string_view command);
}
