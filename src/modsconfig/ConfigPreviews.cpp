#include "ConfigPreviews.h"

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "common/CommonUtils.h"
#include "perf/Perf.h"

namespace
{
    /**
     * A path of a file as one is compared with another: normal, with the separators of Windows, in lower case, as
     * Windows matches a file's name with no regard to case.
     */
    std::string comparablePath(const std::string_view path)
    {
        return f4cf::common::str_tolower(std::filesystem::path(path).lexically_normal().make_preferred().string());
    }
}

namespace f4cf::modsconfig
{
    /**
     * Has the client hand this the changes and the Saves of the mod's config spec.
     */
    void ConfigPreviews::subscribe(mods_config::api::ModsConfigFO4Client& client)
    {
        client.subscribeToChanges([this](const mods_config::api::Change& change) {
            onChange(change);
        });
        client.subscribeToSaves([this](const mods_config::api::Saved& saved) {
            onSaved(saved);
        });
    }

    /**
     * Sets the override of a preview, or clears it for a discard, as the value is back at the file's. Nothing for a
     * setting kept anywhere but the mod's config file. A change the mod's own setPendingValue handed over is only
     * recorded, with no reload of the config: the mod applied the value before it handed it over, and a reload
     * would drop the values it changed and has not handed over yet. So the mod can hand over a value every frame.
     */
    void ConfigPreviews::onChange(const mods_config::api::Change& change) const
    {
        F4CF_PERF_FUNCTION();
        const auto key = keyOf(change);
        if (!key) {
            return;
        }
        if (change.handedOver) {
            F4CF_PERF_SCOPE("handedOver");
            apply(*key, change, false);
            return;
        }
        apply(*key, change, true);
    }

    /**
     * Sets the override of a change's key to its value, or clears it for a discard, and reloads the config for it
     * when `reload` is set.
     */
    void ConfigPreviews::apply(const config::ConfigKey& key, const mods_config::api::Change& change, const bool reload) const
    {
        if (change.discard) {
            _config.clearConfigOverrides(OWNER, { key }, reload);
            return;
        }
        const auto value = std::visit(
            [](const auto& typed) {
                return config::ConfigValue(typed);
            },
            change.value);
        // a refused value is in the log, with why, and the mod keeps the value it had
        (void)_config.setConfigOverrides(OWNER, { config::ConfigEntry{ key.section, key.key, value } }, reload);
    }

    /**
     * Clears the overrides of the settings of the mod's config file a Save wrote, with one reload, which reads the file:
     * it has their values by now, so they apply at once, before the file watch loads them.
     */
    void ConfigPreviews::onSaved(const mods_config::api::Saved& saved) const
    {
        std::vector<config::ConfigKey> keys;
        for (const auto& setting : saved.settings) {
            if (const auto key = keyOf(setting)) {
                keys.push_back(*key);
            }
        }
        if (!keys.empty()) {
            _config.clearConfigOverrides(OWNER, keys);
        }
    }

    /**
     * The key of the config of a setting kept in the mod's config file, by the path of its value from the file's root.
     * None for one of another file, as another mod's, and of the game.
     */
    std::optional<config::ConfigKey> ConfigPreviews::keyOf(const mods_config::api::Change& change) const
    {
        if (change.format != mods_config::api::FileFormat::Toml || change.path.empty() || change.filePath.empty() ||
            comparablePath(change.filePath) != comparablePath(_config.getConfigFilePath())) {
            return std::nullopt;
        }
        return config::ConfigKey::ofPath({ change.path.begin(), change.path.end() });
    }
}
