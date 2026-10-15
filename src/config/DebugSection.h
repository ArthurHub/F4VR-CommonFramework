#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

namespace f4cf
{
    constexpr auto CONFIG_SECTION_DEBUG = "Debug";

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
        // Tune an arbitrary config field referenced by debug.adjustField ("Section::Key").
        Field,
    };
}

namespace f4cf::config
{
    /**
     * Runtime debug/tuning state, backed by the [Debug] config section and grouped here so the
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
        // For DebugAdjustTarget::Field: the "Section::Key" of the arbitrary config field being tuned.
        std::string adjustField;
        // f4cf::debug::DebugDraw overlay: the master switch, the names of the channels to skip, a controller
        // binding (vrcf::parseInputBinding grammar) that flips the overlay in the headset, and where the watch
        // table sits in the view (center, or center- with left / right / top / top-left / top-right).
        bool drawEnabled = true;
        std::vector<std::string> drawDisabledChannels;
        std::string drawToggleBinding;
        std::string drawHudPlacement = "center";
        // f4cf::render::sceneDepth: which implementation resolves the world's depth for overlay
        // occlusion (auto / direct / off) - see render::sceneDepth::internal::Strategy. A support
        // switch: "direct" keeps the capture but never resamples, "off" drops occlusion entirely.
        std::string sceneDepthStrategy = "auto";
        // f4cf::render::sceneDepth: add the rows of the overlay-occlusion capture to the debug-draw watch table,
        // and compare the shipped policies against their alternatives. Off by default: the comparison costs a
        // D3D query per committed graphics state.
        bool sceneDepthDiagnostics = false;
        // f4cf::vrui: mark the fingertip that presses the UI with a small sphere. Without it the sphere
        // is shown only while no hand is drawn at the controller, where the controller presses the UI.
        bool vruiShowFingerTip = false;
        // f4cf::vrui: the dev layout, which tunes the placement of the attached UI through a file of its
        // own while the game runs, see vrui::UIDevLayout.
        bool vruiDevLayout = false;
        // One-shot requests, consumed via ConfigBase::checkDebugDumpDataOnceFor() / consumeDebugAddItemsOnce(): the
        // names of the dumps to write, and the inventory requests as written, each a table, see
        // f4vr::DebugInventory::parseRequest. A request is removed (in memory and in the file) once consumed, so the
        // file-watch reload doesn't run it again.
        std::vector<std::string> dumpDataOnce;
        toml::array addItemsOnce;

        void load(const toml::table& config);
    };
}
