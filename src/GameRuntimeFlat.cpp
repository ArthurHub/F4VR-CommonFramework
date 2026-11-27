#include "GameRuntime.h"

// Flat Fallout 4's side of GameRuntime: its frame tick's place, and nothing of VR's per frame.
namespace f4cf
{
    /**
     * Flat Fallout 4 1.10.163 and 1.10.984, the runtimes CommonLibF4VR's flat build has.
     */
    bool GameRuntime::supports(const REL::Version& runtime) const
    {
        return runtime == F4SE::RUNTIME_1_10_163 || runtime == F4SE::RUNTIME_1_10_984;
    }

    /**
     * Main::OnIdle's call of DemoteQueuedActors, VR's place in the frame, when it is still the game's call, or another
     * mod's frame hook there already: a call out of the game's code, which calls on what it replaced, as this hook does.
     * Anything else is no site, so a runtime whose code differs is left alone.
     */
    std::uintptr_t GameRuntime::frameCallSite() const
    {
        const auto& site = RE::FlatHookSites::MainUpdateFrameTick;
        if (site.Verify()) {
            return site.address();
        }
        const auto at = site.address();
        if (!at || *reinterpret_cast<const std::uint8_t*>(at) != 0xE8) { // NOLINT(performance-no-int-to-ptr)
            return 0;
        }
        std::int32_t rel = 0;
        std::memcpy(&rel, reinterpret_cast<const void*>(at + 1), sizeof(rel)); // NOLINT(performance-no-int-to-ptr)
        const auto target = at + 5 + rel;
        const auto text = REL::Module::get().segment(REL::Segment::text);
        if (target >= text.address() && target < text.address() + text.size()) {
            return 0;
        }
        logger::info("Main loop hook: another mod's frame hook is at the game's call, so this one calls on to it");
        return at;
    }

    void GameRuntime::onGameLoaded(bool)
    {}

    void GameRuntime::onGameSessionLoaded()
    {}

    void GameRuntime::onFrameStart()
    {}

    void GameRuntime::beforeModUpdate()
    {}

    void GameRuntime::afterModUpdate(ConfigBase&)
    {}

    void GameRuntime::onFrameEnd()
    {}

    void GameRuntime::addModsConfigFunctions(mods_config::api::ModsConfigFO4Client&) const
    {}

    void GameRuntime::checkDebugDump(ConfigBase&) const
    {}
}
