#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <format>
#include <string>

#include "render/SceneDepthShared.h"

using namespace f4cf::render::sceneDepth::internal;

namespace
{
    /**
     * A name no other test, and no other run of the tests, uses: the structs found by name live for the process.
     */
    std::string uniqueName()
    {
        static std::atomic<int> next{ 0 };
        return std::format("Local\\F4CFTests_SceneDepthShared_{}_{}", GetCurrentProcessId(), next++);
    }
}

TEST_CASE("The scene depth copy struct is found by every mod that looks for its name")
{
    const auto name = uniqueName();
    auto* first = findOrCreateSharedSceneDepth(name.c_str());
    REQUIRE(first != nullptr);
    CHECK(first->version == 1);
    CHECK(first->size == sizeof(SharedSceneDepthV1));
    CHECK(first->participants == 1);
    CHECK(currentFrame(*first) == 1);
    CHECK(first->copy == nullptr);
    CHECK(first->copiedFrame == 0);

    auto* second = findOrCreateSharedSceneDepth(name.c_str());
    CHECK(second == first);
    CHECK(first->participants == 2);

    auto* other = findOrCreateSharedSceneDepth(uniqueName().c_str());
    REQUIRE(other != nullptr);
    CHECK(other != first);
    CHECK(other->participants == 1);
}

TEST_CASE("A struct of another version is not shared")
{
    const auto name = uniqueName();
    auto* found = findOrCreateSharedSceneDepth(name.c_str());
    REQUIRE(found != nullptr);
    found->version = 2;
    CHECK(findOrCreateSharedSceneDepth(name.c_str()) == nullptr);
    found->version = 1;
    found->size = sizeof(SharedSceneDepthV1) - 8;
    CHECK(findOrCreateSharedSceneDepth(name.c_str()) == nullptr);
}

TEST_CASE("The frame ends when the outermost Submit hook leaves")
{
    auto* shared = findOrCreateSharedSceneDepth(uniqueName().c_str());
    REQUIRE(shared != nullptr);

    // two mods' hooks, the second called from inside the first
    enterSubmit(*shared);
    enterSubmit(*shared);
    leaveSubmit(*shared);
    CHECK(currentFrame(*shared) == 1);
    leaveSubmit(*shared);
    CHECK(currentFrame(*shared) == 2);

    // one mod's hook alone
    enterSubmit(*shared);
    leaveSubmit(*shared);
    CHECK(currentFrame(*shared) == 3);

    // a leave with no enter still ends a frame, and the next Submit counts from zero again
    leaveSubmit(*shared);
    CHECK(currentFrame(*shared) == 4);
    CHECK(shared->submitDepth == 0);
    enterSubmit(*shared);
    leaveSubmit(*shared);
    CHECK(currentFrame(*shared) == 5);
}

TEST_CASE("A copy no mod has written for STALE_COPY_FRAMES is stale")
{
    CHECK_FALSE(isCopyStale(10, 10));
    CHECK_FALSE(isCopyStale(10 + STALE_COPY_FRAMES, 10));
    CHECK(isCopyStale(10 + STALE_COPY_FRAMES + 1, 10));
    // one never written is stale once that many frames are past the first
    CHECK(isCopyStale(STALE_COPY_FRAMES + 1, 0));
}

TEST_CASE("Releasing the copy clears what was copied")
{
    auto* shared = findOrCreateSharedSceneDepth(uniqueName().c_str());
    REQUIRE(shared != nullptr);
    shared->copyWidth = 640;
    shared->copyHeight = 480;
    shared->copiedFrame = 3;
    shared->copiedViewport.Width = 320.0f;

    const SharedSceneDepthLock lock(*shared);
    releaseSharedCopy(*shared);
    CHECK(shared->copyWidth == 0);
    CHECK(shared->copyHeight == 0);
    CHECK(shared->copiedFrame == 0);
    CHECK_THAT(shared->copiedViewport.Width, Catch::Matchers::WithinAbs(0.0, 1e-6));
}
