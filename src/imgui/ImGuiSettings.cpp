#include "ImGuiSettings.h"

#include <algorithm>

namespace
{
    float s_supersample = 1.5f;
}

namespace f4cf::imgui
{
    void setSupersample(const float factor)
    {
        s_supersample = std::clamp(factor, MIN_SUPERSAMPLE, MAX_SUPERSAMPLE);
    }
}

namespace f4cf::imgui::internal
{
    float supersample()
    {
        return s_supersample;
    }
}
