#include <catch2/catch_test_macros.hpp>

#include <SimpleIni.h>
#include <string>

#include "TestMath.h"
#include "common/MatrixUtils.h"
#include "config/DebugSection.h"

using namespace f4cf;
using config::DebugSection;
using test_math::requireNear;

namespace
{
    /**
     * The section as loaded from an INI with the given lines in its [Debug] section.
     */
    DebugSection loaded(const std::string& lines)
    {
        CSimpleIniA ini;
        REQUIRE(ini.LoadData("[Debug]\n" + lines) >= 0);
        DebugSection section;
        section.load(ini);
        return section;
    }
}

TEST_CASE("DebugSection: a section with no keys is its defaults")
{
    const auto section = loaded("");
    const DebugSection defaults;

    REQUIRE(section.flowFlag1 == 0);
    REQUIRE(section.flowText1.empty());
    requireNear(section.transform, common::MatrixUtils::getTransform(0, 0, 0, 0, 0, 0));
    REQUIRE(section.handPose == defaults.handPose);
    REQUIRE(section.adjustTarget == DebugAdjustTarget::None);
    REQUIRE(section.adjustField.empty());
    REQUIRE(section.drawEnabled);
    REQUIRE(section.drawHudPlacement == "center");
    REQUIRE(section.sceneDepthStrategy == "auto");
    REQUIRE_FALSE(section.sceneDepthDiagnostics);
    REQUIRE_FALSE(section.vruiShowFingerTip);
    REQUIRE_FALSE(section.vruiDevLayout);
    REQUIRE(section.dumpDataOnceNames.empty());
    REQUIRE(section.addItemsOnceNames.empty());
}

TEST_CASE("DebugSection: its keys are read into its fields")
{
    const auto section = loaded(
        "fFlowFlag1 = 1.5\n"
        "fFlowFlag2 = -2\n"
        "fFlowFlag3 = 3\n"
        "sFlowText1 = one\n"
        "sFlowText2 = two words\n"
        "tTransform = 1,2,3; 30,20,10; 2\n"
        "hHandPose = 1,2,3,4; 5,6,7,8; 9,10,11,12; 13,14,15,16; 17,18,19,20; 21,22\n"
        "sDumpDataOnceNames = skelly, perf\n"
        "sAddItemsOnceNames = get weapons\n"
        "bDebugDrawEnabled = false\n"
        "sDebugDrawDisabledChannels = physics\n"
        "sDebugDrawToggleBinding = left longpress a\n"
        "sDebugDrawHudPlacement = top\n"
        "sSceneDepthStrategy = copy\n"
        "bSceneDepthDiagnostics = true\n"
        "bVRUIShowFingerTip = true\n"
        "bVRUIDevLayout = true\n");

    REQUIRE(section.flowFlag1 == 1.5f);
    REQUIRE(section.flowFlag2 == -2.0f);
    REQUIRE(section.flowFlag3 == 3.0f);
    REQUIRE(section.flowText1 == "one");
    REQUIRE(section.flowText2 == "two words");
    requireNear(section.transform, common::MatrixUtils::getTransform(1, 2, 3, 30, 20, 10, 2));
    REQUIRE(section.handPose[0] == 1.0f);
    REQUIRE(section.handPose[21] == 22.0f);
    REQUIRE(section.dumpDataOnceNames == "skelly, perf");
    REQUIRE(section.addItemsOnceNames == "get weapons");
    REQUIRE_FALSE(section.drawEnabled);
    REQUIRE(section.drawDisabledChannels == "physics");
    REQUIRE(section.drawToggleBinding == "left longpress a");
    REQUIRE(section.drawHudPlacement == "top");
    REQUIRE(section.sceneDepthStrategy == "copy");
    REQUIRE(section.sceneDepthDiagnostics);
    REQUIRE(section.vruiShowFingerTip);
    REQUIRE(section.vruiDevLayout);
}

TEST_CASE("DebugSection: the adjust target is one of its words, or a field of the INI by its section and key")
{
    REQUIRE(loaded("sAdjustTarget = transform\n").adjustTarget == DebugAdjustTarget::Transform);
    REQUIRE(loaded("sAdjustTarget = handpose\n").adjustTarget == DebugAdjustTarget::HandPose);
    REQUIRE(loaded("sAdjustTarget = flag1\n").adjustTarget == DebugAdjustTarget::FlowFlag1);
    REQUIRE(loaded("sAdjustTarget = flag2\n").adjustTarget == DebugAdjustTarget::FlowFlag2);
    REQUIRE(loaded("sAdjustTarget = flag3\n").adjustTarget == DebugAdjustTarget::FlowFlag3);
    REQUIRE(loaded("sAdjustTarget = flag123\n").adjustTarget == DebugAdjustTarget::FlowFlag123);
    REQUIRE(loaded("sAdjustTarget = haptictest\n").adjustTarget == DebugAdjustTarget::HapticTest);
    REQUIRE(loaded("sAdjustTarget = none\n").adjustTarget == DebugAdjustTarget::None);
    REQUIRE(loaded("sAdjustTarget = something\n").adjustTarget == DebugAdjustTarget::None);

    const auto field = loaded("sAdjustTarget = Panel::tOffset\n");
    REQUIRE(field.adjustTarget == DebugAdjustTarget::Field);
    REQUIRE(field.adjustField == "Panel::tOffset");

    // a word after a field leaves no field behind
    DebugSection section = field;
    CSimpleIniA ini;
    REQUIRE(ini.LoadData("[Debug]\nsAdjustTarget = flag1\n") >= 0);
    section.load(ini);
    REQUIRE(section.adjustTarget == DebugAdjustTarget::FlowFlag1);
    REQUIRE(section.adjustField.empty());
}
