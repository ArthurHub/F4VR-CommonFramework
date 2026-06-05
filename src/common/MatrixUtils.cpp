#include "MatrixUtils.h"

#include <numbers>

#include "ModBase.h"

namespace f4cf::common
{
    float MatrixUtils::vec3Len(const RE::NiPoint3& v1)
    {
        return sqrtf(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z);
    }

    RE::NiPoint3 MatrixUtils::vec3Norm(RE::NiPoint3 v1)
    {
        const float mag = vec3Len(v1);

        if (mag < 0.000001) {
            const float maxX = abs(v1.x);
            const float maxY = abs(v1.y);
            const float maxZ = abs(v1.z);

            if (maxX >= maxY && maxX >= maxZ) {
                return v1.x >= 0 ? RE::NiPoint3(1, 0, 0) : RE::NiPoint3(-1, 0, 0);
            }
            if (maxY > maxZ) {
                return v1.y >= 0 ? RE::NiPoint3(0, 1, 0) : RE::NiPoint3(0, -1, 0);
            }
            return v1.z >= 0 ? RE::NiPoint3(0, 0, 1) : RE::NiPoint3(0, 0, -1);
        }

        v1.x /= mag;
        v1.y /= mag;
        v1.z /= mag;

        return v1;
    }

    float MatrixUtils::vec3Dot(const RE::NiPoint3& v1, const RE::NiPoint3& v2)
    {
        return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    }

    RE::NiPoint3 MatrixUtils::vec3Cross(const RE::NiPoint3& v1, const RE::NiPoint3& v2)
    {
        return RE::NiPoint3(v1.y * v2.z - v1.z * v2.y, v1.z * v2.x - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x);
    }

    // the determinant is proportional to the sin of the angle between two vectors.   In 3d case find the sin of the angle between v1 and v2
    // along their angle of rotation with unit vector n
    // https://stackoverflow.com/questions/14066933/direct-way-of-computing-clockwise-angle-between-2-vectors/16544330#16544330
    float MatrixUtils::vec3Det(const RE::NiPoint3 v1, const RE::NiPoint3 v2, const RE::NiPoint3 n)
    {
        return v1.x * v2.y * n.z + v2.x * n.y * v1.z + n.x * v1.y * v2.z - v1.z * v2.y * n.x - v2.z * n.y * v1.x - n.z * v1.y * v2.x;
    }

    float MatrixUtils::distanceNoSqrt(const RE::NiPoint3 po1, const RE::NiPoint3 po2)
    {
        const float x = po1.x - po2.x;
        const float y = po1.y - po2.y;
        const float z = po1.z - po2.z;
        return x * x + y * y + z * z;
    }

    float MatrixUtils::distanceNoSqrt2d(const float x1, const float y1, const float x2, const float y2)
    {
        const float x = x1 - x2;
        const float y = y1 - y2;
        return x * x + y * y;
    }

    float MatrixUtils::degreesToRads(const float deg)
    {
        return deg * std::numbers::pi_v<float> / 180;
    }

    float MatrixUtils::radsToDegrees(const float rad)
    {
        return rad * 180 / std::numbers::pi_v<float>;
    }

    RE::NiPoint3 MatrixUtils::rotateXY(const RE::NiPoint3 vec, const float angle)
    {
        RE::NiPoint3 retV;

        retV.x = vec.x * cosf(angle) - vec.y * sinf(angle);
        retV.y = vec.x * sinf(angle) + vec.y * cosf(angle);
        retV.z = vec.z;

        return retV;
    }

    RE::NiPoint3 MatrixUtils::pitchVec(const RE::NiPoint3 vec, const float angle)
    {
        const auto rotAxis = RE::NiPoint3(vec.y, -vec.x, 0);
        return getRotationAxisAngle(vec3Norm(rotAxis), angle) * vec;
    }

