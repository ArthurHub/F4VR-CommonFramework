#pragma once

#include <optional>

#include "config/ConfigBase.h"
#include "modsconfig/ModsConfigFO4Api.h"

namespace f4cf::modsconfig
{
    /**
     * Mods Config FO4 VR's live preview of the mod's own config, with no code of the mod's: a change of a `live` setting
     * of its config spec that is kept in the mod's config file is a session override of the config, which reloads the
     * mod's values as a change of the file does. A preview sets it, a discard clears it, and a Save clears the ones
     * of the settings it wrote, as the file has their values then. The overrides are under an owner of their own, so
     * those of the devbench tool and of the mod's API callers stay.
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

    private:
        std::optional<config::ConfigKey> keyOf(const mods_config::api::Change& change) const;

        ConfigBase& _config;
    };
}
