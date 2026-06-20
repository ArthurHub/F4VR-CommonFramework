#pragma once

namespace f4cf::common
{
    struct MatrixUtils
    {
        // 3D space related functions
        static float vec3Len(const RE::NiPoint3& v1);
        static RE::NiPoint3 vec3Norm(RE::NiPoint3 v1);
        static float vec3Dot(const RE::NiPoint3& v1, const RE::NiPoint3& v2);
        static RE::NiPoint3 vec3Cross(const RE::NiPoint3& v1, const RE::NiPoint3& v2);
        static float vec3Det(RE::NiPoint3 v1, RE::NiPoint3 v2, RE::NiPoint3 n);
        static float distanceNoSqrt(RE::NiPoint3 po1, RE::NiPoint3 po2);
        static float distanceNoSqrt2d(float x1, float y1, float x2, float y2);
        static float degreesToRads(float deg);
        static float radsToDegrees(float rad);
        static RE::NiPoint3 rotateXY(RE::NiPoint3 vec, float angle);
        static RE::NiPoint3 pitchVec(RE::NiPoint3 vec, float angle);
        static RE::NiTransform calculateRelocation(const RE::NiAVObject* fromNode, const RE::NiAVObject* toNode);
        static RE::NiTransform calculateRelocation(const RE::NiAVObject* fromNode, const RE::NiAVObject* toNode, const RE::NiPoint3& offset, const RE::NiMatrix3& rotationOffset);
        // Local<->world conversion via a node's world transform (the codebase's local->world convention,
        // shared with calculateRelocation: rotation stored transposed, local offsets scaled by world scale).
        // Each pair is an exact inverse, letting a point or whole transform be carried off one node and
        // re-expressed under another (e.g. pinning a node to a world placement regardless of its parent).
        static RE::NiPoint3 localToWorldPoint(const RE::NiTransform& parentWorld, const RE::NiPoint3& localPoint);
        static RE::NiPoint3 worldToLocalPoint(const RE::NiTransform& parentWorld, const RE::NiPoint3& worldPoint);
        static RE::NiTransform localToWorldTransform(const RE::NiTransform& parentWorld, const RE::NiTransform& local);
        static RE::NiTransform worldToLocalTransform(const RE::NiTransform& parentWorld, const RE::NiTransform& world);
        // Re-express a transform local to `fromParentWorld` as one local to `toParentWorld`, keeping its world
        // placement (localToWorld then worldToLocal). includeRotation=false leaves the result rotation identity,
        // for rotation-invariant uses (e.g. a sphere) where carrying the source orientation through is noise.
        static RE::NiTransform reparentTransform(const RE::NiTransform& fromParentWorld, const RE::NiTransform& local, const RE::NiTransform& toParentWorld,
            bool includeRotation = true);

        // matrix
        static RE::NiMatrix3 getIdentityMatrix();
        static RE::NiMatrix3 getMatrix(float r1, float r2, float r3, float r4, float r5, float r6, float r7, float r8, float r9);
        static void getEulerAnglesFromMatrix(const RE::NiMatrix3& matrix, float* heading, float* roll, float* attitude);
        static void getEulerAnglesFromMatrixDegrees(const RE::NiMatrix3& matrix, float* heading, float* roll, float* attitude);
        static RE::NiMatrix3 getMatrixFromEulerAngles(float heading, float roll, float attitude);
        static RE::NiMatrix3 getMatrixFromEulerAnglesDegrees(float heading, float roll, float attitude);
        static RE::NiMatrix3 getMatrixFromRotateVectorVec(const RE::NiPoint3& toVec, const RE::NiPoint3& fromVec);
        static RE::NiMatrix3 getRotationAxisAngle(RE::NiPoint3 axis, float theta);

        // transform
        static RE::NiTransform getTransform(float x, float y, float z, float r1, float r2, float r3, float r4, float r5, float r6, float r7, float r8, float r9, float scale);
        static RE::NiTransform getTransform(float x, float y, float z, float heading, float roll, float attitude, float scale = 1.0f);
        static RE::NiTransform getDeltaTransform(const RE::NiTransform& from, const RE::NiTransform& to);
        static RE::NiTransform getTargetTransform(const RE::NiTransform& baseFrom, const RE::NiTransform& baseTo, const RE::NiTransform& targetFrom);

        // complex
        static bool isCameraLookingAtObject(const RE::NiTransform& cameraTrans, const RE::NiTransform& objectTrans, float detectThresh);
    };
}
