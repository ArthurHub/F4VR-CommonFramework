#pragma once

#include <array>

// The framework's color, plain std only, so the config's parsers and their unit tests use it with no renderer.
namespace f4cf::render
{
    /**
     * RGBA color, components in [0,1].
     */
    struct Color
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;

        /**
         * A colour from the 0-255 bytes a colour picker reports, rather than the 0-1 floats stored
         * here.
         *
         * Sampling a colour off a screenshot gives bytes, and putting those straight into the fields
         * saturates every channel to white, since anything at or above 1.0 is full brightness. This
         * is the conversion, so a sampled value can stay written the way it was read.
         *
         * The overlay draws into the eye texture after the engine has finished tonemapping it, and
         * that target is not sRGB-encoded, so no gamma step comes between these bytes and the final
         * image: a byte in is that byte out.
         */
        static constexpr Color rgba(const int red, const int green, const int blue, const int alpha = 255)
        {
            return { static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f, static_cast<float>(blue) / 255.0f, static_cast<float>(alpha) / 255.0f };
        }

        /**
         * The channels as the 0-255 bytes `rgba` takes, red, green, blue and alpha, each the nearest one and clamped to
         * the range: the bytes a color was made from come back as they were, as the config writes them.
         */
        constexpr std::array<int, 4> bytes() const
        {
            const auto byte = [](const float channel) {
                const float clamped = channel < 0.0f ? 0.0f : channel > 1.0f ? 1.0f : channel;
                return static_cast<int>(clamped * 255.0f + 0.5f);
            };
            return { byte(r), byte(g), byte(b), byte(a) };
        }

        bool operator==(const Color&) const = default;
    };

    namespace colors
    {
        inline constexpr Color Red{ 1, 0, 0 };
        inline constexpr Color Green{ 0, 1, 0 };
        inline constexpr Color Blue{ 0, 0.6f, 1 };
        inline constexpr Color Yellow{ 1, 1, 0 };
        inline constexpr Color Cyan{ 0, 1, 1 };
        inline constexpr Color Magenta{ 1, 0.25f, 0.95f };
        inline constexpr Color Orange{ 1, 0.6f, 0.1f };
        inline constexpr Color White{ 1, 1, 1 };
        inline constexpr Color Grey{ 0.6f, 0.6f, 0.6f };
    }
}
