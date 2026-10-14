#include "ConfigBase.h"

#include <nlohmann/json.hpp>

#include "ConfigMigration.h"
#include "ValueParsers.h"
#include "devbench/DevBench.h"
#include "perf/Perf.h"

namespace f4cf
{
    /**
     * Load the config from its file.
     */
    void ConfigBase::load()
    {
        logger::info("Load config...");
        common::createDirDeep(_configFilePath);
        loadConfig();
    }

    /**
     * Re-read all values from the file on disk. Skips version migration and watcher setup so it's safe to call
     * repeatedly at runtime (e.g. from the DebugAdjuster reload action).
     */
    void ConfigBase::reload()
    {
        loadConfigValues();
        devbench::emit("config.reloaded", [&] {
            return nlohmann::json{ { "file", fileName() }, { "trigger", "reload" } };
        });
    }

    /**
     * Load the default config that is embedded in the mod's DLL and not the file on disk.
     */
    void ConfigBase::loadEmbeddedDefaultOnly()
    {
        std::lock_guard lock(_loadMutex);

        const auto config = loadEmbeddedDefault();
        loadDebugSection(config);
        loadConfigInternal(config);
        _subscribers.markReloaded();
    }

    /**
     * The config file's name, which names it in the log.
     */
    std::string ConfigBase::fileName() const
    {
        return fs::path(_configFilePath).filename().string();
    }

    /**
     * Parse the default config that is embedded in the mod's DLL. Throws when it is not TOML, which is the mod's bug.
     */
    toml::table ConfigBase::loadEmbeddedDefault() const
    {
        auto parsed = common::parseToml(common::getEmbededResourceAsString(_module, _defaultConfigEmbeddedResourceId), "the embedded default config");
        if (!parsed) {
            logger::error("Config: {}", parsed.error());
            throw std::runtime_error("The embedded default config is not TOML: " + parsed.error());
        }
        return std::move(*parsed);
    }

    /**
     * Subscribe to be told when the config values were loaded again: a change of the file on disk, a session
     * override, or reload(). The callback runs on the game thread at the start of the next frame, once for all
     * the loads since the frame before. Call it on the game thread. A key that is already subscribed throws.
     */
    void ConfigBase::subscribeForConfigChangedEvent(const std::string& key, const std::function<void(const std::string&)>& callback)
    {
        _subscribers.subscribe(key, callback);
    }

    /**
     * Remove the config change subscription for the given key.
     */
    void ConfigBase::unsubscribeFromConfigChangedEvent(const std::string& key)
    {
        _subscribers.unsubscribe(key);
    }

    /**
     * Call the subscribers if the config values were loaded again since the last call.
     * A load runs on another thread at any point of a frame, and only marks that it ran. ModBase calls this at
     * the start of every frame on the game thread, and a mod with a frame update of its own calls it there.
     */
    void ConfigBase::notifySubscribersOfReload()
    {
        _subscribers.notify([](const std::string& key) {
            logger::debug("Notify config change subscriber '{}'", key);
        });
    }

    /**
     * Check if a debug data dump is requested for the given name in sDumpDataOnceNames, see config::takeName.
     * The name is then removed from the list, in memory and in the file, so the dump runs once and a reload does
     * not request it again.
     */
    bool ConfigBase::checkDebugDumpDataOnceFor(const char* name)
    {
        if (!config::takeName(debug.dumpDataOnceNames, name)) {
            return false;
        }
        // write to the file for auto-reload not to re-enable it
        saveConfigValue(CONFIG_SECTION_DEBUG, "sDumpDataOnceNames", debug.dumpDataOnceNames.c_str());

        logger::info("---- Debug Dump Data check passed for '{}' ----", name);
        return true;
    }

    /**
     * Consume the one-shot "sAddItemsOnceNames" flag.
     * Returns the current value and clears it (in memory + file) so the bulk item-add runs exactly once,
     * and is not re-triggered by the file-watcher reloading the same value.
     */
    std::string ConfigBase::consumeDebugAddItemsOnce()
    {
        if (debug.addItemsOnceNames.empty()) {
            return {};
        }
        auto spec = debug.addItemsOnceNames;
        debug.addItemsOnceNames.clear();
        saveConfigValue(CONFIG_SECTION_DEBUG, "sAddItemsOnceNames", "");
        return spec;
    }

