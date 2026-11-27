#pragma once

namespace f4cf::f4vr
{
    struct DebugDump
    {
        static void printMatrix(const RE::NiMatrix3* mat);
#ifdef ENABLE_FALLOUT_VR
        static void positionDiff();
#endif
        static void printAllNodes();
        static void printNodes(RE::NiAVObject* node, bool printAncestors = true);
        static void printNodesTransform(RE::NiAVObject* node, bool printAncestors = true);
        static void printTransform(const std::string& name, const RE::NiTransform& transform, bool sample = false);
        static void printPosition(const std::string& name, const RE::NiPoint3& pos, bool sample);
        static void dumpPlayerGeometry();
        static void debug();
        static void printScaleFormElements(RE::Scaleform::GFx::Value* elm, const std::string& padding = "");
    };
}
