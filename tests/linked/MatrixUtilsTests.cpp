#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>

#include "TestMath.h"
#include "common/MatrixUtils.h"

using f4cf::common::MatrixUtils;
using test_math::requireNear;

namespace
{
    constexpr float PI = std::numbers::pi_v<float>;

    const RE::NiPoint3 X(1, 0, 0);
    const RE::NiPoint3 Y(0, 1, 0);
    const RE::NiPoint3 Z(0, 0, 1);

    /**
     * A transform with a rotation that turns about all three axes, so a wrong order or a missing transpose shows.
     */
    RE::NiTransform transform(const float x, const float y, const float z, const float heading, const float roll, const float attitude, const float scale)
    {
        return MatrixUtils::getTransform(x, y, z, heading, roll, attitude, scale);
    }
}

TEST_CASE("MatrixUtils: the length, dot and cross of vectors")
{
    requireNear(MatrixUtils::vec3Len({ 3, 4, 12 }), 13.0f);
    requireNear(MatrixUtils::vec3Dot({ 1, 2, 3 }, { 4, -5, 6 }), 12.0f);
    requireNear(MatrixUtils::vec3Cross(X, Y), Z);
    requireNear(MatrixUtils::vec3Cross(Y, X), { 0, 0, -1 });

    // the sine of the angle from the first to the second, seen from the tip of the third
    requireNear(MatrixUtils::vec3Det(X, Y, Z), 1.0f);
    requireNear(MatrixUtils::vec3Det(Y, X, Z), -1.0f);

    requireNear(MatrixUtils::distanceNoSqrt({ 1, 2, 3 }, { 4, 6, 3 }), 25.0f);
    requireNear(MatrixUtils::distanceNoSqrt2d(1, 2, 4, 6), 25.0f);
}

TEST_CASE("MatrixUtils: a vector is normalized to length one")
{
    requireNear(MatrixUtils::vec3Norm({ 0, 3, 4 }), { 0, 0.6f, 0.8f });

    SECTION("one with no length gets the axis of its largest part, with its sign")
    {
        requireNear(MatrixUtils::vec3Norm({ 0, 0, 0 }), X);
        requireNear(MatrixUtils::vec3Norm({ 0, 1e-8f, 0 }), Y);
        requireNear(MatrixUtils::vec3Norm({ 1e-9f, 0, -1e-8f }), { 0, 0, -1 });
    }
    SECTION("or is told apart, and the output left as it was")
    {
        RE::NiPoint3 out(7, 8, 9);

        REQUIRE_FALSE(MatrixUtils::tryVec3Norm({ 0, 0, 0 }, out));
        REQUIRE_FALSE(MatrixUtils::tryVec3Norm({ std::numeric_limits<float>::quiet_NaN(), 0, 0 }, out));
        REQUIRE_FALSE(MatrixUtils::tryVec3Norm({ 0.5f, 0, 0 }, out, 1.0f));
        requireNear(out, { 7, 8, 9 });

        REQUIRE(MatrixUtils::tryVec3Norm({ 0, 3, 4 }, out));
        requireNear(out, { 0, 0.6f, 0.8f });
    }
}

TEST_CASE("MatrixUtils: degrees and radians")
{
    requireNear(MatrixUtils::degreesToRads(180), PI);
    requireNear(MatrixUtils::radsToDegrees(PI / 2), 90.0f);
}

TEST_CASE("MatrixUtils: a vector is turned in the ground plane, and pitched off it")
{
    // counter-clockwise seen from above, the height kept
    requireNear(MatrixUtils::rotateXY({ 1, 0, 5 }, PI / 2), { 0, 1, 5 });

    // a positive angle pitches a level vector down, and its heading stays
    requireNear(MatrixUtils::pitchVec(Y, PI / 2), { 0, 0, -1 });
    const auto pitched = MatrixUtils::pitchVec({ 3, 4, 0 }, PI / 6);
    requireNear(MatrixUtils::vec3Len(pitched), 5.0f);
    requireNear(pitched.z, -2.5f);
    requireNear(pitched.x / pitched.y, 0.75f);
}