    /**
     * Load all config values.
     * If the file doesn't exist it is created from the default embedded in the mod's DLL.
     * If the file's version is older than the default's, the file is brought up to it, see updateConfigToLatestVersion.
     * A file that is not TOML is left as it is, for its user to fix: the values are the embedded default's until then.
     */
    void ConfigBase::loadConfig()
    {
        F4CF_PERF_FUNCTION();

        // create the file if it doesn't exist
        common::createFileFromResourceIfNotExists(_configFilePath, _module, _defaultConfigEmbeddedResourceId, true);

        if (loadConfigValues()) {
            const auto latestVersion = loadEmbeddedDefault()[CONFIG_SECTION_DEBUG]["iVersion"].value_or(0);
            if (_configVersion < latestVersion) {
                logger::info("Updating config version {} -> {}", _configVersion, latestVersion);
                updateConfigToLatestVersion(_configVersion, latestVersion);

                // reload the config after update
                loadConfigValues();
            }
        }

        startConfigFileWatch();
    }

    /**
     * Read the [Debug] section: the values that are the config's own, and the rest into `debug`.
     */
    void ConfigBase::loadDebugSection(const toml::table& config)
    {
        const auto section = config[CONFIG_SECTION_DEBUG];
        _configVersion = section["iVersion"].value_or(0);
        _logLevel = section["iLogLevel"].value_or(2);
        _logPattern = section["sLogPattern"].value_or(std::string("%H:%M:%S.%e %l [%-20!k] %v"));
        debug.load(config);
    }

    /**
     * Load all the config values from `source`, see config::ConfigFile::Source, with the session overrides on top.
     * It runs on the file-watch thread for a change on disk and on the caller's thread for an override, so the
     * whole load is under one lock, the read of the file included: the load that read last is applied last.
     * A file that can't be read, or is not TOML, is logged with why, and the values stay as they were: the ones of
     * the load before, or the embedded default's when there was none, so the mod always has values.
     * Returns whether the file was loaded: false from `ChangedFile` too when the file holds what the mod last loaded
     * or saved, which loads nothing.
     */
    bool ConfigBase::loadConfigValues(const config::ConfigFile::Source source)
    {
        F4CF_PERF_FUNCTION();
        std::lock_guard lock(_loadMutex);

        toml::table config;
        std::string error;
        const auto result = _configFile.load(_configFilePath, config, error, source);
        switch (result) {
        case config::ConfigFile::LoadResult::Unchanged:
            return false;
        case config::ConfigFile::LoadResult::Failed:
            logger::error("Config: failed to read '{}'", _configFilePath);
            break;
        case config::ConfigFile::LoadResult::Invalid:
            logger::error("Config: {} is not valid TOML, fix it to load it: {}", fileName(), error);
            devbench::emit("config.error", [&] {
                return nlohmann::json{ { "file", fileName() }, { "error", error } };
            });
            break;
        case config::ConfigFile::LoadResult::Loaded:
            if (source == config::ConfigFile::Source::ChangedFile) {
                logger::info("Config change detected, reload...");
            }
            break;
        }

        if (result != config::ConfigFile::LoadResult::Loaded) {
            if (_valuesLoaded) {
                logger::warn("Config: keeping the values loaded before");
                return false;
            }
            logger::warn("Config: using the default values until {} is fixed", fileName());
            config = loadEmbeddedDefault();
        }

        _overrides.applyTo(config);
        applyConfig(config);
        _valuesLoaded = true;
        return result == config::ConfigFile::LoadResult::Loaded;
    }

    /**
     * Apply an already-loaded config to all in-memory config members (debug section, logger, and the inherited mod
     * values). Shared by loadConfigValues and applyConfigWithOverride so both the on-disk reload and the in-memory
     * live override run the exact same propagation path.
     */
    void ConfigBase::applyConfig(const toml::table& config)
    {
        loadDebugSection(config);

        // set log after loading from config
        logger::setLogLevelAndPattern(_logLevel, _logPattern);

        // let inherited class load all its values
        loadConfigInternal(config);

        // for the subscribers, which are called on the game thread, see notifySubscribersOfReload
        _subscribers.markReloaded();
    }

    /**
     * Re-apply the config as the mod last loaded or saved it to all config members, with one key overridden in memory
     * only, in the type the file has for it, see config::applyOverride. No disk is read or written, so the
     * DebugAdjuster field mode calls it every frame to preview a field.
     */
    void ConfigBase::applyConfigWithOverride(const char* section, const char* key, const char* value)
    {
        std::lock_guard lock(_loadMutex);

        toml::table config;
        if (!loadKeptConfig(config)) {
            return;
        }
        // persistent session overrides first, then the caller's transient one-off override on top
        _overrides.applyTo(config);
        config::applyOverride(config, { section, key }, value);
        applyConfig(config);
    }

