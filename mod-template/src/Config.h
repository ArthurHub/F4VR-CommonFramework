#pragma once

#include "Resources.h"
#include "common/CommonUtils.h"
#include "config/ConfigBase.h"
#include "f4vr/WandActivationSphere.h"

namespace my_mod
{
    static const auto BASE_MOD_PATH = BASE_PATH + "\\" + std::string(Version::PROJECT);
    static const auto CONFIG_PATH = BASE_MOD_PATH + "\\" + std::string(Version::PROJECT) + ".toml";

    /**
     * The mod's config: data/config/MyMod.toml, embedded as the default and reloaded live from the user's copy.
     */
    class Config : public ConfigBase
    {
    public:
        Config()
            : ConfigBase(Version::PROJECT, CONFIG_PATH, IDR_CONFIG)
        {}

        // the values, read in loadConfigInternal with the same defaults
        float myConfigValue = 3.0f;

        // the head gesture: the offhand at the face and its trigger, read from [MyMod_HeadActivation]
        f4vr::WandActivationConfig headActivation;

    protected:
        virtual void loadConfigInternal(const toml::table& config) override;
    };

    inline Config g_config;
}
