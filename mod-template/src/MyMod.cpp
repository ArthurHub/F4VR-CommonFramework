#include "MyMod.h"

#include "devbench/DevBench.h"
#include "f4vr/F4VRUtils.h"
#include "f4vr/PlayerNodes.h"

// F4SE asks for the plugin's name and version
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    return g_mod->onF4SEPluginQuery(a_f4se, a_info);
}

// F4SE for flat Fallout 4 1.10.980 and later reads this in place of the query, before any of the DLL's code runs
extern "C" DLLEXPORT constinit auto F4SEPlugin_Version = f4cf::pluginVersionData(Version::PROJECT, { Version::MAJOR, Version::MINOR, Version::PATCH });

// F4SE loads the plugin: the framework sets up logging and the config, then calls onModLoaded
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    return g_mod->onF4SEPluginLoad(a_f4se);
}

namespace
{
    /**
     * The mod's state in the devbench tool's `state` answer, copied each frame: plain values only, as a snapshot outlives
     * its frame.
     */
    struct MyModState
    {
        bool headInZone = false;
        std::uint32_t headTaps = 0;
        std::uint32_t headHolds = 0;
    };
}

namespace my_mod
{
    /**
     * F4SE load, before the game's data loads: register Papyrus functions and install hooks, and describe the mod to its
     * devbench tool.
     */
    void MyMod::onModLoaded(const F4SE::LoadInterface*)
    {
        // an agent picks the mod's tool by this line, so name the features someone would ask about
        devbench::setToolDescription("MyMod: TODO what the mod does - the head gesture");

        devbench::setStateProvider<MyModState>(
            "head: whether the offhand is in the head gesture's zone (read from the trigger it takes from the game), and its taps and holds",
            [this](MyModState& state) {
                const auto& head = g_config.headActivation;
                state.headInZone = _headSphere.isSuppressing(head.primary) || _headSphere.isSuppressing(head.secondary);
                state.headTaps = _headTaps;
                state.headHolds = _headHolds;
            },
            [](const MyModState& state) -> nlohmann::json {
                return { { "head", { { "inZone", state.headInZone }, { "taps", state.headTaps }, { "holds", state.headHolds } } } };
            });
    }

    /**
     * Once, when the game finishes loading: set up what lives for the whole run, such as the VR UI.
     */
    void MyMod::onGameLoaded()
    {
        //noop
    }

    /**
     * On a new game and on every save load: reset the state of the session before.
     */
    void MyMod::onGameSessionLoaded()
    {
        // the sphere's visual is of the session before; it is attached again on its next show
        _headSphere.detachVisual();
    }

    /**
     * Every frame, from the game's main loop.
     */
    void MyMod::onFrameUpdate()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->loadedData) {
            logger::sample(3000, "no player data - noop");
            return;
        }

        // the head gesture: reach the offhand to the face and tap or hold its trigger
        _headSphere.onFrameUpdate(f4vr::getVRPlayerNodes()->hmdNode, g_config.headActivation, [this](const vrcf::InputBinding& binding) {
            const bool held = binding == g_config.headActivation.secondary;
            ++(held ? _headHolds : _headTaps);
            f4vr::showNotification(held ? "My Mod: head gesture held" : "My Mod: head gesture tapped");
            return true;
        });

        // TODO: add logic
    }
}