    /**
     * Get the effective value of a section/key as text: the session override if one is set, otherwise the value in
     * the file as the mod last loaded or saved it, otherwise defaultValue. A string is its text and any other value
     * its TOML text ("true", "1.5", "[1, 2]"). The caller parses it.
     * For a key the file does not have, defaultValue can differ from the default the mod loads its member with.
     */
    std::string ConfigBase::getConfigValue(const char* section, const char* key, const char* defaultValue) const
    {
        if (const auto value = _overrides.find({ section, key })) {
            return *value;
        }

        toml::table config;
        const auto* node = loadKeptConfig(config) ? config[section][key].node() : nullptr;
        if (!node) {
            return defaultValue ? defaultValue : std::string{};
        }
        if (const auto* string = node->as_string()) {
            return string->get();
        }
        return common::TomlEditor::Value(*node).text();
    }

    /**
     * Override the value of a section/key in memory for the rest of the session, and reload the config so the
     * members have it. It is applied again on every load and never written to disk. A string value is taken in the
     * type the file has for the key, see config::applyOverride.
     * `owner` is the name of who sets it. An owner sets and clears only its own overrides, and when two override
     * the same key the one set last applies, see config::Overrides.
     */
    void ConfigBase::setConfigOverride(const std::string& owner, const char* section, const char* key, const config::ConfigValue& value)
    {
        setConfigOverrides(owner, { config::ConfigEntry{ section, key, value } });
    }

    /**
     * Set several session overrides of one owner with one reload of the config, see setConfigOverride.
     * The reload reads no disk, so a value can be set many times a second while it is previewed. For that reason
     * only a key the owner did not override before is logged at the info level and is a devbench event.
     */
    void ConfigBase::setConfigOverrides(const std::string& owner, const std::vector<config::ConfigEntry>& overrides)
    {
        if (overrides.empty()) {
            return;
        }

        // the ones for a key that the owner did not override before this call
        const auto added = _overrides.set(owner, overrides);
        for (const auto& entry : added) {
            logger::info("Config: '{}' set session override \"{}.{} = {}\"", owner, entry.section, entry.key, entry.value);
        }
        if (logger::isDebugEnabled()) {
            for (const auto& entry : overrides) {
                logger::debug("Config: '{}' session override \"{}.{} = {}\"", owner, entry.section, entry.key, entry.value.toString());
            }
        }

        loadConfigValues(config::ConfigFile::Source::Kept);

        for (const auto& entry : added) {
            devbench::emit("config.override", [&] {
                return nlohmann::json{ { "section", entry.section }, { "key", entry.key }, { "value", entry.value }, { "owner", owner } };
            });
        }
    }

    /**
     * Remove the owner's session override for section/key and reload, so the member has another owner's
     * override of it, or its on-disk value with none. Returns whether the owner had one, and nothing is
     * reloaded when it did not.
     */
    bool ConfigBase::clearConfigOverride(const std::string& owner, const char* section, const char* key)
    {
        return clearConfigOverrides(owner, { config::ConfigKey{ section, key } }) > 0;
    }

    /**
     * Remove several session overrides of one owner with one reload, and return how many were removed. A key the
     * owner does not override is skipped, and nothing is reloaded when none was removed.
     * The reload reads the file, so values that were saved to it right before the clear apply at once.
     */
    std::size_t ConfigBase::clearConfigOverrides(const std::string& owner, const std::vector<config::ConfigKey>& keys)
    {
        const auto removed = _overrides.clear(owner, keys);
        if (removed.empty()) {
            return 0;
        }
        for (const auto& entry : removed) {
            logger::info("Config: '{}' cleared session override \"{}.{}\"", owner, entry.section, entry.key);
        }

        loadConfigValues();

        for (const auto& entry : removed) {
            devbench::emit("config.override", [&] {
                return nlohmann::json{ { "section", entry.section }, { "key", entry.key }, { "value", nullptr }, { "owner", owner } };
            });
        }
        return removed.size();
    }

    /**
     * Remove every session override of one owner and reload, so its members have another owner's override
     * or their on-disk value. Returns how many were removed, and nothing is reloaded when none was.
     */
    std::size_t ConfigBase::clearAllConfigOverrides(const std::string& owner)
    {
        const auto removed = _overrides.clearAll(owner);
        if (removed > 0) {
            logger::info("Config: '{}' cleared all its {} session overrides", owner, removed);
            loadConfigValues();
            devbench::emit("config.override", [&] {
                return nlohmann::json{ { "all", true }, { "value", nullptr }, { "owner", owner } };
            });
        }
        return removed;
    }

    /**
     * Whether a session override is currently set for section/key, by any owner.
     */
    bool ConfigBase::hasConfigOverride(const char* section, const char* key) const
    {
        return _overrides.find({ section, key }).has_value();
    }

