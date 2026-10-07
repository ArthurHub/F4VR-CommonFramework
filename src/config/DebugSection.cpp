#include "DebugSection.h"

#include "IniReaders.h"
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
}

namespace f4cf::config
{
    /**
     * Read the fields from the [Debug] section of a loaded INI.
     * The section's version, log level and log pattern are not here: they are ConfigBase's own, and it reads them.
     */
    void DebugSection::load(const CSimpleIniA& ini)
    {
        flowFlag1 = static_cast<float>(ini.GetDoubleValue(INI_SECTION_DEBUG, "fFlowFlag1", 0));
        flowFlag2 = static_cast<float>(ini.GetDoubleValue(INI_SECTION_DEBUG, "fFlowFlag2", 0));
        flowFlag3 = static_cast<float>(ini.GetDoubleValue(INI_SECTION_DEBUG, "fFlowFlag3", 0));
        flowText1 = ini.GetValue(INI_SECTION_DEBUG, "sFlowText1", "");
        flowText2 = ini.GetValue(INI_SECTION_DEBUG, "sFlowText2", "");
        transform = IniReaders::getTransformValue(ini, INI_SECTION_DEBUG, "tTransform", common::MatrixUtils::getTransform(0, 0, 0, 0, 0, 0));
        handPose = IniReaders::getHandPoseValue(ini, INI_SECTION_DEBUG, "hHandPose", {});
        // sAdjustTarget is either a fixed keyword (transform/handpose/flag1...) or, when it
        // contains "::", a "Section::Key" reference to any INI field tuned live via the field mode.
        const std::string target = ini.GetValue(INI_SECTION_DEBUG, "sAdjustTarget", "none");
        if (target.find("::") != std::string::npos) {
            adjustTarget = DebugAdjustTarget::Field;
            adjustField = target;
        } else {
            adjustTarget = parseDebugAdjustTarget(target);
            adjustField.clear();
        }
        dumpDataOnceNames = ini.GetValue(INI_SECTION_DEBUG, "sDumpDataOnceNames", "");
        addItemsOnceNames = ini.GetValue(INI_SECTION_DEBUG, "sAddItemsOnceNames", "");
        drawEnabled = ini.GetBoolValue(INI_SECTION_DEBUG, "bDebugDrawEnabled", true);
        drawDisabledChannels = ini.GetValue(INI_SECTION_DEBUG, "sDebugDrawDisabledChannels", "");
        drawToggleBinding = ini.GetValue(INI_SECTION_DEBUG, "sDebugDrawToggleBinding", "");
        drawHudPlacement = ini.GetValue(INI_SECTION_DEBUG, "sDebugDrawHudPlacement", "center");
        sceneDepthStrategy = ini.GetValue(INI_SECTION_DEBUG, "sSceneDepthStrategy", "auto");
        sceneDepthDiagnostics = ini.GetBoolValue(INI_SECTION_DEBUG, "bSceneDepthDiagnostics", false);
        vruiShowFingerTip = ini.GetBoolValue(INI_SECTION_DEBUG, "bVRUIShowFingerTip", false);
        vruiDevLayout = ini.GetBoolValue(INI_SECTION_DEBUG, "bVRUIDevLayout", false);
    }
}