TEST_CASE("MatrixUtils: a matrix is filled column by column")
{
    const auto matrix = MatrixUtils::getMatrix(1, 2, 3, 4, 5, 6, 7, 8, 9);

    REQUIRE(matrix.entry[0][0] == 1);
    REQUIRE(matrix.entry[1][0] == 2);
    REQUIRE(matrix.entry[2][0] == 3);
    REQUIRE(matrix.entry[0][1] == 4);
    REQUIRE(matrix.entry[2][2] == 9);

    requireNear(MatrixUtils::getIdentityMatrix(), MatrixUtils::getMatrix(1, 0, 0, 0, 1, 0, 0, 0, 1));
}

TEST_CASE("MatrixUtils: a matrix from Euler angles is a rotation, and gives its angles back")
{
    requireNear(MatrixUtils::getMatrixFromEulerAngles(0, 0, 0), MatrixUtils::getIdentityMatrix());

    struct Angles
    {
        float heading, roll, attitude;
    };

    for (const auto [heading, roll, attitude] : { Angles{ 30, 20, 10 }, Angles{ -170, -80, 95 }, Angles{ 0, 89, -45 }, Angles{ 120, 0, 0 } }) {
        CAPTURE(heading, roll, attitude);
        const auto matrix = MatrixUtils::getMatrixFromEulerAnglesDegrees(heading, roll, attitude);

        // a rotation: its transpose undoes it
        requireNear(matrix * matrix.Transpose(), MatrixUtils::getIdentityMatrix());

        float headingBack, rollBack, attitudeBack;
        MatrixUtils::getEulerAnglesFromMatrixDegrees(matrix, &headingBack, &rollBack, &attitudeBack);
        requireNear(headingBack, heading, 0.01f);
        requireNear(rollBack, roll, 0.01f);
        requireNear(attitudeBack, attitude, 0.01f);
    }
}

TEST_CASE("MatrixUtils: Euler angles are the same in radians and in degrees")
{
    const auto matrix = MatrixUtils::getMatrixFromEulerAngles(PI / 6, PI / 9, PI / 18);
    requireNear(matrix, MatrixUtils::getMatrixFromEulerAnglesDegrees(30, 20, 10));

    float heading, roll, attitude;
    MatrixUtils::getEulerAnglesFromMatrix(matrix, &heading, &roll, &attitude);
    requireNear(heading, PI / 6);
    requireNear(roll, PI / 9);
    requireNear(attitude, PI / 18);
}

TEST_CASE("MatrixUtils: angles that a matrix cannot give back still give the same rotation")
{
    struct Angles
    {
        float heading, roll, attitude;
    };

    // a roll of 90 degrees, where the heading and the attitude turn about one axis, and a roll past it
    for (const auto [heading, roll, attitude] : { Angles{ 30, 90, 10 }, Angles{ 30, -90, 10 }, Angles{ 30, 120, 10 } }) {
        CAPTURE(heading, roll, attitude);
        const auto matrix = MatrixUtils::getMatrixFromEulerAnglesDegrees(heading, roll, attitude);

        float headingBack, rollBack, attitudeBack;
        MatrixUtils::getEulerAnglesFromMatrixDegrees(matrix, &headingBack, &rollBack, &attitudeBack);

        // 90 degrees is one float short of a sine of exactly one
        requireNear(MatrixUtils::getMatrixFromEulerAnglesDegrees(headingBack, rollBack, attitudeBack), matrix, 1e-3f);
    }
}

