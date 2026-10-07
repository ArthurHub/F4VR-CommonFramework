#pragma once

#include <SimpleIni.h>
#include <array>

namespace f4cf::vrcf
{
    struct InputBinding;
}

namespace f4cf::f4vr
{
    struct WandActivationConfig;
}

namespace f4cf::render
{
    struct Color;
}

namespace f4cf::config
{
    /**
     * The readers of the INI values that are more than one number or word, into the types the game code uses.
     * Each reads a loaded INI, and returns the default it was given for a key that is missing or malformed.
     * ConfigBase derives from it, so a mod's config calls them by their names in its loadIniConfigInternal.
     */
    struct IniReaders
    {
        static RE::NiTransform getTransformValue(const CSimpleIniA& ini, const char* section, const char* key, const RE::NiTransform& defaultValue);
        static std::array<float, 22> getHandPoseValue(const CSimpleIniA& ini, const char* section, const char* key, const std::array<float, 22>& defaultValue);
        static vrcf::InputBinding getInputBindingValue(const CSimpleIniA& ini, const char* section, const char* key, const vrcf::InputBinding& defaultValue);
        static render::Color getColorValue(const CSimpleIniA& ini, const char* section, const char* key, const render::Color& defaultValue);
        static f4vr::WandActivationConfig loadWandActivationConfig(const CSimpleIniA& ini, const char* section, const f4vr::WandActivationConfig& defaults);
    };
}
