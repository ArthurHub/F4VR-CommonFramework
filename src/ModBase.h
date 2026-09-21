#pragma once

#include "ConfigBase.h"
#include "DebugAdjuster.h"

namespace f4cf
{
    /**
     * Register a callback to run at the end of every frame, right after the mod's onFrameUpdate.
     *
     * This exists so an optional subsystem can be pumped without ModBase naming any of its symbols:
     * with static-library semantics, a mod that never calls that subsystem's API never pulls its
     * object files, so it pays neither the code size nor the frame cost. The subsystem registers
     * itself the first time the mod uses it - the ImGui UI layer does so when the first panel is
     * constructed - which is also why callbacks are never removed.
     *
     * Safe to call from inside a callback (a subsystem installing lazily from its own pump); the one
     * registered that way runs in the same frame. GAME thread only, like the callbacks themselves.
     */
    void registerFrameEndCallback(void (*callback)());

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

        // Dump game data if requested in "sDumpDataOnceNames" flag in INI config.
        virtual void checkDebugDump() const;

        // Bulk-add game items to the player inventory if requested in "sAddItemsOnceNames" flag in INI config.
        virtual void checkDebugAddItems() const;

        Settings _settings;
        const F4SE::MessagingInterface* _messaging = nullptr;
        DebugAdjuster _debugAdjuster;
    };

    // The ONE global to rule them ALL
    inline ModBase* g_mod;
}