    /**
     * The session override that applies for every overridden key, with its value in its text form and the owner
     * that set it.
     */
    std::vector<config::AppliedOverride> ConfigBase::getConfigOverrides() const
    {
        return _overrides.applied();
    }

    /**
     * Parse the config as the mod last loaded or saved it into `config`, with no read of the disk, for a read that
     * loads no values, see config::ConfigFile::read.
     */
    bool ConfigBase::loadKeptConfig(toml::table& config) const
    {
        if (!_configFile.read(_configFilePath, config)) {
            logger::warn("Config: failed to read '{}'", _configFilePath);
            return false;
        }
        return true;
    }

    /**
     * Save one value, see saveConfigValues. The value is a bool, an int, a float or a string, which converts to
     * ConfigValue at the call.
     * Each call reads, parses and writes the whole file, about 0.4 ms for a file of a few hundred lines, on the
     * caller's thread, which is the game thread for a save from a menu. Save several values with one
     * saveConfigValues, which costs about the same as one value, and never call this in a loop.
     */
    void ConfigBase::saveConfigValue(const char* section, const char* key, const config::ConfigValue& value)
    {
        saveConfigValues({ config::ConfigEntry{ section, key, value } });
    }

    /**
     * Save several values of one section, see saveConfigValues for the values of any sections.
     */
    void ConfigBase::saveConfigValues(const char* section, const std::initializer_list<std::pair<const char*, config::ConfigValue>> values)
    {
        std::vector<config::ConfigEntry> entries;
        entries.reserve(values.size());
        for (const auto& [key, value] : values) {
            entries.push_back({ section, key, value });
        }
        saveConfigValues(entries);
    }

    /**
     * Save values of any sections in a single read and write of the file, see common::TomlEditor: only the values
     * change in it, and its comments and layout stay, and the file is not written when it already holds them.
     * Every save routes through here, so this is the one place that writes the file. A string value is written in the
     * type the file has for the key, see config::ConfigValue::toTomlLike. A file that is not TOML is not written, so
     * a person in the middle of editing it does not lose their edit.
     */
    void ConfigBase::saveConfigValues(const std::span<const config::ConfigEntry> values)
    {
        F4CF_PERF_FUNCTION();
        const auto text = config::ConfigFile::readFile(_configFilePath);
        if (!text) {
            logger::warn("Config: not saved, failed to read '{}'", _configFilePath);
            return;
        }
        common::TomlEditor editor(*text, fileName());
        if (!editor.ok()) {
            logger::warn("Config: not saved, {} is not valid TOML: {}", fileName(), editor.error());
            return;
        }

        std::vector<common::TomlEditor::Edit> edits;
        for (const auto& [section, key, value] : values) {
            auto tomlValue = value.toTomlLike(editor.table()[section][key].node());
            if (!tomlValue) {
                logger::warn("Config: not saved \"{}.{} = {}\", the file's value for the key is of another type", section, key, value.toString());
                continue;
            }
            logger::info("Config: Saving \"{}.{} = {}\"", section, key, value.toString());
            edits.push_back({ { section, key }, std::move(*tomlValue) });
        }
        if (!editor.set(edits)) {
            logger::warn("Config: not saved: {}", editor.error());
        }
        if (editor.text() != *text && !_configFile.save(_configFilePath, editor.text())) {
            logger::error("Config: failed to save '{}'", _configFilePath);
        }
    }

    /**
     * Read the transform of a section/key from the config as the mod has it (DebugAdjuster field mode seed).
     */
    RE::NiTransform ConfigBase::readTransformValue(const char* section, const char* key, const RE::NiTransform& defaultValue) const
    {
        toml::table config;
        if (!loadKeptConfig(config)) {
            return defaultValue;
        }
        return getTransformValue(config, section, key, defaultValue);
    }

    /**
     * Read the 22-float hand pose of a section/key from the config as the mod has it (DebugAdjuster field mode seed).
     */
    std::array<float, 22> ConfigBase::readHandPoseValue(const char* section, const char* key, const std::array<float, 22>& defaultValue) const
    {
        toml::table config;
        if (!loadKeptConfig(config)) {
            return defaultValue;
        }
        return getHandPoseValue(config, section, key, defaultValue);
    }

    /**
     * Read the float of a section/key from the config as the mod has it (DebugAdjuster field mode seed).
     */
    float ConfigBase::readFloatValue(const char* section, const char* key, const float defaultValue) const
    {
        toml::table config;
        if (!loadKeptConfig(config)) {
            return defaultValue;
        }
        return config[section][key].value_or(defaultValue);
    }

