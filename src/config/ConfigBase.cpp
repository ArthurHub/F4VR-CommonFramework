#include "ConfigBase.h"

#include <nlohmann/json.hpp>

#include "IniMigration.h"
#include "IniValueParsers.h"
#include "devbench/DevBench.h"

namespace f4cf
{
    /**
     * Load the config from the INI file.
     */
    void ConfigBase::load()
    {
        logger::info("Load ini config...");
        common::createDirDeep(_iniFilePath);
        loadIniConfig();
    }

    /**
     * Save the current in-memory config values to the INI file.
     */
    void ConfigBase::save()
    {
        saveIniConfig();
    }

    /**
     * Re-read all values from the on-disk INI file. Skips version migration and watcher setup
     * so it's safe to call repeatedly at runtime (e.g. from the DebugAdjuster reload action).
     */
    void ConfigBase::reload()
    {
        loadIniConfigValues();
        devbench::emit("config.reloaded", [&] {
            return nlohmann::json{ { "file", fs::path(_iniFilePath).filename().string() }, { "trigger", "reload" } };
        });
    }

    /**
     * Load the embedded ini with default values and not the on-disk file.
     */
    void ConfigBase::loadEmbeddedDefaultOnly()
    {
        std::lock_guard lock(_loadMutex);

        CSimpleIniA ini;
        loadEmbeddedIni(ini);

        loadDebugSection(ini);
        loadIniConfigInternal(ini);
        _subscribers.markReloaded();
    }

    /**
     * Parse the default INI that is embedded in the mod's DLL into `ini`. Throws when it cannot be parsed.
     */
    void ConfigBase::loadEmbeddedIni(CSimpleIniA& ini) const
    {
        const SI_Error rc = ini.LoadData(common::getEmbededResourceAsString(_module, _iniDefaultConfigEmbeddedResourceId));
        if (rc < 0) {
            logger::warn("Failed to load the embedded INI config! Error: {}", rc);
            throw std::runtime_error("Failed to load the embedded INI config! Error: " + std::to_string(rc));
        }
    }

    /**
     * Subscribe to be told when the config values were loaded again: a change of the INI on disk, a session
     * override, or reload(). The callback runs on the game thread at the start of the next frame, once for all
     * the loads since the frame before. Call it on the game thread. A key that is already subscribed throws.
     */
    void ConfigBase::subscribeForIniChangedEvent(const std::string& key, const std::function<void(const std::string&)>& callback)
    {
        _subscribers.subscribe(key, callback);
    }

