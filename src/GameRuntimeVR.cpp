#include "GameRuntime.h"

#include "DebugAdjuster.h"
#include "config/ConfigBase.h"
#include "debug/DebugDraw.h"
#include "f4vr/DebugDump.h"
#include "f4vr/PlayerNodes.h"
#include "f4vr/WandMovement.h"
#include "modsconfig/ModsConfigFO4Api.h"
#include "render/PrimitiveDrawRenderer.h"
#include "render/SubmitHook.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersManager.h"
#include "vrcf/VRControllersSuppressor.h"
#include "vrui/UIDevLayout.h"
#include "vrui/UIManager.h"

#include <shellapi.h>

// Fallout 4 VR's side of GameRuntime: the controllers, the VR UI and the in-world debug draw each frame.
namespace f4cf
{
    namespace
    {
        /**
         * Opens the VR UI dev layout's file with Windows' default handler for it, on the desktop. The file is
         * there only once the dev layout is on and a VR UI has been shown since, so a press before then says so.
         */
        mods_config::api::FunctionResult openDevLayoutFile()
        {
            const auto path = vrui::UIDevLayout::filePath();
            std::error_code error;
            if (!std::filesystem::exists(path, error)) {
                return { .done = false, .message = "The file is made once the dev layout is on, saved, and a VR UI is shown." };
            }
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) {
                logger::warn("Windows did not open the dev layout file '{}' (error {})", path.string(), result);
                return { .done = false, .message = "Windows did not open the file. The mod's log has its path." };
            }
            return { .done = true, .message = {} };
        }
    }

    /**
     * Fallout 4 VR from 1.2.72 on.
     */
    bool GameRuntime::supports(const REL::Version& runtime) const
    {
        return runtime >= F4SE::RUNTIME_LATEST_VR;
    }

    /**
     * Main::OnIdle's call of a small lock helper, once a frame on the main thread.
     */
    std::uintptr_t GameRuntime::frameCallSite() const
    {
        return RE::VRHookSites::MainUpdateFrameTick.address();
    }

    /**
     * The VR UI's manager, and the overlay's D3D resources when the mod asked for them early: the game's device is up
     * by the main menu.
     */
    void GameRuntime::onGameLoaded(const bool preloadRendering)
    {
        vrui::initUIManager();
        if (preloadRendering) {
            render::PrimitiveDrawRenderer::preload();
        }
    }

    /**
     * The controllers' state, suppressions and haptics start over in a new game or a loaded save.
     */
    void GameRuntime::onGameSessionLoaded()
    {
        vrcf::VRControllers.reset();
        vrcf::VRControllersSuppress.reset();
        vrcf::VRHaptics.reset();
    }

    /**
     * The game thread for the Submit hook's check, the controllers and the wands as of this frame, and the debug
     * draw's frame boundary, which is one atomic read until the mod first draws.
     */
    void GameRuntime::onFrameStart()
    {
        static bool s_gameThreadNoted = false;
        if (!s_gameThreadNoted) {
            s_gameThreadNoted = true;
            render::internal::noteGameThread();
        }

        const bool leftHanded = RE::VRSettings::IsLeftHandedMode();
        vrcf::VRControllers.update(leftHanded);
        vrcf::VRControllersSuppress.update(leftHanded);
        vrcf::VRHaptics.update(leftHanded);
        f4vr::WandMovement::onFrameStart();
        debug::DebugDraw::onFrameStart();
    }

    /**
     * The VR UI's frame update runs once in a frame, and from here it can run again.
     */
    void GameRuntime::beforeModUpdate()
    {
        if (vrui::g_uiManager) {
            vrui::g_uiManager->onFrameStart();
        }
    }

    /**
     * The VR UI's frame update, which does nothing if the mod ran it at a point of its own, so a UI it attached is laid
     * out and drawn in this frame; then the debug adjuster's controller input on the config's debug field.
     */
    void GameRuntime::afterModUpdate(ConfigBase& config)
    {
        if (vrui::g_uiManager) {
            vrui::g_uiManager->onFrameUpdate();
        }
        DebugAdjuster::onFrameUpdate(config);
    }

    void GameRuntime::onFrameEnd()
    {
        debug::DebugDraw::onFrameEnd();
    }

    /**
     * The function `openDevLayoutFile` for a config spec's button.
     */
    void GameRuntime::addModsConfigFunctions(mods_config::api::ModsConfigFO4Client& client) const
    {
        client.addFunction("openDevLayoutFile", [](const mods_config::api::Params&) {
            return openDevLayoutFile();
        });
    }

    /**
     * The VR nodes in the "aDumpDataOnce" names: the Pip-Boy's, and the world from the scope camera up.
     */
    void GameRuntime::checkDebugDump(ConfigBase& config) const
    {
        if (config.checkDebugDumpDataOnceFor("pipboy")) {
            f4vr::DebugDump::printNodes(f4vr::getVRPlayerNodes()->pipboyRootNIFOnlyNode);
        }
        if (config.checkDebugDumpDataOnceFor("world")) {
            f4vr::DebugDump::printNodes(f4vr::getVRPlayerNodes()->primaryWeaponScopeCamera->parent->parent->parent->parent->parent->parent);
        }
    }
}
