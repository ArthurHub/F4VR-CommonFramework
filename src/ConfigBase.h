#pragma once

#include <SimpleIni.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <thomasmonkman-filewatch/FileWatch.hpp>
#include <utility>
#include <vector>

#include "Common/CommonUtils.h"
#include "ConfigIniFile.h"
#include "ConfigIniValue.h"
#include "ConfigOverrides.h"

namespace f4cf::vrcf
{
    struct InputBinding;
}

namespace f4cf::f4vr
{
    struct WandActivationConfig;
}

namespace f4cf::render
{
    struct Color;
}

namespace f4cf
{
    static const auto BASE_PATH = common::getRelativePathInDocuments(R"(\My Games\Fallout4VR\Mods_Config)");

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
            // f4cf::debug::DebugDraw overlay: master switch (hot-reloadable; runtime setEnabled /
            // the hotkey below override it until changed again), comma-separated channel names to
            // skip, a controller binding (vrcf::parseInputBinding grammar) to flip the overlay
            // in-headset, and where the default watch-table HUD sits in the view (center /
            // center-left / center-right / center-top / center-top-left / center-top-right). All
            // inert unless the mod actually issues draw calls.
            bool drawEnabled = true;
            std::string drawDisabledChannels;
            std::string drawToggleBinding;
            std::string drawHudPlacement = "center";
            // f4cf::render::sceneDepth: which implementation resolves the world's depth for overlay
            // occlusion (auto / direct / off) - see render::sceneDepth::internal::Strategy. A support
            // switch: "direct" keeps the capture but never resamples, "off" drops occlusion entirely.
            std::string sceneDepthStrategy = "auto";
            // f4cf::render::sceneDepth: investigate the overlay-occlusion capture - add its rows to
            // the debug-draw watch table, and re-run the side-by-side comparison of the shipped
            // policies against their alternatives. Off by default: the comparison has already
            // answered and costs a D3D query per committed graphics state, and the rows belong to
            // whoever is looking at the capture, not to everyone who opens the overlay. The capture
            // reports itself to the log either way.
            bool sceneDepthDiagnostics = false;
            // f4cf::vrui: mark the fingertip that presses the UI with a small sphere. Without it the sphere
            // is shown only while no hand is drawn at the controller, where the controller presses the UI.
            bool vruiShowFingerTip = false;
            // f4cf::vrui: the dev layout, which tunes the placement of the attached UI through a file of its
            // own while the game runs, see vrui::UIDevLayout.
            bool vruiDevLayout = false;
            // One-shot name lists consumed via checkDebugDumpDataOnceFor() / consumeDebugAddItemsOnce();
            // each is cleared (in memory + INI) once consumed so the file-watch reload doesn't re-trigger it.
            std::string dumpDataOnceNames;
            std::string addItemsOnceNames;
        };

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
        void saveIniToFile(const CSimpleIniA& ini);
        void saveIniConfig();

        // special config structs loading
        static RE::NiTransform getTransformValue(const CSimpleIniA& ini, const char* section, const char* key, const RE::NiTransform& defaultValue);
        static std::array<float, 22> getHandPoseValue(const CSimpleIniA& ini, const char* section, const char* key, const std::array<float, 22>& defaultValue);
        static vrcf::InputBinding getInputBindingValue(const CSimpleIniA& ini, const char* section, const char* key, const vrcf::InputBinding& defaultValue);
        static render::Color getColorValue(const CSimpleIniA& ini, const char* section, const char* key, const render::Color& defaultValue);
        static f4vr::WandActivationConfig loadWandActivationConfig(const CSimpleIniA& ini, const char* section, const f4vr::WandActivationConfig& defaults);

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

        void waitForIniFileWriteToEnd() const;

        // filesystem watch for changes to INI config file to have live reload
        std::unique_ptr<filewatch::FileWatch<std::string>> _iniConfigFileWatch;

        // The INI file as the mod last loaded or saved it. By it the file watch tells a change by someone else
        // from the mod's own save, and from one more event of a write that is already loaded.
        config::IniFile _iniFile;

        // Callbacks to notify when the config values were loaded again. Used on the game thread only.
        std::unordered_map<std::string, std::function<void(const std::string&)>> _onIniConfigChangedSubscribers;

        // Set by every load of the values, on the thread it runs on, and taken by notifySubscribersOfReload() on
        // the game thread
        std::atomic<bool> _valuesReloaded = false;

        // Session-only value overrides, each with the owner that set it, re-applied on every load and never
        // persisted to disk
        config::Overrides _overrides;

        // Held by a load from the read of the INI to the last value set, so two loads never run at once and
        // the load that read the INI last is the one whose values stay. A change on disk loads on the
        // file-watch thread, and an override on the thread of whoever sets it.
        std::mutex _loadMutex;
    };
}