    /**
     * Remove ini change subscription for the given key.
     */
    void ConfigBase::unsubscribeFromIniChangedEvent(const std::string& key)
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
            logger::debug("Notify INI config change subscriber '{}'", key);
        });
    }

    /**
     * Check if a debug data dump is requested for the given name in sDumpDataOnceNames, see config::takeName.
     * The name is then removed from the list, in memory and in the INI, so the dump runs once and a reload does
     * not request it again.
     */
    bool ConfigBase::checkDebugDumpDataOnceFor(const char* name)
    {
        if (!config::takeName(debug.dumpDataOnceNames, name)) {
            return false;
        }
        // write to INI for auto-reload not to re-enable it
        saveIniConfigValue(INI_SECTION_DEBUG, "sDumpDataOnceNames", debug.dumpDataOnceNames.c_str());

        logger::info("---- Debug Dump Data check passed for '{}' ----", name);
        return true;
    }

    /**
     * Consume the one-shot "sAddItemsOnceNames" flag.
     * Returns the current value and clears it (in memory + INI) so the bulk item-add runs exactly once,
     * and is not re-triggered by the file-watcher reloading the same value.
     */
    std::string ConfigBase::consumeDebugAddItemsOnce()
    {
        if (debug.addItemsOnceNames.empty()) {
            return {};
        }
        auto spec = debug.addItemsOnceNames;
        debug.addItemsOnceNames.clear();
        saveIniConfigValue(INI_SECTION_DEBUG, "sAddItemsOnceNames", "");
        return spec;
    }

    /**
     * Load all INI config values.
     * If INI file doesn't exist it will be created from the default embedded resource.
     * If the found INI version is not the latest it will run update to the latest using embedded resource.
     */
    void ConfigBase::loadIniConfig()
    {
        // create .ini if it doesn't exist
        common::createFileFromResourceIfNotExists(_iniFilePath, _module, _iniDefaultConfigEmbeddedResourceId, true);

        loadIniConfigValues();

        const auto iniConfigLatestVersion = loadEmbeddedResourceIniConfigVersion();
        if (_iniConfigVersion < iniConfigLatestVersion) {
            logger::info("Updating INI config version {} -> {}", _iniConfigVersion, iniConfigLatestVersion);
            updateIniConfigToLatestVersion(_iniConfigVersion, iniConfigLatestVersion);

            // reload the config after update
            loadIniConfigValues();
        }

        startIniConfigFileWatch();
    }

    /**
     * Get the latest version of the embedded INI config resource to know if update migration is required.
     */
    int ConfigBase::loadEmbeddedResourceIniConfigVersion() const
    {
        CSimpleIniA ini;
        loadEmbeddedIni(ini);
        return ini.GetLongValue(INI_SECTION_DEBUG, "iVersion", 0);
    }

    /**
     * Read the [Debug] section: the values that are the config's own, and the rest into `debug`.
     */
    void ConfigBase::loadDebugSection(const CSimpleIniA& ini)
    {
        _iniConfigVersion = ini.GetLongValue(INI_SECTION_DEBUG, "iVersion", 0);
        _logLevel = ini.GetLongValue(INI_SECTION_DEBUG, "iLogLevel", 2);
        _logPattern = ini.GetValue(INI_SECTION_DEBUG, "sLogPattern", "%H:%M:%S.%e %L: %v");
        debug.load(ini);
    }

    /**
     * Load all the config values from `source`, see config::IniFile::Source, with the session overrides on top.
     * It runs on the file-watch thread for a change on disk and on the caller's thread for an override, so the
     * whole load is under one lock, the read of the file included: the load that read last is applied last.
     * Returns false from `ChangedFile` when the file holds what the mod last loaded or saved, and loads nothing.
     * Throws when the INI cannot be read.
     */
    bool ConfigBase::loadIniConfigValues(const config::IniFile::Source source)
    {
        std::lock_guard lock(_loadMutex);

        CSimpleIniA ini;
        const auto result = _iniFile.load(_iniFilePath, ini, source);
        if (result == config::IniFile::LoadResult::Unchanged) {
            return false;
        }
        if (result == config::IniFile::LoadResult::Failed) {
            logger::warn("Failed to load INI config file '{}'", _iniFilePath);
            throw std::runtime_error("Failed to load INI config file: " + _iniFilePath);
        }
        if (source == config::IniFile::Source::ChangedFile) {
            logger::info("INI config change detected, reload...");
        }

        _overrides.applyTo(ini);
        applyIniConfig(ini);
        return true;
    }

    /**
     * Apply an already-loaded INI to all in-memory config members (debug section, logger, and
     * the inherited mod values). Shared by loadIniConfigValues and applyIniConfigWithOverride so both
     * the on-disk reload and the in-memory live override run the exact same propagation path.
     */
    void ConfigBase::applyIniConfig(const CSimpleIniA& ini)
    {
        loadDebugSection(ini);

        // set log after loading from config
        logger::setLogLevelAndPattern(_logLevel, _logPattern);

        // let inherited class load all its values
        loadIniConfigInternal(ini);

        // for the subscribers, which are called on the game thread, see notifySubscribersOfReload
        _subscribers.markReloaded();
    }

    /**
     * Re-apply the INI as the mod last loaded or saved it to all config members, with one key overridden in memory
     * only. No disk is read or written, so the DebugAdjuster field mode calls it every frame to preview a field.
     */
    void ConfigBase::applyIniConfigWithOverride(const char* section, const char* key, const char* value)
    {
        std::lock_guard lock(_loadMutex);

        CSimpleIniA ini;
        if (!loadKeptIni(ini)) {
            return;
        }
        // persistent session overrides first, then the caller's transient one-off override on top
        _overrides.applyTo(ini);
        ini.SetValue(section, key, value);
        applyIniConfig(ini);
    }

    /**
     * Get the effective value of a section/key as a string: the session override if one is set, otherwise the
     * value in the INI as the mod last loaded or saved it, otherwise defaultValue. The caller parses it.
     * For a key the INI does not have, defaultValue can differ from the default the mod loads its member with.
     */
    std::string ConfigBase::getConfigValue(const char* section, const char* key, const char* defaultValue) const
    {
        if (const auto value = _overrides.find({ section, key })) {
            return *value;
        }

        CSimpleIniA ini;
        if (!loadKeptIni(ini)) {
            return defaultValue ? defaultValue : std::string{};
        }
        return ini.GetValue(section, key, defaultValue ? defaultValue : "");
    }

    /**
     * Override the value of a section/key in memory for the rest of the session, and reload the config so the
     * members have it. It is applied again on every load and never written to disk. A string value can override
     * a value of any type: the member's getter parses it.
     * `owner` is the name of who sets it. An owner sets and clears only its own overrides, and when two override
     * the same key the one set last applies, see config::Overrides.
     */
    void ConfigBase::setConfigOverride(const std::string& owner, const char* section, const char* key, const config::IniValue& value)
    {
        setConfigOverrides(owner, { config::IniOverride{ section, key, value } });
    }

    /**
     * Set several session overrides of one owner with one reload of the config, see setConfigOverride.
     * The reload reads no disk, so a value can be set many times a second while it is previewed. For that reason
     * only a key the owner did not override before is logged at the info level and is a devbench event.
     */
    void ConfigBase::setConfigOverrides(const std::string& owner, const std::vector<config::IniOverride>& overrides)
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

        loadIniConfigValues(config::IniFile::Source::Kept);

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
        return clearConfigOverrides(owner, { config::IniKey{ section, key } }) > 0;
    }

    /**
     * Remove several session overrides of one owner with one reload, and return how many were removed. A key the
     * owner does not override is skipped, and nothing is reloaded when none was removed.
     * The reload reads the file, so values that were saved to it right before the clear apply at once.
     */
    std::size_t ConfigBase::clearConfigOverrides(const std::string& owner, const std::vector<config::IniKey>& keys)
    {
        const auto removed = _overrides.clear(owner, keys);
        if (removed.empty()) {
            return 0;
        }
        for (const auto& entry : removed) {
            logger::info("Config: '{}' cleared session override \"{}.{}\"", owner, entry.section, entry.key);
        }

        loadIniConfigValues();

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
            loadIniConfigValues();
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
     * The session override that applies for every overridden key, with its value in the INI string form
     * and the owner that set it.
     */
    std::vector<config::AppliedOverride> ConfigBase::getConfigOverrides() const
    {
        return _overrides.applied();
    }

    /**
     * Save the config values into the INI config file.
     * Load the file first to never lose existing values.
     */
    void ConfigBase::saveIniConfig()
    {
        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return;
        }

        // let inherited class save all its values
        saveIniConfigInternal(ini);

        saveIniToFile(ini);
    }

    /**
     * Load the INI file from disk into `ini`, for a save to change it and write it back with saveIniToFile.
     * The file can hold a change by someone else that the mod has not loaded yet, and the save then keeps it.
     */
    bool ConfigBase::loadIniFromFile(CSimpleIniA& ini) const
    {
        const auto rc = ini.LoadFile(_iniFilePath.c_str());
        if (rc < 0) {
            logger::warn("Failed to load INI config file '{}' with code: {}", _iniFilePath, rc);
            return false;
        }
        return true;
    }

    /**
     * Parse the INI as the mod last loaded or saved it into `ini`, with no read of the disk, for a read that
     * loads no values, see config::IniFile::read.
     */
    bool ConfigBase::loadKeptIni(CSimpleIniA& ini) const
    {
        if (!_iniFile.read(_iniFilePath, ini)) {
            logger::warn("Failed to read INI config file '{}'", _iniFilePath);
            return false;
        }
        return true;
    }

    /**
     * Save the INI config to the specified ini config file.
     * The file watch does not load it back: it tells the mod's own save by the content that was written.
     */
    void ConfigBase::saveIniToFile(const CSimpleIniA& ini)
    {
        if (_iniFile.save(_iniFilePath, ini)) {
            logger::info("Config: Saving INI config successful");
        } else {
            logger::error("Config: Failed to save .ini");
        }
    }

    // The single-value overloads are thin wrappers over the batch saveIniConfigValues so there is
    // exactly one file load/save code path. The IniValue conversion happens implicitly at the call.
    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const bool value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const int value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const float value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const char* value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    /**
     * Save one or more key/value pairs in a single file load/save cycle. The single-value
     * saveIniConfigValue overloads all route through here, so this is the one place that does
     * the disk I/O. Values may be of any type IniValue supports (bool/int/float/string).
     */
    void ConfigBase::saveIniConfigValues(const char* section, std::initializer_list<std::pair<const char*, config::IniValue>> values)
    {
        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return;
        }
        for (const auto& [key, value] : values) {
            value.applyTo(ini, section, key);
            logger::info("Config: Saving \"{} = {}\"", key, value.toString());
        }
        if (!_iniFile.save(_iniFilePath, ini)) {
            logger::warn("Failed to save INI config values");
        }
    }

    /**
     * Read the transform of a section/key from the INI as the mod has it (DebugAdjuster field mode seed).
     */
    RE::NiTransform ConfigBase::readIniTransformValue(const char* section, const char* key, const RE::NiTransform& defaultValue) const
    {
        CSimpleIniA ini;
        if (!loadKeptIni(ini)) {
            return defaultValue;
        }
        return getTransformValue(ini, section, key, defaultValue);
    }

    /**
     * Read the 22-float hand pose of a section/key from the INI as the mod has it (DebugAdjuster field mode seed).
     */
    std::array<float, 22> ConfigBase::readIniHandPoseValue(const char* section, const char* key, const std::array<float, 22>& defaultValue) const
    {
        CSimpleIniA ini;
        if (!loadKeptIni(ini)) {
            return defaultValue;
        }
        return getHandPoseValue(ini, section, key, defaultValue);
    }

    /**
     * Read the float of a section/key from the INI as the mod has it (DebugAdjuster field mode seed).
     */
    float ConfigBase::readIniFloatValue(const char* section, const char* key, const float defaultValue) const
    {
        CSimpleIniA ini;
        if (!loadKeptIni(ini)) {
            return defaultValue;
        }
        return static_cast<float>(ini.GetDoubleValue(section, key, defaultValue));
    }

    /**
     * Update an INI file of an older version: write the default INI of the new version with the user's values for
     * the keys it still has, see config::migrateIniValues. So new keys and comments come in, and old keys go.
     * The previous file is kept beside it as a backup, with its version in the name.
     */
    void ConfigBase::updateIniConfigToLatestVersion(const int currentVersion, const int latestVersion) const
    {
        CSimpleIniA oldIni;
        SI_Error rc = oldIni.LoadFile(_iniFilePath.c_str());
        if (rc < 0) {
            throw std::runtime_error("Failed to load old .ini file! Error: " + std::to_string(rc));
        }

        // the default .ini of the new version
        CSimpleIniA newIni;
        loadEmbeddedIni(newIni);

        // update all values in the new ini with the old ini values but only if they exist in the new
        for (const auto& entry : config::migrateIniValues(oldIni, newIni)) {
            if (entry.outcome == config::MigratedValue::Outcome::Carried) {
                logger::info("Migrating {}.{} = {}", entry.section, entry.key, entry.value);
            } else {
                logger::debug("Skipping {}.{} ({})", entry.section, entry.key, entry.outcome == config::MigratedValue::Outcome::Removed ? "removed" : "unchanged");
            }
        }

        // set the version to latest
        newIni.SetLongValue(INI_SECTION_DEBUG, "iVersion", latestVersion);

        updateIniConfigToLatestVersionCustom(currentVersion, latestVersion, oldIni, newIni);

        // backup the old ini file before overwriting
        auto nameStr = std::string(_iniFilePath);
        nameStr = nameStr.replace(nameStr.length() - 4, 4, "_backup_v" + std::to_string(_iniConfigVersion) + ".ini");
        const auto res = std::rename(_iniFilePath.c_str(), nameStr.c_str());
        if (res != 0) {
            logger::warn("Failed to backup old .ini file to '{}'. Error: {}", nameStr.c_str(), res);
        }

        // save the new ini file
        rc = newIni.SaveFile(_iniFilePath.c_str());
        if (rc < 0) {
            throw std::runtime_error("Failed to save post update .ini file! Error: " + std::to_string(rc));
        }

        logger::info(".ini updated successfully");
    }

    /**
     * Watch the INI file and reload the config when it changes.
     * One change fires 3-5 events, and the mod's own save fires them too. Every event reads the file, and it is
     * loaded only when it holds something else than the mod last loaded or saved, see config::IniFile.
     */
    void ConfigBase::startIniConfigFileWatch()
    {
        if (_iniConfigFileWatch) {
            return;
        }
        // use thread as otherwise there is a deadlock
        std::thread([this]() {
            logger::info("Start file watch in INI config '{}'", _iniFilePath.c_str());
            _iniConfigFileWatch = std::make_unique<filewatch::FileWatch<std::string>>(_iniFilePath, [this](const std::string&, const filewatch::Event changeType) {
                if (changeType != filewatch::Event::modified) {
                    return;
                }

                waitForIniFileWriteToEnd();

                if (!loadIniConfigValues(config::IniFile::Source::ChangedFile)) {
                    logger::debug("Ignore INI config file event, the file is as the mod last loaded or saved it");
                    return;
                }

                // the values are loaded, and the subscribers are called at the start of the next frame
                devbench::emit("config.reloaded", [&] {
                    return nlohmann::json{ { "file", fs::path(_iniFilePath).filename().string() }, { "trigger", "file" } };
                });
            });
        }).detach();
    }

    /**
     * Wait until the INI file was not written for a short time, so it is not read while a program still
     * writes it. Runs on the file-watch thread.
     */
    void ConfigBase::waitForIniFileWriteToEnd() const
    {
        constexpr auto delay = std::chrono::milliseconds(200);
        while (true) {
            std::error_code ec;
            const auto sinceWrite = fs::file_time_type::clock::now() - fs::last_write_time(_iniFilePath, ec);
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
    void ConfigBase::stopIniConfigFileWatch()
    {
        logger::info("Stop current file watch in INI config");
        _iniConfigFileWatch.reset();
    }
}
