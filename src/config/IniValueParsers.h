#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

// The text forms of the INI values that are more than one number or word, apart from the game: plain std only, so
// it is unit tested.
namespace f4cf::config
{
    /**
     * A transform as an INI value has it: the position, the rotation as Euler angles in degrees, and the scale.
     */
    struct IniTransform
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float heading = 0.0f;
        float roll = 0.0f;
        float attitude = 0.0f;
        float scale = 1.0f;
    };

    std::optional<IniTransform> parseTransform(const char* text);

    std::optional<std::array<float, 22>> parseHandPose(const char* text);

    bool parseColor255(const std::string& text, std::array<float, 4>& color);

    bool takeName(std::string& names, std::string_view name);
}
