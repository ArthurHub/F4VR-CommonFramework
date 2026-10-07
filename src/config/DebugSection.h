#pragma once

#include <SimpleIni.h>
#include <array>
#include <cstdint>
#include <string>

namespace f4cf
{
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
}

namespace f4cf::config
{
    /**
     * Runtime debug/tuning state, backed by the [Debug] INI section and grouped here so the
     * fields stay together instead of loose on ConfigBase. Field names drop the "Debug" prefix
     * (the [Debug] section already scopes them): access as e.g. config->debug.flowFlag1.
     */
    struct DebugSection
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
        // One-shot name lists consumed via ConfigBase::checkDebugDumpDataOnceFor() / consumeDebugAddItemsOnce();
        // each is cleared (in memory + INI) once consumed so the file-watch reload doesn't re-trigger it.
        std::string dumpDataOnceNames;
        std::string addItemsOnceNames;

        void load(const CSimpleIniA& ini);
    };
}
