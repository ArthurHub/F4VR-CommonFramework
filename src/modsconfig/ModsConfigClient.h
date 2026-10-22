#pragma once

#include <string>

#include "ModsConfigVRApi.h"

namespace f4cf
{
    class ModBase;
}

namespace f4cf::modsconfig
{
    /**
     * The mod's client of Mods Config VR's native API (see README.md). ModBase registers it at F4SE's kPostLoad,
     * under the mod's name, while Mods Config VR is installed, so the mod's config spec is matched to it.
     */
    class ModsConfigClient
    {
    public:
        static ModsConfigClient& get();

        // the API: null while Mods Config VR is not installed, before kPostLoad, and for a mod that opts out
        const mods_config::api::ModsConfigVRApiV1* api() const
        {
            return _api;
        }

        // the mod's handle, which every call of the API for the mod takes: null while the API is
        mods_config::api::Client* client() const
        {
            return _client;
        }

        bool isRegistered() const
        {
            return _client != nullptr;
        }

    private:
        friend class f4cf::ModBase;

        void registerMod(const std::string& name);

        const mods_config::api::ModsConfigVRApiV1* _api = nullptr;
        mods_config::api::Client* _client = nullptr;
    };
}
