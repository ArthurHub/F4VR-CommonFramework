#include "ModsConfigClient.h"

using namespace mods_config::api;

namespace f4cf::modsconfig
{
    /**
     * The mod's one client.
     */
    ModsConfigClient& ModsConfigClient::get()
    {
        static ModsConfigClient client;
        return client;
    }

    /**
     * Registers the mod's client under its name, once. Mods Config VR that is not installed is not an error: the mod
     * logs one line and works without it.
     */
    void ModsConfigClient::registerMod(const std::string& name)
    {
        if (_client) {
            return;
        }
        if (const int err = ModsConfigVRApiV1::initialize(); err != 0) {
            logger::info("Mods Config VR is not available (error {}), so the mod has no client of its menu", err);
            return;
        }
        const auto* api = ModsConfigVRApiV1::inst;
        const ClientInfo info{ .version = MODS_CONFIG_VR_API_VERSION, .name = name.c_str(), .ctx = nullptr };
        _client = api->registerClient(&info);
        if (!_client) {
            logger::warn("Mods Config VR (v{}) did not register the client '{}': its log says why", api->getModVersion(), name);
            return;
        }
        _api = api;
        logger::info("Registered with Mods Config VR (v{}, API v{}) as client '{}'", api->getModVersion(), api->getVersion(), name);
    }
}
