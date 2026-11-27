#pragma once

namespace mods_config::api
{
    class ModsConfigFO4Client;
}

namespace f4cf
{
    class ConfigBase;

    /**
     * The game the build runs in, and what the framework does differently in it: Fallout 4 VR (GameRuntimeVR.cpp) or
     * flat Fallout 4 (GameRuntimeFlat.cpp), one of them per build (F4CF_RUNTIME). ModBase calls it at fixed points of the
     * plugin's lifecycle and of each frame, so its own code is the same for both.
     */
    class GameRuntime
    {
    public:
        static GameRuntime& get()
        {
            static GameRuntime instance;
            return instance;
        }

        // Internal: ModBase calls these.

        bool supports(const REL::Version& runtime) const;
        std::uintptr_t frameCallSite() const;
        void onGameLoaded(bool preloadRendering);
        void onGameSessionLoaded();
        void onFrameStart();
        void beforeModUpdate();
        void afterModUpdate(ConfigBase& config);
        void onFrameEnd();
        void addModsConfigFunctions(mods_config::api::ModsConfigFO4Client& client) const;
        void checkDebugDump(ConfigBase& config) const;

    private:
        GameRuntime() = default;
    };
}
