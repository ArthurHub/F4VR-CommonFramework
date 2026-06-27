#pragma once

#include <SimpleIni.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <thomasmonkman-filewatch/FileWatch.hpp>
#include <utility>
#include <variant>

#include "Common/CommonUtils.h"

namespace f4cf::vrcf
{
    struct InputBinding;
}

namespace f4cf
{
    static const auto BASE_PATH = common::getRelativePathInDocuments(R"(\My Games\Fallout4VR\Mods_Config)");

    namespace config
    {
        /**
         * A typed INI value for batch writes via ConfigBase::saveIniConfigValues.
         * Constructs implicitly from any supported type, so callers can write {"key", 1.5f}.
         * NiTransform is serialized as "x,y,z;heading,roll,attitude;scale" (rotation in degrees).
         */
        class IniValue
        {
        public:
            IniValue(bool value)
                : _value(value)
            {}

            IniValue(int value)
                : _value(value)
            {}

            IniValue(float value)
                : _value(value)
            {}

            IniValue(const char* value)
                : _value(std::string(value))
            {}

            IniValue(std::string value)
                : _value(std::move(value))
            {}

            void applyTo(CSimpleIniA& ini, const char* section, const char* key) const;

            std::string toString() const;

        private:
            std::variant<bool, int, float, std::string> _value;
        };
    }

    constexpr auto INI_SECTION_DEBUG = "Debug";

    /**
     * Which debug field the in-game DebugAdjuster is bound to. None disables it.
     */
    enum class DebugAdjustTarget : uint8_t
    {
        None = 0,
        Transform,
        HandPose,
        FlowFlag1,
        FlowFlag2,
        FlowFlag3,
        FlowFlag123,
        HapticTest,
        // Tune an arbitrary INI field referenced by debug.adjustField ("Section::Key").
        Field,
    };

    class ConfigBase
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

        void applyIniConfigWithOverride(const char* section, const char* key, const char* value);

        RE::NiTransform readIniTransformValue(const char* section, const char* key, const RE::NiTransform& defaultValue) const;
        std::array<float, 22> readIniHandPoseValue(const char* section, const char* key, const std::array<float, 22>& defaultValue) const;
        float readIniFloatValue(const char* section, const char* key, float defaultValue) const;

        std::string getConfigValue(const char* section, const char* key, const char* defaultValue = "") const;

        void setConfigOverride(const char* section, const char* key, const config::IniValue& value);

        void clearConfigOverride(const char* section, const char* key);

        void clearAllConfigOverrides();

        bool hasConfigOverride(const char* section, const char* key) const;

        void loadEmbeddedDefaultOnly();

        void saveIniConfigValue(const char* section, const char* key, bool value);
        void saveIniConfigValue(const char* section, const char* key, int value);
        void saveIniConfigValue(const char* section, const char* key, float value);
        void saveIniConfigValue(const char* section, const char* key, const char* value);
        void saveIniConfigValues(const char* section, std::initializer_list<std::pair<const char*, config::IniValue>> values);

        void subscribeForIniChangedEvent(const std::string& key, const std::function<void(const std::string&)>& callback);
        void unsubscribeFromIniChangedEvent(const std::string& key);

        bool checkDebugDumpDataOnceFor(const char* name);

        std::string consumeDebugAddItemsOnce();

        /**
         * Runtime debug/tuning state, backed by the [Debug] INI section and grouped here so the
         * fields stay together instead of loose on ConfigBase. Field names drop the "Debug" prefix
         * (the [Debug] section already scopes them): access as e.g. config->debug.flowFlag1.
         */
        struct Debug
        {
            // Can be used to test things at runtime during development, i.e. check "debug.flowFlag1 == 1"
            // somewhere in code and use config reload to change the value at runtime.
            float flowFlag1 = 0;
            float flowFlag2 = 0;
            float flowFlag3 = 0;
            std::string flowText1;
            std::string flowText2;
            RE::NiTransform transform{};
            // General usage debug 22-float pose for tuning at runtime via live-reload.
            // Layout matches FRIK API HandPoseData (5 fingers x {prox,mid,dist,splay}, then palmPitch, palmYaw).
            // Consumers can convert via frik::api::FRIKApi::HandPoseData::fromFloats(debug.handPose).
            std::array<float, 22> handPose{};
            DebugAdjustTarget adjustTarget = DebugAdjustTarget::None;
            // For DebugAdjustTarget::Field: the "Section::Key" of the arbitrary INI field being tuned.
            std::string adjustField;
            // One-shot name lists consumed via checkDebugDumpDataOnceFor() / consumeDebugAddItemsOnce();
            // each is cleared (in memory + INI) once consumed so the file-watch reload doesn't re-trigger it.
            std::string dumpDataOnceNames;
            std::string addItemsOnceNames;
        };