TEST_CASE("MatrixUtils: a point local to a node is placed in the world by the node's world transform")
{
    SECTION("moved and scaled")
    {
        const auto parent = transform(10, 0, 0, 0, 0, 0, 2);

        requireNear(MatrixUtils::localToWorldPoint(parent, { 1, 2, 3 }), { 12, 4, 6 });
        requireNear(MatrixUtils::worldToLocalPoint(parent, { 12, 4, 6 }), { 1, 2, 3 });
    }
    SECTION("turned, and back")
    {
        const auto parent = transform(10, -20, 30, 30, 20, 10, 1.5f);
        const RE::NiPoint3 local(1, 2, 3);

        const auto world = MatrixUtils::localToWorldPoint(parent, local);

        // as far from the node as the point is from its origin, scaled
        requireNear(MatrixUtils::vec3Len(world - parent.translate), MatrixUtils::vec3Len(local) * 1.5f);
        requireNear(MatrixUtils::worldToLocalPoint(parent, world), local);
    }
}

TEST_CASE("MatrixUtils: a transform local to a node is placed in the world, and back")
{
    const auto parent = transform(10, -20, 30, 30, 20, 10, 1.5f);
    const auto local = transform(1, 2, 3, -40, 15, 70, 2);

    const auto world = MatrixUtils::localToWorldTransform(parent, local);

    requireNear(world.translate, MatrixUtils::localToWorldPoint(parent, local.translate));
    requireNear(world.scale, 3.0f);
    requireNear(MatrixUtils::worldToLocalTransform(parent, world), local);

    // a point of the child lands where it does through the child and then the parent
    const RE::NiPoint3 point(4, 5, 6);
    requireNear(MatrixUtils::localToWorldPoint(world, point), MatrixUtils::localToWorldPoint(parent, MatrixUtils::localToWorldPoint(local, point)), 1e-3f);
}

TEST_CASE("MatrixUtils: a transform moved to another parent stays where it is in the world")
{
    const auto fromParent = transform(10, -20, 30, 30, 20, 10, 1.5f);
    const auto toParent = transform(-5, 8, 2, 100, -35, 60, 0.5f);
    const auto local = transform(1, 2, 3, -40, 15, 70, 2);
    const auto world = MatrixUtils::localToWorldTransform(fromParent, local);

    const auto moved = MatrixUtils::reparentTransform(fromParent, local, toParent);

    requireNear(MatrixUtils::localToWorldTransform(toParent, moved), world, 1e-3f);

    SECTION("without its rotation, for a shape that has none to show")
    {
        const auto unturned = MatrixUtils::reparentTransform(fromParent, local, toParent, false);

        requireNear(unturned.rotate, MatrixUtils::getIdentityMatrix());
        requireNear(unturned.translate, moved.translate);
        requireNear(unturned.scale, moved.scale);
    }
}

TEST_CASE("MatrixUtils: a rotation about an axis is kept transposed, as a node's world rotation is")
{
    const auto rotation = MatrixUtils::getRotationAxisAngle(Z, PI / 2);

    // so its transpose is what turns a vector counter-clockwise about the axis
    requireNear(rotation.Transpose() * X, Y);
    requireNear(rotation * X, { 0, -1, 0 });

    // the axis need not have length one
    requireNear(MatrixUtils::getRotationAxisAngle({ 0, 0, 5 }, PI / 2), rotation);
    requireNear(MatrixUtils::getRotationAxisAngle({ 1, 2, 3 }, 0), MatrixUtils::getIdentityMatrix());
}

TEST_CASE("MatrixUtils: the rotation from one vector to another, kept transposed")
{
    SECTION("turns the first onto the second, whatever their lengths")
    {
        const RE::NiPoint3 from(1, 2, 3);
        const RE::NiPoint3 to(-4, 0.5f, 2);

        const auto rotation = MatrixUtils::getMatrixFromRotateVectorVec(to, from);

        requireNear(rotation.Transpose() * MatrixUtils::vec3Norm(from), MatrixUtils::vec3Norm(to));
        requireNear(rotation * rotation.Transpose(), MatrixUtils::getIdentityMatrix());
    }
    SECTION("is no rotation for vectors that point the same way")
    {
        requireNear(MatrixUtils::getMatrixFromRotateVectorVec({ 0, 2, 0 }, Y), MatrixUtils::getIdentityMatrix());
    }
    SECTION("is half a turn for vectors that point opposite ways")
    {
        for (const auto& from : { X, Y, Z, RE::NiPoint3(1, 2, 3) }) {
            const auto rotation = MatrixUtils::getMatrixFromRotateVectorVec(from * -1.0f, from);

            requireNear(rotation.Transpose() * MatrixUtils::vec3Norm(from), MatrixUtils::vec3Norm(from) * -1.0f);
            requireNear(rotation * rotation.Transpose(), MatrixUtils::getIdentityMatrix());
        }
    }
}