    /**
     * Calculate a relocation transform from one node to another in the world space to be applied to local space of target node.
     * Example usage:
     * lightNode->local = calculateRelocation(lightNode, handNode);
     * This will move the light node to the hand node position and rotation.
     */
    RE::NiTransform MatrixUtils::calculateRelocation(const RE::NiAVObject* fromNode, const RE::NiAVObject* toNode)
    {
        RE::NiTransform out;
        out.scale = fromNode->local.scale;
        out.rotate = toNode->world.rotate * fromNode->parent->world.rotate.Transpose();
        out.translate = fromNode->parent->world.rotate * ((toNode->world.translate - fromNode->parent->world.translate) / fromNode->parent->world.scale);
        return out;
    }

    /**
     * Same as above but with offset and rotation offset applied.
     */
    RE::NiTransform MatrixUtils::calculateRelocation(const RE::NiAVObject* fromNode, const RE::NiAVObject* toNode, const RE::NiPoint3& offset, const RE::NiMatrix3& rotationOffset)
    {
        RE::NiTransform out;
        out.scale = fromNode->local.scale;
        out.rotate = rotationOffset * toNode->world.rotate * fromNode->parent->world.rotate.Transpose();

        // TODO: better understand this calculation and how to make it work for all toNodes
        const auto pos = toNode->parent->world.rotate.Transpose() * ((toNode->local.translate + offset) * toNode->parent->world.scale);
        const auto newToNodePos = toNode->parent->world.translate + pos;
        out.translate = fromNode->parent->world.rotate * ((newToNodePos - fromNode->parent->world.translate) / fromNode->parent->world.scale);

        return out;
    }

    RE::NiMatrix3 MatrixUtils::getIdentityMatrix()
    {
        RE::NiMatrix3 iden;
        iden.MakeIdentity();
        return iden;
    }

    RE::NiMatrix3 MatrixUtils::getMatrix(const float r1, const float r2, const float r3, const float r4, const float r5, const float r6, const float r7, const float r8,
        const float r9)
    {
        RE::NiMatrix3 result;
        result.entry[0][0] = r1;
        result.entry[1][0] = r2;
        result.entry[2][0] = r3;
        result.entry[0][1] = r4;
        result.entry[1][1] = r5;
        result.entry[2][1] = r6;
        result.entry[0][2] = r7;
        result.entry[1][2] = r8;
        result.entry[2][2] = r9;
        return result;
    }

    /**
     * Decompose a rotation matrix into Euler angles (radians). This is the exact inverse of
     * getMatrixFromEulerAngles (heading=X, roll=Y, attitude=Z): for any input angles with
     * roll in [-90, 90], getEulerAnglesFromMatrix(getMatrixFromEulerAngles(h, r, a)) == (h, r, a).
     *
     * The builder stores sin(roll) in entry[0][2], so roll is recovered with asin and the other
     * two angles from the surrounding terms. When roll = +/-90 (cos(roll) == 0) heading and
     * attitude are coupled (gimbal lock); heading is pinned to 0 and the rotation folds into
     * attitude. Outside the canonical roll range the returned triple differs from the original
     * but encodes the same rotation.
     */
    void MatrixUtils::getEulerAnglesFromMatrix(const RE::NiMatrix3& matrix, float* heading, float* roll, float* attitude)
    {
        const float sinRoll = std::fmax(-1.0f, std::fmin(1.0f, matrix.entry[0][2]));
        *roll = asin(sinRoll);
        if (sinRoll > -1.0f && sinRoll < 1.0f) {
            *heading = atan2(-matrix.entry[1][2], matrix.entry[2][2]);
            *attitude = atan2(-matrix.entry[0][1], matrix.entry[0][0]);
        } else {
            // gimbal lock: heading and attitude rotate about the same axis, fold both into attitude
            *heading = 0.0f;
            *attitude = atan2(matrix.entry[1][0], matrix.entry[1][1]);
        }
    }

    /**
     * Degrees-output variant of getEulerAnglesFromMatrix; converts the radian outputs in place.
     */
    void MatrixUtils::getEulerAnglesFromMatrixDegrees(const RE::NiMatrix3& matrix, float* heading, float* roll, float* attitude)
    {
        getEulerAnglesFromMatrix(matrix, heading, roll, attitude);
        *heading = radsToDegrees(*heading);
        *roll = radsToDegrees(*roll);
        *attitude = radsToDegrees(*attitude);
    }

