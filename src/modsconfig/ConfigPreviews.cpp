#include "ConfigPreviews.h"

#include <charconv>
#include <filesystem>
#include <optional>
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

    /**
     * A number read from the whole of a text, or none when the text is not one.
     */
    template <typename T>
    std::optional<T> wholeNumber(const std::string_view text)
    {
        T value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() ? std::optional{ value } : std::nullopt;
    }

    /**
     * The text of a value as the config keeps it, see config::appliedText, as a value of a setting's valueType: "true"
     * or "false" for a bool, and a number's TOML text for a number. The text as a string when it is not of the type.
     */
    mods_config::api::Value valueOf(const std::string& text, const mods_config::api::ValueType type)
    {
        using mods_config::api::ValueType;
        switch (type) {
        case ValueType::Bool:
            if (text == "true" || text == "false") {
                return text == "true";
            }
            break;
        case ValueType::Int:
            if (const auto number = wholeNumber<int>(text)) {
                return *number;
            }
            break;
        case ValueType::Float:
            if (const auto number = wholeNumber<float>(text)) {
                return *number;
            }
            break;
        case ValueType::String:
            break;
        }
        return text;
    }
}

namespace f4cf::modsconfig
{
    /**
     * Has the client hand this the changes and the Saves of the mod's config spec, and ask it the overrides of a setting.
     */
    void ConfigPreviews::subscribe(mods_config::api::ModsConfigFO4Client& client)
    {
        client.subscribeToChanges([this](const mods_config::api::Change& change) {
            onChange(change);
        });
        client.subscribeToSaves([this](const mods_config::api::Saved& saved) {
            onSaved(saved);
        });
        client.addOverrides([this](const mods_config::api::Setting& setting) {
            return overridesOf(setting);
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
        const auto key = keyOf(change.format, change.filePath, change.path);
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
            if (const auto key = keyOf(setting.format, setting.filePath, setting.path)) {
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
    std::optional<config::ConfigKey> ConfigPreviews::keyOf(const mods_config::api::FileFormat format, const std::string_view filePath,
        const std::vector<std::string_view>& path) const
    {
        if (format != mods_config::api::FileFormat::Toml || path.empty() || filePath.empty() || comparablePath(filePath) != comparablePath(_config.getConfigFilePath())) {
            return std::nullopt;
        }
        return config::ConfigKey::ofPath({ path.begin(), path.end() });
    }

    /**
     * Every session override of a setting kept in the mod's config file, the one in effect first, by any owner: the
     * menu leaves out its own previews. Each value is in the setting's valueType, read from the text the override keeps,
     * see ConfigBase::setConfigOverride, or that text as a string when it is not of the type. None for a setting of
     * another file or of the game.
     */
    std::vector<mods_config::api::Override> ConfigPreviews::overridesOf(const mods_config::api::Setting& setting) const
    {
        const auto key = keyOf(setting.format, setting.filePath, setting.path);
        if (!key) {
            return {};
        }
        std::vector<mods_config::api::Override> overrides;
        for (auto& applied : _config.getConfigOverrides(key->section.c_str(), key->key.c_str())) {
            overrides.push_back({ .owner = std::move(applied.owner), .value = valueOf(applied.value, setting.valueType) });
        }
        return overrides;
    }
}
