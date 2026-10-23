#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace f4cf::render
{
    std::string ddsHeaderError(std::span<const std::uint8_t> bytes);
}