    /**
     * Update a config file of an older version: write the default config of the new version with the user's values
     * for the keys it still has with values of the same type, see config::migrateValues, and then the mod's own
     * migration. So new keys and comments come in, and old keys go. The previous file is kept beside it as a backup,
     * with its version in the name.
     */
    void ConfigBase::updateConfigToLatestVersion(const int currentVersion, const int latestVersion)
    {
        const auto oldText = config::ConfigFile::readFile(_configFilePath);
        std::expected<toml::table, std::string> old = std::unexpected(std::string("failed to read it"));
        if (oldText) {
            old = common::parseToml(*oldText, fileName());
        }
        if (!old) {
            throw std::runtime_error(std::format("Failed to read the config file to update '{}': {}", _configFilePath, old.error()));
        }

        // the default config of the new version
        common::TomlEditor latest(common::getEmbededResourceAsString(_module, _defaultConfigEmbeddedResourceId), fileName());
        if (!latest.ok()) {
            throw std::runtime_error("The embedded default config is not TOML: " + latest.error());
        }

        // update all values in the new config with the old config values but only if they exist in the new
        for (const auto& entry : config::migrateValues(*old, latest)) {
            switch (entry.outcome) {
            case config::MigratedValue::Outcome::Carried:
                logger::info("Migrating {} = {}", entry.pathText(), entry.value);
                break;
            case config::MigratedValue::Outcome::NotCarried:
                logger::warn("Not migrating {} = {}: the new config has it as another type", entry.pathText(), entry.value);
                break;
            default:
                logger::debug("Skipping {} ({})", entry.pathText(), entry.outcome == config::MigratedValue::Outcome::Removed ? "removed" : "unchanged");
                break;
            }
        }

        // set the version to latest
        latest.set({ CONFIG_SECTION_DEBUG, "iVersion" }, latestVersion);

        updateConfigToLatestVersionCustom(currentVersion, latestVersion, *old, latest);

        // backup the old file before overwriting
        const fs::path path(_configFilePath);
        const auto backup = path.parent_path() / std::format("{}_backup_v{}{}", path.stem().string(), currentVersion, path.extension().string());
        std::error_code renameError;
        fs::rename(path, backup, renameError);
        if (renameError) {
            logger::warn("Failed to backup old config file to '{}'. Error: {}", backup.string(), renameError.message());
        }

        // save the new config file
        if (!_configFile.save(_configFilePath, latest.text())) {
            throw std::runtime_error("Failed to save the updated config file: " + _configFilePath);
        }

        logger::info("Config updated successfully");
    }

    /**
     * Watch the config file and reload the config when it changes.
     * One change fires 3-5 events, and the mod's own save fires them too. Every event reads the file, and it is
     * loaded only when it holds something else than the mod last loaded or saved, see config::ConfigFile.
     */
    void ConfigBase::startConfigFileWatch()
    {
        if (_configFileWatch) {
            return;
        }
        // use thread as otherwise there is a deadlock
        std::thread([this]() {
            logger::info("Start file watch in config '{}'", _configFilePath.c_str());
            _configFileWatch = std::make_unique<filewatch::FileWatch<std::string>>(_configFilePath, [this](const std::string&, const filewatch::Event changeType) {
                if (changeType != filewatch::Event::modified) {
                    return;
                }

                waitForConfigFileWriteToEnd();

                if (!loadConfigValues(config::ConfigFile::Source::ChangedFile)) {
                    logger::debug("Config file event: nothing new loaded");
                    return;
                }

                // the values are loaded, and the subscribers are called at the start of the next frame
                devbench::emit("config.reloaded", [&] {
                    return nlohmann::json{ { "file", fileName() }, { "trigger", "file" } };
                });
            });
        }).detach();
    }

    /**
     * Wait until the config file was not written for a short time, so it is not read while a program still
     * writes it. Runs on the file-watch thread.
     */
    void ConfigBase::waitForConfigFileWriteToEnd() const
    {
        constexpr auto delay = std::chrono::milliseconds(200);
        while (true) {
            std::error_code ec;
            const auto sinceWrite = fs::file_time_type::clock::now() - fs::last_write_time(_configFilePath, ec);
            // a write time in the future is a clock that was set back, not a write to wait for
            if (ec || sinceWrite >= delay || sinceWrite < std::chrono::milliseconds(0)) {
                return;
            }
            std::this_thread::sleep_for(delay - sinceWrite);
        }
    }

    /**
     * Stop current file watch.
     */
    void ConfigBase::stopConfigFileWatch()
    {
        logger::info("Stop current file watch in config");
        _configFileWatch.reset();
    }
}
