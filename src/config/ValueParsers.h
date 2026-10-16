#pragma once

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

// The forms of the config values that are more than one number or word, apart from the game: plain std and toml++
// only, so it is unit tested.
namespace f4cf::config
{
    /**
     * A transform as the config has it: the position, the rotation as Euler angles in degrees, and the scale.
     */
    struct ParsedTransform
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float heading = 0.0f;
        float roll = 0.0f;
        float attitude = 0.0f;
        float scale = 1.0f;
    };

    std::expected<ParsedTransform, std::string> parseTransform(const toml::node& node);

    std::optional<toml::table> transformTableFromText(std::string_view text);

    std::expected<std::array<float, 22>, std::string> parseHandPose(const toml::node& node);

    std::string handPoseTomlText(const std::array<float, 22>& pose);

    std::optional<std::array<float, 22>> parseHandPoseText(const char* text);

    std::expected<void, std::string> parseColor(const toml::node& node, std::array<float, 4>& color);
}
