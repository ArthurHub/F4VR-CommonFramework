#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "config/ConfigBase.h"
#include "modsconfig/ModsConfigFO4Api.h"

namespace f4cf::modsconfig
{
    /**
     * Mods Config FO4 VR's live preview of the mod's own config, with no code of the mod's: a change of a `live` setting
     * of its config spec that is kept in the mod's config file is a session override of the config, which reloads the
     * mod's values as a change of the file does. A preview sets it, a discard clears it, and a Save clears the ones
     * of the settings it wrote, as the file has their values then. The overrides are under an owner of their own, so
     * those of the devbench tool and of the mod's API callers stay. A change the mod's own UI handed over is only
     * recorded, with no reload: the mod has the value already. It also tells the menu every override of a setting of
     * the file, so the menu shows the ones another caller set.
     */
    class ConfigPreviews
    {
    public:
        // the owner of the overrides, which no other caller of the config uses
        static constexpr auto OWNER = "ModsConfigFO4VR";

        explicit ConfigPreviews(ConfigBase& config)
            : _config(config)
        {}

        void subscribe(mods_config::api::ModsConfigFO4Client& client);
        void onChange(const mods_config::api::Change& change) const;
        void onSaved(const mods_config::api::Saved& saved) const;
        std::vector<mods_config::api::Override> overridesOf(const mods_config::api::Setting& setting) const;

    private:
        void apply(const config::ConfigKey& key, const mods_config::api::Change& change, bool reload) const;
        std::optional<config::ConfigKey> keyOf(mods_config::api::FileFormat format, std::string_view filePath, const std::vector<std::string_view>& path) const;

        ConfigBase& _config;
    };
}
