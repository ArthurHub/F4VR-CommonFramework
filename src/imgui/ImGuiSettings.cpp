#include "ImGuiSettings.h"

#include <algorithm>

namespace
{
    float s_fontSizePixels = 24.0f;
    float s_supersample = 1.5f;
}

namespace f4cf::imgui
{
    void setFontSizePixels(const float sizePixels)
    {
        s_fontSizePixels = std::clamp(sizePixels, MIN_FONT_SIZE_PIXELS, MAX_FONT_SIZE_PIXELS);
    }

    void setSupersample(const float factor)
    {
        s_supersample = std::clamp(factor, MIN_SUPERSAMPLE, MAX_SUPERSAMPLE);
    }
}

namespace f4cf::imgui::internal
{
    float fontSizePixels()
    {
        return s_fontSizePixels;
    }

    float supersample()
    {
        return s_supersample;
    }
}
