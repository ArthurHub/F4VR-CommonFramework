#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "TestMath.h"
#include "common/MatrixUtils.h"
#include "common/Quaternion.h"

using f4cf::common::MatrixUtils;
using f4cf::common::Quaternion;
using test_math::requireNear;

namespace
{
    constexpr float PI = std::numbers::pi_v<float>;

    const RE::NiPoint3 X(1, 0, 0);
    const RE::NiPoint3 Y(0, 1, 0);
    const RE::NiPoint3 Z(0, 0, 1);

    Quaternion aboutAxis(const float angle, const RE::NiPoint3& axis)
    {
        Quaternion quaternion;
        quaternion.setAngleAxis(angle, axis);
        return quaternion;
    }

    void requireNear(const Quaternion& actual, const Quaternion& expected)
    {
        CAPTURE(actual.w, actual.x, actual.y, actual.z, expected.w, expected.x, expected.y, expected.z);
        test_math::requireNear(actual.w, expected.w);
        test_math::requireNear(actual.x, expected.x);
        test_math::requireNear(actual.y, expected.y);
        test_math::requireNear(actual.z, expected.z);
    }
}

TEST_CASE("Quaternion: a new one is no rotation")
{
    const Quaternion identity;

    requireNear(identity, Quaternion(0, 0, 0, 1));
    requireNear(identity.getMatrix(), MatrixUtils::getIdentityMatrix());
    requireNear(identity.getMag(), 1.0f);

    auto turned = aboutAxis(1, X);
    turned.makeIdentity();
    requireNear(turned, identity);
}

TEST_CASE("Quaternion: an angle about an axis turns a vector counter-clockwise about it")
{
    const auto quarterTurn = aboutAxis(PI / 2, Z);

    requireNear(quarterTurn, Quaternion(0, 0, std::sqrt(0.5f), std::sqrt(0.5f)));
    requireNear(quarterTurn.getMatrix() * X, Y);

    // the axis need not have length one
    requireNear(aboutAxis(PI / 2, { 0, 0, 5 }), quarterTurn);

    // the transpose of MatrixUtils' rotation about an axis, which is kept as a node's world rotation is
    requireNear(aboutAxis(0.7f, { 1, 2, 3 }).getMatrix(), MatrixUtils::getRotationAxisAngle({ 1, 2, 3 }, 0.7f).Transpose());
}

TEST_CASE("Quaternion: a rotation matrix gives its quaternion, and the quaternion the matrix")
{
    struct Turn
    {
        float angle;
        RE::NiPoint3 axis;
    };

    for (const auto& [angle, axis] : { Turn{ 0.7f, { 1, 2, 3 } }, Turn{ 2.5f, { -1, 0.5f, 0 } }, Turn{ PI / 2, Z }, Turn{ 3.0f, { 0, 1, 1 } }, Turn{ -1.2f, { 1, 0, 0 } } }) {
        CAPTURE(angle, axis.x, axis.y, axis.z);
        const auto matrix = aboutAxis(angle, axis).getMatrix();

        Quaternion fromMatrix;
        fromMatrix.fromMatrix(matrix);

        requireNear(fromMatrix.getMag(), 1.0f, 1e-3f);

        // to three decimals: a part of the quaternion that is zero is the square root of what the floats left of it
        requireNear(fromMatrix.getMatrix(), matrix, 1e-3f);
    }
}

TEST_CASE("Quaternion: a product is one rotation after the other")
{
    const auto first = aboutAxis(0.7f, { 1, 2, 3 });
    const auto second = aboutAxis(-1.1f, { 0, 1, -1 });

    requireNear((second * first).getMatrix(), second.getMatrix() * first.getMatrix());

    auto product = second;
    product *= first;
    requireNear(product, second * first);

    // the conjugate undoes a rotation
    requireNear(first * first.conjugate(), Quaternion());
}

TEST_CASE("Quaternion: its size, scaled and brought back to one")
{
    const Quaternion quaternion(1, 2, 3, 4);

    requireNear(quaternion.getMag(), std::sqrt(30.0f));
    requireNear(quaternion.dot(Quaternion(4, 3, 2, 1)), 20.0f);
    requireNear(quaternion * 2.0f, Quaternion(2, 4, 6, 8));

    auto scaled = quaternion;
    scaled *= 2.0f;
    requireNear(scaled, Quaternion(2, 4, 6, 8));

    requireNear(quaternion.getNorm().getMag(), 1.0f);
    requireNear(quaternion.getNorm(), quaternion * (1 / std::sqrt(30.0f)));

    scaled.normalize();
    requireNear(scaled, quaternion.getNorm());
}

TEST_CASE("Quaternion: a slerp goes part of the way to another rotation")
{
    const Quaternion start;
    const auto target = aboutAxis(PI / 2, Z);

    SECTION("none of it")
    {
        auto quaternion = start;
        quaternion.slerp(0, target);
        requireNear(quaternion, start);
    }
    SECTION("half of it")
    {
        auto quaternion = start;
        quaternion.slerp(0.5f, target);
        requireNear(quaternion, aboutAxis(PI / 4, Z));
    }
    SECTION("all of it")
    {
        auto quaternion = start;
        quaternion.slerp(1, target);
        requireNear(quaternion, target);
    }
    SECTION("the short way, to a target written with the other sign")
    {
        auto quaternion = start;
        quaternion.slerp(0.5f, target * -1.0f);
        requireNear(quaternion.getMatrix(), aboutAxis(PI / 4, Z).getMatrix());
    }
    SECTION("and stays where it is next to the target")
    {
        auto quaternion = aboutAxis(PI / 2 + 0.001f, Z);
        const auto before = quaternion;
        quaternion.slerp(0.5f, target);
        requireNear(quaternion, before);
    }
}

TEST_CASE("Quaternion: the rotation from one vector of length one to another")
{
    SECTION("turns the first onto the second")
    {
        const auto from = MatrixUtils::vec3Norm({ 1, 2, 3 });
        const auto to = MatrixUtils::vec3Norm({ -4, 0.5f, 2 });

        Quaternion quaternion;
        quaternion.vec2Vec(from, to);

        requireNear(quaternion.getMag(), 1.0f);
        requireNear(quaternion.getMatrix() * from, to);
    }
    SECTION("is no rotation for vectors that point the same way")
    {
        Quaternion quaternion = aboutAxis(1, X);
        quaternion.vec2Vec(Y, Y);

        requireNear(quaternion, Quaternion());
    }
    SECTION("is half a turn for vectors that point opposite ways")
    {
        for (const auto& from : { X, Y, Z }) {
            Quaternion quaternion;
            quaternion.vec2Vec(from, from * -1.0f);

            requireNear(quaternion.getMag(), 1.0f);
            requireNear(quaternion.getMatrix() * from, from * -1.0f);
        }
    }
}
