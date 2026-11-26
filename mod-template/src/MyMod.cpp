#include "MyMod.h"

#include "f4vr/F4VRUtils.h"
#include "f4vr/PlayerNodes.h"

// F4SE asks for the plugin's name and version
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    return g_mod->onF4SEPluginQuery(a_f4se, a_info);
}

// F4SE loads the plugin: the framework sets up logging and the config, then calls onModLoaded
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    return g_mod->onF4SEPluginLoad(a_f4se);
}

namespace my_mod
{
    /**
     * F4SE load, before the game's data loads: register Papyrus functions and install hooks.
     */
    void MyMod::onModLoaded(const F4SE::LoadInterface*)
    {
        //noop
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
        _headSphere.onFrameUpdate(f4vr::getVRPlayerNodes()->hmdNode, g_config.headActivation, [](const vrcf::InputBinding& binding) {
            const bool held = binding == g_config.headActivation.secondary;
            f4vr::showNotification(held ? "My Mod: head gesture held" : "My Mod: head gesture tapped");
            return true;
        });

        // TODO: add logic
    }
}