        Debug debug;

        std::map<std::string, std::string> debugVRUIProperties;

    protected:
        // Override to load your config values
        virtual void loadIniConfigInternal(const CSimpleIniA& ini) = 0;

        // Override to save your config values
        virtual void saveIniConfigInternal(CSimpleIniA&)
        {}

        void loadIniConfig();
        int loadEmbeddedResourceIniConfigVersion() const;
        void loadDebugSection(const CSimpleIniA& ini);
        void loadVRUISection(const CSimpleIniA& ini);
        void loadIniConfigValues();
        void applyIniConfig(const CSimpleIniA& ini);

        // Stamp all active session overrides onto the given INI (in-memory only) before it is applied
        // to the typed members. Called on every load path so overrides survive reloads.
        void applyConfigOverrides(CSimpleIniA& ini) const;
        void saveVRUIIniSection(CSimpleIniA& ini);
        bool loadIniFromFile(CSimpleIniA& ini) const;
        void saveIniToFile(const CSimpleIniA& ini);
        void saveIniConfig();

        // Read an NiTransform from "x,y,z;heading,roll,attitude;scale" (rotation in degrees).
        // Returns defaultValue if the key is missing or the value is malformed.
        static RE::NiTransform getTransformValue(const CSimpleIniA& ini, const char* section, const char* key, const RE::NiTransform& defaultValue);

        // Read a 22-float hand pose from "p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;pp,py" — exactly 5
        // ';'-separated groups of 4 ','-separated floats (thumb,index,middle,ring,pinky), then 2 trailing
        // ','-separated floats (palmPitch,palmYaw). Returns defaultValue if the key is missing or the
        // value does not match this exact structure.
        static std::array<float, 22> getHandPoseValue(const CSimpleIniA& ini, const char* section, const char* key, const std::array<float, 22>& defaultValue);

        // Read a controller input binding from a config string, e.g. "offhand longpress grip 0.6 +trigger".
        // See vrcf::parseInputBinding (vrcf/InputBindingParser.h) for the full grammar and aliases.
        // Returns defaultValue (and logs a warning) if the key is missing or the value is malformed.
        static vrcf::InputBinding getInputBindingValue(const CSimpleIniA& ini, const char* section, const char* key, const vrcf::InputBinding& defaultValue);

        void updateIniConfigToLatestVersion(int currentVersion, int latestVersion) const;
        static std::unordered_map<std::string, RE::NiTransform> loadEmbeddedOffsets(WORD fromResourceId, WORD toResourceId);
        static void loadOffsetJsonFile(const std::string& file, std::unordered_map<std::string, RE::NiTransform>& offsetsMap);
        static std::unordered_map<std::string, RE::NiTransform> loadOffsetsFromFilesystem(const std::string& path);
        static bool saveOffsetsToJsonFile(const std::string& name, const RE::NiTransform& transform, const std::string& file);

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

        // filesystem watch for changes to INI config file to have live reload
        std::unique_ptr<filewatch::FileWatch<std::string>> _iniConfigFileWatch;

        // INI config file last write time to prevent reload the same change because of OS multiple events
        std::atomic<std::filesystem::file_time_type> _lastIniFileWriteTime;

        // Callbacks to notify when INI config is changed
        std::unordered_map<std::string, std::function<void(const std::string&)>> _onIniConfigChangedSubscribers;

        // Handle ignoring file watch change event IFF the change was made by us
        std::atomic<bool> _ignoreNextIniFileChange = false;

        // Session-only value overrides keyed by {section, key}, re-applied on every load and never
        // persisted to disk. Guarded by _overridesMutex as it is read from the file-watch thread.
        std::map<std::pair<std::string, std::string>, config::IniValue> _overrides;
        mutable std::mutex _overridesMutex;
    };
}