    RE::NiMatrix3 MatrixUtils::getMatrixFromEulerAngles(const float heading, const float roll, const float attitude)
    {
        const float sinX = sin(heading);
        const float cosX = cos(heading);
        const float sinY = sin(roll);
        const float cosY = cos(roll);
        const float sinZ = sin(attitude);
        const float cosZ = cos(attitude);

        RE::NiMatrix3 result;
        result.entry[0][0] = cosY * cosZ;
        result.entry[1][0] = sinX * sinY * cosZ + sinZ * cosX;
        result.entry[2][0] = sinX * sinZ - cosX * sinY * cosZ;
        result.entry[0][1] = -cosY * sinZ;
        result.entry[1][1] = cosX * cosZ - sinX * sinY * sinZ;
        result.entry[2][1] = cosX * sinY * sinZ + sinX * cosZ;
        result.entry[0][2] = sinY;
        result.entry[1][2] = -sinX * cosY;
        result.entry[2][2] = cosX * cosY;
        return result;
    }

    RE::NiMatrix3 MatrixUtils::getMatrixFromEulerAnglesDegrees(const float heading, const float roll, const float attitude)
    {
        return getMatrixFromEulerAngles(degreesToRads(heading), degreesToRads(roll), degreesToRads(attitude));
    }

    RE::NiMatrix3 MatrixUtils::getMatrixFromRotateVectorVec(const RE::NiPoint3& toVec, const RE::NiPoint3& fromVec)
    {
        const auto toVecNorm = vec3Norm(toVec);
        const auto fromVecNorm = vec3Norm(fromVec);

        const float dotP = vec3Dot(fromVecNorm, toVecNorm);

        if (dotP >= 0.99999) {
            return getIdentityMatrix();
        }

        const auto crossP = vec3Norm(vec3Cross(toVecNorm, fromVecNorm));
        const float phi = acosf(dotP);
        const float rCos = cos(phi);
        const float rSin = sin(phi);

        // Build the matrix
        RE::NiMatrix3 result;
        result.entry[0][0] = rCos + crossP.x * crossP.x * (1.0f - rCos);
        result.entry[0][1] = -crossP.z * rSin + crossP.x * crossP.y * (1.0f - rCos);
        result.entry[0][2] = crossP.y * rSin + crossP.x * crossP.z * (1.0f - rCos);
        result.entry[1][0] = crossP.z * rSin + crossP.y * crossP.x * (1.0f - rCos);
        result.entry[1][1] = rCos + crossP.y * crossP.y * (1.0f - rCos);
        result.entry[1][2] = -crossP.x * rSin + crossP.y * crossP.z * (1.0f - rCos);
        result.entry[2][0] = -crossP.y * rSin + crossP.z * crossP.x * (1.0f - rCos);
        result.entry[2][1] = crossP.x * rSin + crossP.z * crossP.y * (1.0f - rCos);
        result.entry[2][2] = rCos + crossP.z * crossP.z * (1.0f - rCos);
        return result;
    }

    // Gets a rotation matrix from an axis and an angle
    RE::NiMatrix3 MatrixUtils::getRotationAxisAngle(RE::NiPoint3 axis, const float theta)
    {
        RE::NiMatrix3 result;
        // This math was found online http://www.euclideanspace.com/maths/geometry/rotations/conversions/angleToMatrix/
        const float c = cosf(theta);
        const float s = sinf(theta);
        const float t = 1.0f - c;
        axis = vec3Norm(axis);
        result.entry[0][0] = c + axis.x * axis.x * t;
        result.entry[1][1] = c + axis.y * axis.y * t;
        result.entry[2][2] = c + axis.z * axis.z * t;
        float tmp1 = axis.x * axis.y * t;
        float tmp2 = axis.z * s;
        result.entry[1][0] = tmp1 + tmp2;
        result.entry[0][1] = tmp1 - tmp2;
        tmp1 = axis.x * axis.z * t;
        tmp2 = axis.y * s;
        result.entry[2][0] = tmp1 - tmp2;
        result.entry[0][2] = tmp1 + tmp2;
        tmp1 = axis.y * axis.z * t;
        tmp2 = axis.x * s;
        result.entry[2][1] = tmp1 + tmp2;
        result.entry[1][2] = tmp1 - tmp2;
        return result.Transpose();
    }

