#pragma once

#include <array>
#include <map>
#include <mutex>
#include <span>
#include <thomasmonkman-filewatch/FileWatch.hpp>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#include "Common/CommonUtils.h"
#include "ConfigFile.h"
#include "ConfigReaders.h"
#include "ConfigValue.h"
#include "DebugSection.h"
#include "OffsetsFiles.h"
#include "Overrides.h"
#include "ReloadSubscribers.h"
#include "common/TomlEditor.h"

namespace f4cf
{
    static const auto BASE_PATH = common::getRelativePathInDocuments(R"(\My Games\Fallout4VR\Mods_Config)");

    class ConfigBase : protected config::ConfigReaders, protected config::OffsetsFiles
    {
    public:
        ConfigBase(const std::string_view& module, const std::string_view& configFilePath, const WORD defaultConfigEmbeddedResourceId)
            : _module(std::string(module)),
              _configFilePath(configFilePath),
              _defaultConfigEmbeddedResourceId(defaultConfigEmbeddedResourceId)
        {}

        virtual ~ConfigBase() = default;

        virtual void load();

        /**
         * Override to write the values the mod saves all at once, with one saveConfigValues, which writes the file once.
         */
        virtual void save()
        {}

        void reload();

        /**
         * Where the config file is on disk.
         */
        const std::string& getConfigFilePath() const
        {
            return _configFilePath;
        }

        void switchConfigFile(const std::string& configFilePath);

        void applyConfigWithOverride(const char* section, const char* key, const char* value);

        RE::NiTransform readTransformValue(const char* section, const char* key, const RE::NiTransform& defaultValue) const;
        std::array<float, 22> readHandPoseValue(const char* section, const char* key, const std::array<float, 22>& defaultValue) const;
        float readFloatValue(const char* section, const char* key, float defaultValue) const;

        std::string getConfigValue(const char* section, const char* key, const char* defaultValue = "") const;

        bool setConfigOverride(const std::string& owner, const char* section, const char* key, const config::ConfigValue& value);

        bool setConfigOverrides(const std::string& owner, const std::vector<config::ConfigEntry>& overrides);

        bool clearConfigOverride(const std::string& owner, const char* section, const char* key);

        std::size_t clearConfigOverrides(const std::string& owner, const std::vector<config::ConfigKey>& keys);

        std::size_t clearAllConfigOverrides(const std::string& owner);

        bool hasConfigOverride(const char* section, const char* key) const;

        std::vector<config::AppliedOverride> getConfigOverrides() const;

        void loadEmbeddedDefaultOnly();

        // TODO: save, override and read a value in a nested table ([section.sub], an inline table), by a path of keys in
        // place of section + key; common::TomlEditor::set already takes a path.
        void saveConfigValue(const char* section, const char* key, const config::ConfigValue& value);
        void saveConfigValues(const char* section, std::initializer_list<std::pair<const char*, config::ConfigValue>> values);
        void saveConfigValues(std::span<const config::ConfigEntry> values);

        /**
         * Save several values of any sections written in place, such as {{"Main", "fScale", 1.5f}, {"Debug", "iLogLevel", 1}},
         * see saveConfigValues.
         */
        void saveConfigValues(const std::initializer_list<config::ConfigEntry> values)
        {
            saveConfigValues(std::span(values.begin(), values.size()));
        }

        void subscribeForConfigChangedEvent(const std::string& key, const std::function<void(const std::string&)>& callback);
        void unsubscribeFromConfigChangedEvent(const std::string& key);
        void notifySubscribersOfReload();

        bool checkDebugDumpDataOnceFor(const char* name);

        std::string consumeDebugAddItemsOnce();

        // The values of the [Debug] section: access as e.g. config->debug.flowFlag1.
        using Debug = config::DebugSection;
        Debug debug;

    protected:
        // Override to load your config values
        virtual void loadConfigInternal(const toml::table& config) = 0;

        void loadConfig();
        bool loadConfigValues(config::ConfigFile::Source source = config::ConfigFile::Source::File);

        /**
         * Custom code to migrate the config to the latest version, after the generic one: `old` is the user's config
         * of `currentVersion`, and `latest` the default of the new version with the user's values set in it, where
         * values are set. Can be used when a key is renamed or a value changes its type, which the generic one leaves
         * to it, see config::migrateValues. Version 0 is the mod's INI config as TOML, see importIniConfig.
         */
        virtual void updateConfigToLatestVersionCustom(int /*currentVersion*/, int /*latestVersion*/, const toml::table& /*old*/, common::TomlEditor& /*latest*/) const
        {}

        // name of the module DLL to read resources from
        std::string _module;

    private:
        // location of the config file on disk, see switchConfigFile
        std::string _configFilePath;

        // resource id of the default config, embedded in the module
        WORD _defaultConfigEmbeddedResourceId;

        // The config version to handle updates/migrations
        int _configVersion = 0;

        // The log level to set for the logger
        int _logLevel = 0;

        // the log message pattern to use for the logger
        std::string _logPattern;

        // Whether the config's values were loaded once. A file that can't be loaded keeps the values loaded before it,
        // and before any were the embedded default's apply. Under _loadMutex.
        bool _valuesLoaded = false;

        std::string fileName() const;
        toml::table loadEmbeddedDefault() const;
        void loadDebugSection(const toml::table& config);
        void applyConfig(const toml::table& config);
        bool loadKeptConfig(toml::table& config) const;
        void updateConfigToLatestVersion(int currentVersion, int latestVersion);
        void importIniConfig();
        void startConfigFileWatch();
        void stopConfigFileWatch();
        void waitForConfigFileWriteToEnd() const;

        // filesystem watch for changes to the config file to have live reload, made on a thread of its own, see
        // startConfigFileWatch. Under _watchMutex.
        std::unique_ptr<filewatch::FileWatch<std::string>> _configFileWatch;

        // Counts the starts and stops of the watch: a watch made after a stop, or after a later start, is dropped.
        // Under _watchMutex, as whether the watch is started, made yet or not.
        std::uint64_t _watchGeneration = 0;
        bool _watchStarted = false;
        std::mutex _watchMutex;

        // The config file as the mod last loaded or saved it. By it the file watch tells a change by someone else
        // from the mod's own save, and from one more event of a write that is already loaded.
        config::ConfigFile _configFile;

        // The callbacks to call when the config values were loaded again. Every load marks in it that it ran, on
        // the thread it runs on, and notifySubscribersOfReload() calls them on the game thread.
        config::ReloadSubscribers _subscribers;

        // Session-only value overrides, each with the owner that set it, re-applied on every load and never
        // persisted to disk
        config::Overrides _overrides;

        // Held by a load from the read of the file to the last value set, so two loads never run at once and
        // the load that read the file last is the one whose values stay. A change on disk loads on the
        // file-watch thread, and an override on the thread of whoever sets it.
        std::mutex _loadMutex;
    };
}