TEST_CASE("MatrixUtils: a transform from a position, Euler angles in degrees and a scale")
{
    const auto built = MatrixUtils::getTransform(1, 2, 3, 30, 20, 10, 4);

    requireNear(built.translate, { 1, 2, 3 });
    requireNear(built.rotate, MatrixUtils::getMatrixFromEulerAnglesDegrees(30, 20, 10));
    requireNear(built.scale, 4.0f);
    requireNear(MatrixUtils::getTransform(1, 2, 3, 0, 0, 0).scale, 1.0f);

    // and from the nine numbers of the matrix, column by column
    const auto fromMatrix = MatrixUtils::getTransform(1, 2, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 4);
    requireNear(fromMatrix.rotate, MatrixUtils::getMatrix(1, 2, 3, 4, 5, 6, 7, 8, 9));
    requireNear(fromMatrix.translate, { 1, 2, 3 });
}

TEST_CASE("MatrixUtils: the delta of two transforms is the parent under which the first is the second")
{
    const auto from = transform(1, 2, 3, -40, 15, 70, 2);
    const auto to = transform(10, -20, 30, 30, 20, 10, 3);

    const auto delta = MatrixUtils::getDeltaTransform(from, to);

    requireNear(delta.scale, 1.5f);
    requireNear(MatrixUtils::localToWorldTransform(delta, from), to, 1e-3f);
}

TEST_CASE("MatrixUtils: the change from one transform to another, made on a third")
{
    const auto from = transform(1, 2, 3, -40, 15, 70, 2);
    const auto to = transform(10, -20, 30, 30, 20, 10, 3);

    SECTION("made on the first gives the second")
    {
        requireNear(MatrixUtils::getTargetTransform(from, to, from), to, 1e-3f);
    }
    SECTION("made on another is that one under the delta")
    {
        const auto other = transform(-5, 8, 2, 100, -35, 60, 0.5f);

        requireNear(MatrixUtils::getTargetTransform(from, to, other), MatrixUtils::localToWorldTransform(MatrixUtils::getDeltaTransform(from, to), other), 1e-3f);
    }
}

TEST_CASE("MatrixUtils: a camera looks at an object when both face along the line from the camera to the object")
{
    // forward is +Y
    const auto camera = transform(0, 0, 0, 0, 0, 0, 1);
    const auto ahead = transform(0, 10, 0, 0, 0, 0, 1);

    REQUIRE(MatrixUtils::isCameraLookingAtObject(camera, ahead, 0.9f));

    SECTION("not when the object is off to the side")
    {
        REQUIRE_FALSE(MatrixUtils::isCameraLookingAtObject(camera, transform(10, 10, 0, 0, 0, 0, 1), 0.9f));
    }
    SECTION("not when the camera is turned away")
    {
        REQUIRE_FALSE(MatrixUtils::isCameraLookingAtObject(transform(0, 0, 0, 0, 0, 90, 1), ahead, 0.9f));
    }
    SECTION("not when the object faces back at the camera")
    {
        REQUIRE_FALSE(MatrixUtils::isCameraLookingAtObject(camera, transform(0, 10, 0, 0, 0, 180, 1), 0.9f));
    }
    SECTION("a lower threshold takes a wider angle")
    {
        REQUIRE(MatrixUtils::isCameraLookingAtObject(camera, transform(10, 10, 0, 0, 0, 0, 1), 0.7f));
    }
}
