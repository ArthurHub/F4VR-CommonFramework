#include "DebugSection.h"

#include "ConfigReaders.h"
#include "common/MatrixUtils.h"

namespace
{
    DebugAdjustTarget parseDebugAdjustTarget(const std::string_view value)
    {
        if (value == "transform")
            return DebugAdjustTarget::Transform;
        if (value == "handpose")
            return DebugAdjustTarget::HandPose;
        if (value == "flag1")
            return DebugAdjustTarget::FlowFlag1;
        if (value == "flag2")
            return DebugAdjustTarget::FlowFlag2;
        if (value == "flag3")
            return DebugAdjustTarget::FlowFlag3;
        if (value == "flag123")
            return DebugAdjustTarget::FlowFlag123;
        if (value == "haptictest")
            return DebugAdjustTarget::HapticTest;
        return DebugAdjustTarget::None;
    }

    /**
     * The inventory requests of aAddItemsOnce, as written: an empty array when the key is missing, and with a warning
     * when it is not an array. Each request is read when it runs, see f4vr::DebugInventory::parseRequest.
     */
    toml::array readAddItemsOnce(const toml::table& config)
    {
        const auto* node = config[f4cf::CONFIG_SECTION_DEBUG]["aAddItemsOnce"].node();
        if (!node) {
            return {};
        }
        if (const auto* array = node->as_array()) {
            return *array;
        }
        logger::warn("Config: 'Debug.aAddItemsOnce' is not an array, write it as [{{ op = \"print\", category = \"weapons\" }}]. Ignored.");
        return {};
    }
}

namespace f4cf::config
{
    /**
     * Read the fields from the [Debug] section of a loaded config.
     * The section's version, log level and log pattern are not here: they are ConfigBase's own, and it reads them.
     */
    void DebugSection::load(const toml::table& config)
    {
        const auto debug = config[CONFIG_SECTION_DEBUG];
        flowFlag1 = debug["fFlowFlag1"].value_or(0.0f);
        flowFlag2 = debug["fFlowFlag2"].value_or(0.0f);
        flowFlag3 = debug["fFlowFlag3"].value_or(0.0f);
        flowText1 = debug["sFlowText1"].value_or(std::string());
        flowText2 = debug["sFlowText2"].value_or(std::string());
        transform = ConfigReaders::getTransformValue(config, CONFIG_SECTION_DEBUG, "tTransform", common::MatrixUtils::getTransform(0, 0, 0, 0, 0, 0));
        handPose = ConfigReaders::getHandPoseValue(config, CONFIG_SECTION_DEBUG, "hHandPose", {});
        // sAdjustTarget is either a fixed keyword (transform/handpose/flag1...) or, when it
        // contains "::", a "Section::Key" reference to any config field tuned live via the field mode.
        const std::string target = debug["sAdjustTarget"].value_or(std::string("none"));
        if (target.find("::") != std::string::npos) {
            adjustTarget = DebugAdjustTarget::Field;
            adjustField = target;
        } else {
            adjustTarget = parseDebugAdjustTarget(target);
            adjustField.clear();
        }
        dumpDataOnce = ConfigReaders::getStringListValue(config, CONFIG_SECTION_DEBUG, "aDumpDataOnce", {});
        addItemsOnce = readAddItemsOnce(config);
        drawEnabled = debug["bDebugDrawEnabled"].value_or(true);
        drawDisabledChannels = ConfigReaders::getStringListValue(config, CONFIG_SECTION_DEBUG, "aDebugDrawDisabledChannels", {});
        drawToggleBinding = debug["sDebugDrawToggleBinding"].value_or(std::string());
        drawHudPlacement = debug["sDebugDrawHudPlacement"].value_or(std::string("center"));
        sceneDepthStrategy = debug["sSceneDepthStrategy"].value_or(std::string("auto"));
        sceneDepthDiagnostics = debug["bSceneDepthDiagnostics"].value_or(false);
        vruiShowFingerTip = debug["bVRUIShowFingerTip"].value_or(false);
        vruiDevLayout = debug["bVRUIDevLayout"].value_or(false);
    }
}
