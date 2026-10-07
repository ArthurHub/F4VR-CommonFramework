#pragma once

#include <SimpleIni.h>
#include <array>
#include <map>
#include <mutex>
#include <thomasmonkman-filewatch/FileWatch.hpp>
#include <utility>
#include <vector>

#include "Common/CommonUtils.h"
#include "DebugSection.h"
#include "IniFile.h"
#include "IniReaders.h"
#include "IniValue.h"
#include "OffsetsFiles.h"
#include "Overrides.h"
#include "ReloadSubscribers.h"

namespace f4cf
{
    static const auto BASE_PATH = common::getRelativePathInDocuments(R"(\My Games\Fallout4VR\Mods_Config)");

    class ConfigBase : protected config::IniReaders, protected config::OffsetsFiles
    {
    public:
        ConfigBase(const std::string_view& module, const std::string_view& iniFilePath, const WORD iniDefaultConfigEmbeddedResourceId)
            : _module(std::string(module)),
              _iniFilePath(iniFilePath),
              _iniDefaultConfigEmbeddedResourceId(iniDefaultConfigEmbeddedResourceId)
        {}

        virtual ~ConfigBase() = default;

        virtual void load();
        virtual void save();

        void reload();

        /**
         * Where the INI file is on disk.
         */
        const std::string& getIniFilePath() const
        {
            return _iniFilePath;
        }

        void applyIniConfigWithOverride(const char* section, const char* key, const char* value);

        RE::NiTransform readIniTransformValue(const char* section, const char* key, const RE::NiTransform& defaultValue) const;
        std::array<float, 22> readIniHandPoseValue(const char* section, const char* key, const std::array<float, 22>& defaultValue) const;
        float readIniFloatValue(const char* section, const char* key, float defaultValue) const;

        std::string getConfigValue(const char* section, const char* key, const char* defaultValue = "") const;

        void setConfigOverride(const std::string& owner, const char* section, const char* key, const config::IniValue& value);

        void setConfigOverrides(const std::string& owner, const std::vector<config::IniOverride>& overrides);

        bool clearConfigOverride(const std::string& owner, const char* section, const char* key);

        std::size_t clearConfigOverrides(const std::string& owner, const std::vector<config::IniKey>& keys);

        std::size_t clearAllConfigOverrides(const std::string& owner);

        bool hasConfigOverride(const char* section, const char* key) const;

        std::vector<config::AppliedOverride> getConfigOverrides() const;

        void loadEmbeddedDefaultOnly();

        void saveIniConfigValue(const char* section, const char* key, bool value);
        void saveIniConfigValue(const char* section, const char* key, int value);
        void saveIniConfigValue(const char* section, const char* key, float value);
        void saveIniConfigValue(const char* section, const char* key, const char* value);
        void saveIniConfigValues(const char* section, std::initializer_list<std::pair<const char*, config::IniValue>> values);

        void subscribeForIniChangedEvent(const std::string& key, const std::function<void(const std::string&)>& callback);
        void unsubscribeFromIniChangedEvent(const std::string& key);
        void notifySubscribersOfReload();

        bool checkDebugDumpDataOnceFor(const char* name);

        std::string consumeDebugAddItemsOnce();

        // The values of the [Debug] INI section: access as e.g. config->debug.flowFlag1.
        using Debug = config::DebugSection;
        Debug debug;

    protected:
        // Override to load your config values
        virtual void loadIniConfigInternal(const CSimpleIniA& ini) = 0;

        // Override to save your config values
        virtual void saveIniConfigInternal(CSimpleIniA&)
        {}

        void loadIniConfig();
        int loadEmbeddedResourceIniConfigVersion() const;
        void loadDebugSection(const CSimpleIniA& ini);
        bool loadIniConfigValues(config::IniFile::Source source = config::IniFile::Source::File);
        void applyIniConfig(const CSimpleIniA& ini);

        bool loadIniFromFile(CSimpleIniA& ini) const;
        bool loadKeptIni(CSimpleIniA& ini) const;
        void saveIniToFile(const CSimpleIniA& ini);
        void saveIniConfig();

        void updateIniConfigToLatestVersion(int currentVersion, int latestVersion) const;

        /**
         * Custom code to migrate the INI config to the latest version.
         * Can be used is special handling is required for the specific config.
         */
        virtual void updateIniConfigToLatestVersionCustom(int /*currentVersion*/, int /*latestVersion*/, const CSimpleIniA& /*oldIni*/, CSimpleIniA& /*newIni*/) const
        {}

        void startIniConfigFileWatch();

        void stopIniConfigFileWatch();

        // name of the module DLL to read resources from
        std::string _module;

        // location of ini config on disk
        std::string _iniFilePath;

    private:
        // resource id to use for default ini config
        WORD _iniDefaultConfigEmbeddedResourceId;

        // The INI config version to handle updates/migrations
        int _iniConfigVersion = 0;

        // The log level to set for the logger
        int _logLevel = 0;

        // the log message pattern to use for the logger
        std::string _logPattern;

        void loadEmbeddedIni(CSimpleIniA& ini) const;

        void waitForIniFileWriteToEnd() const;

        // filesystem watch for changes to INI config file to have live reload
        std::unique_ptr<filewatch::FileWatch<std::string>> _iniConfigFileWatch;

        // The INI file as the mod last loaded or saved it. By it the file watch tells a change by someone else
        // from the mod's own save, and from one more event of a write that is already loaded.
        config::IniFile _iniFile;

        // The callbacks to call when the config values were loaded again. Every load marks in it that it ran, on
        // the thread it runs on, and notifySubscribersOfReload() calls them on the game thread.
        config::ReloadSubscribers _subscribers;

        // Session-only value overrides, each with the owner that set it, re-applied on every load and never
        // persisted to disk
        config::Overrides _overrides;

        // Held by a load from the read of the INI to the last value set, so two loads never run at once and
        // the load that read the INI last is the one whose values stay. A change on disk loads on the
        // file-watch thread, and an override on the thread of whoever sets it.
        std::mutex _loadMutex;
    };
}