    RE::NiTransform MatrixUtils::getTransform(const float x, const float y, const float z, const float r1, const float r2, const float r3, const float r4, const float r5,
        const float r6, const float r7, const float r8, const float r9, const float scale)
    {
        RE::NiTransform transform;
        transform.translate = RE::NiPoint3(x, y, z);
        transform.rotate = getMatrix(r1, r2, r3, r4, r5, r6, r7, r8, r9);
        transform.scale = scale;
        return transform;
    }

    /**
     * Build an NiTransform from translation + Euler rotation (in degrees) + optional scale.
     * Convenience over the 14-arg overload when callers think in heading/roll/attitude degrees.
     */
    RE::NiTransform MatrixUtils::getTransform(const float x, const float y, const float z, const float heading, const float roll, const float attitude, const float scale)
    {
        RE::NiTransform transform;
        transform.translate = RE::NiPoint3(x, y, z);
        transform.rotate = getMatrixFromEulerAnglesDegrees(heading, roll, attitude);
        transform.scale = scale;
        return transform;
    }

    /**
     * Compute the delta transform between two transforms.
     * i.e. the transform that takes from "from" transform to the "to" transform.
     */
    RE::NiTransform MatrixUtils::getDeltaTransform(const RE::NiTransform& from, const RE::NiTransform& to)
    {
        RE::NiTransform delta;
        delta.scale = to.scale / from.scale;
        delta.rotate = from.rotate.Transpose() * to.rotate;
        delta.translate = to.translate - delta.rotate.Transpose() * (from.translate * delta.scale);
        return delta;
    }

    /**
     * Compute the target transform starting with base using the delta transform from "from" to "to".
     * i.e. made the same change as from->to on base to get target.
     */
    RE::NiTransform MatrixUtils::getTargetTransform(const RE::NiTransform& baseFrom, const RE::NiTransform& baseTo, const RE::NiTransform& targetFrom)
    {
        const auto delta = getDeltaTransform(baseFrom, baseTo);
        RE::NiTransform target;
        target.scale = delta.scale * targetFrom.scale;
        target.rotate = targetFrom.rotate * delta.rotate;
        target.translate = delta.rotate.Transpose() * ((targetFrom.translate * delta.scale) + delta.translate);
        return target;
    }

    /**
     * Check if the camera is looking at the object and the object facing the camera
     */
    bool MatrixUtils::isCameraLookingAtObject(const RE::NiTransform& cameraTrans, const RE::NiTransform& objectTrans, const float detectThresh)
    {
        // Get the position of the camera and the object
        const auto cameraPos = cameraTrans.translate;
        const auto objectPos = objectTrans.translate;

        // Calculate the direction vector from the camera to the object
        const auto direction = vec3Norm(RE::NiPoint3(objectPos.x - cameraPos.x, objectPos.y - cameraPos.y, objectPos.z - cameraPos.z));

        // Get the forward vector of the camera (assuming it's the y-axis)
        const auto cameraForward = vec3Norm(cameraTrans.rotate.Transpose() * RE::NiPoint3(0, 1, 0));

        // Get the forward vector of the object (assuming it's the y-axis)
        const auto objectForward = vec3Norm(objectTrans.rotate.Transpose() * RE::NiPoint3(0, 1, 0));

        // Check if the camera is looking at the object
        const float cameraDot = vec3Dot(cameraForward, direction);
        const bool isCameraLooking = cameraDot > detectThresh; // Adjust the threshold as needed

        // Check if the object is facing the camera
        const float objectDot = vec3Dot(objectForward, direction);
        const bool isObjectFacing = objectDot > detectThresh; // Adjust the threshold as needed

        return isCameraLooking && isObjectFacing;
    }
}
