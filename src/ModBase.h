#pragma once

#include "ConfigBase.h"
#include "DebugAdjuster.h"

namespace f4cf
{
    class ModBase
    {
    public:
        struct Settings
        {
            // the name used for logging and VRUI folder structure
            std::string name;
            // the name reported to F4SE system (generally should be the same as name)
            std::string f4seName;
            // the version reported to F4SE system
            std::string version;
            // the singleton config object used by the mod
            ConfigBase* config;
            // the name of the log file
            std::string logFileName = name;
            // size of memory allocation for trampolines
            int trampolineAllocationSize = 256;
            // if to set up hook to call onFrameUpdate on every game frame
            bool setupMainGameLoop = false;
            // setting game loop late means that this mod will be the first to have its onFrameUpdate called
            // important for mods like FRIK that update the player skeleton
            bool setupMainGameLoopLate = false;

            Settings(const std::string_view& name, const std::string_view& version, ConfigBase* config);
            Settings(const std::string_view& name, const std::string_view& version, ConfigBase* config, int trampolineAllocationSize, bool setupMainGameLoop);
            Settings(const std::string_view& name, const std::string_view& version, ConfigBase* config, const std::string_view& logFileName, int trampolineAllocationSize,
                bool setupMainGameLoop);
            Settings(const std::string_view& name, const std::string_view& f4seName, const std::string_view& version, ConfigBase* config, const std::string_view& logFileName,
                int trampolineAllocationSize, bool setupMainGameLoop, bool setupMainGameLoopLate = false);
        };

        explicit ModBase(Settings settings);
        virtual ~ModBase() = default;

        const std::string& getName() const;
        ConfigBase* getConfig() const;

        bool onF4SEPluginQuery(const F4SE::QueryInterface* skse, F4SE::PluginInfo* info) const;
        bool onF4SEPluginLoad(const F4SE::LoadInterface* f4se);

        void onFrameUpdateSafe();

    private:
        void logPluginGameStart() const;
        static void onF4VRSEMessage(F4SE::MessagingInterface::Message* msg);
        void onGameLoadedInner();
        void onGameSessionLoadedInner();

    protected:
        // Run F4SE plugin load and initialize the plugin given the init handle.
        virtual void onModLoaded(const F4SE::LoadInterface* f4SE) = 0;

        // On game fully loaded initialize things that should be initialized only once.
        virtual void onGameLoaded() = 0;

        // Game session can be initialized multiple times as it is fired on new game and save loaded events.
        virtual void onGameSessionLoaded() = 0;

        // Runs on every game frame, main logic goes here.
        virtual void onFrameUpdate() = 0;

        // Dump game data if requested in "sDebugDumpDataOnceNames" flag in INI config.
        virtual void checkDebugDump() const;

        Settings _settings;
        const F4SE::MessagingInterface* _messaging = nullptr;
        DebugAdjuster _debugAdjuster;
    };

    // The ONE global to rule them ALL
    inline ModBase* g_mod;
}
