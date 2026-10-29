#pragma once

#include "Resources.h"
#include "common/CommonUtils.h"
#include "config/ConfigBase.h"

namespace my_mod
{
    static const auto BASE_MOD_PATH = BASE_PATH + "\\" + std::string(Version::PROJECT);
    static const auto CONFIG_PATH = BASE_MOD_PATH + "\\" + std::string(Version::PROJECT) + ".toml";

    class Config : public ConfigBase
    {
    public:
        explicit Config()
            : ConfigBase(Version::PROJECT, CONFIG_PATH, IDR_CONFIG)
        {}

        // Mod configs
        float myConfigValue = 0.0f;

    protected:
        virtual void loadConfigInternal(const toml::table& config) override;
    };

    // Global singleton for easy access
    inline Config g_config;
}
