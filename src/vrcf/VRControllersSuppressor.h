// ReSharper disable CppClangTidyClangDiagnosticUniqueObjectDuplication
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "VRControllersManager.h"

#include "../../external/openvr/openvr.h"

namespace f4cf::vrcf
{
    /**
     * Suppresses VR controller button/axis input from reaching the game.
     *
     * Hooks IVRSystem::GetControllerState (vtable slot 34) and GetControllerStateWithPose
     * (slot 35) on the live IVRSystem object. Because the game, this framework, and all other
     * mods share that single vtable, the hook sees every poll. A per-hand atomic mask of
     * suppressed buttons/axes is cleared from the state every reader sees EXCEPT our own module
     * (the DLL this code is compiled into): the game and any other mod get the filtered state,
     * while our own reads (e.g. VRControllersManager) get raw hardware state via a caller-module
     * check. The net effect: we keep seeing the real press and decide whether to suppress;
     * everyone else is blinded.
     *
     * Owner-keyed: every suppress/release is attributed to a string `key`. A key only ever undoes
     * its own suppression, and the effective game-facing mask is the union of all active owners, so
     * independent subsystems never fight over the same button. Suppression is persistent: it stays
     * set until that key releases it (or reset() wipes everyone, e.g. on a save reload).
     *
     * Threading: the hook runs on the OpenVR polling thread, NOT the main game thread. Only the
     * aggregated per-hand mask is std::atomic -- the hook reads it lock-free and does nothing else.
     * All owner bookkeeping lives in a plain map mutated only by suppress/release, so those MUST be
     * called from the main thread; each change recomputes the union and republishes the atomics.
     * Never call F4SE / CommonLibF4VR from the hook -- decide on the main thread and flip a key.
     *
     * Scope: this owns its own suppression mask and does not arbitrate with foreign hooks -- if
     * another mod also patches the vtable, whoever is outermost wins (and treats the inner hook's
     * reads as foreign, i.e. suppressed).
     */
    class VRControllersSuppressor
    {
    public:
        // Install the IVRSystem vtable hook. Idempotent -- installs once, safe to call repeatedly.
        // vr::VRSystem() must be live; returns false to retry next frame.
        bool initialize();
        bool isInitialized() const
        {
            return _installed.load(std::memory_order_acquire);
        }

        // Driven each frame by the framework: retries install while not yet installed and refreshes
        // the logical-hand -> physical-controller mapping. Mirrors VRControllersManager::update.
        void update(bool isLeftHanded);

        // Persistent, owner-keyed button suppression. release only undoes what `key` suppressed; the
        // effective mask is the union of all owners. Analog-backed buttons (trigger/grip/thumbstick-
        // click) also suppress their backing axis so the game can't re-derive the press from rAxis.
        void suppress(std::string_view key, Hand hand, vr::EVRButtonId button);
        void release(std::string_view key, Hand hand, vr::EVRButtonId button);
        void setSuppressed(std::string_view key, Hand hand, vr::EVRButtonId button, bool suppressed);

        // Aggregate query (suppressed by anyone) and per-owner query.
        bool isSuppressed(Hand hand, vr::EVRButtonId button) const;
        bool isSuppressedBy(std::string_view key, Hand hand, vr::EVRButtonId button) const;

        // Analog axis suppression (zeroes rAxis[axisIndex].x/.y for the game). axisIndex 0..4.
        void suppressAxis(std::string_view key, Hand hand, int axisIndex);
        void releaseAxis(std::string_view key, Hand hand, int axisIndex);
        void setAxisSuppressed(std::string_view key, Hand hand, int axisIndex, bool suppressed);

        // Suppress every analog axis on a hand in one call (all k_unControllerStateAxisCount axes).
        void suppressAllAxes(std::string_view key, Hand hand);
        void releaseAllAxes(std::string_view key, Hand hand);
        void setAllAxesSuppressed(std::string_view key, Hand hand, bool suppressed);

        // Suppress everything (all buttons AND all axes) on one hand / both hands.
        void suppressAll(std::string_view key, Hand hand);
        void suppressAll(std::string_view key);
        void setAllSuppressed(std::string_view key, Hand hand, bool suppressed);
        void setAllSuppressed(std::string_view key, bool suppressed);

        // Drop everything `key` suppressed (all hands, all bits).
        void release(std::string_view key);

        // Hard reset: wipe every owner. For lifecycle reload (clean slate).
        void reset();

    private:
        // Published per-hand aggregate (union of all owners). Read lock-free by the hook on the
        // OpenVR thread; written only by republishAggregate on the main thread.
        struct HandMask
        {
            std::atomic<uint64_t> buttons{ 0 }; // bits cleared from ulButtonPressed/ulButtonTouched
            std::atomic<uint8_t> axes{ 0 }; // bits -> matching rAxis[i] zeroed
        };

        // One owner's contribution for one physical side (non-atomic; main-thread only).
        struct SideMask
        {
            uint64_t buttons = 0;
            uint8_t axes = 0;
            bool operator==(const SideMask&) const = default;
        };

        struct OwnerMask
        {
            SideMask left;
            SideMask right;
            bool operator==(const OwnerMask&) const = default;
        };

        // Transparent hash so the map can be looked up by string_view without allocating a string.
        struct StringHash
        {
            using is_transparent = void;
            std::size_t operator()(const std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }
        };

        // Maps a logical hand to its physical side using the cached left-handed flag.
        // Mirrors VRControllersManager::getHand.
        bool isLeftHand(Hand hand) const;

        // Returns the analog axis index backing an EVRButtonId (Axis0...Axis4 -> 0..4, Grip -> 2), or -1.
        static int backingAxisIndex(vr::EVRButtonId button);

        static bool isOwnerEmpty(const OwnerMask& owner);

        // Finds (or, when mayCreate, inserts) the owner, applies fn to its mask, prunes it if it ends
        // up empty, then republishes the aggregate. mayCreate==false makes a release on an unknown
        // owner a cheap no-op. Main thread only.
        template <class Fn>
        void editOwner(std::string_view key, bool mayCreate, Fn&& fn);

        // Recomputes the union of all owners into the published per-hand atomics.
        void republishAggregate();

        // Logs the effective masks + owner count at debug level. Called only on real changes, so it
        // is at most one line per suppression transition -- never per frame. Main thread only.
        void logState(std::string_view key, const char* action) const;

        // Applies the suppression masks to a foreign (non-self) state read. Called on the OpenVR thread.
        void applyTo(vr::TrackedDeviceIndex_t idx, vr::VRControllerState_t* state, bool foreignCaller) const;

        // Hook trampolines installed into the vtable (OpenVR thread). MSVC x64 passes `this` in RCX,
        // which maps to the leading IVRSystem* -- matching the native virtual's calling convention.
        static bool hookedGetControllerState(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index, vr::VRControllerState_t* state, uint32_t stateSize);
        static bool hookedGetControllerStateWithPose(vr::IVRSystem* system, vr::ETrackingUniverseOrigin origin, vr::TrackedDeviceIndex_t index, vr::VRControllerState_t* state,
            uint32_t stateSize, vr::TrackedDevicePose_t* pose);

        using GetControllerState_t = bool (*)(vr::IVRSystem*, vr::TrackedDeviceIndex_t, vr::VRControllerState_t*, uint32_t);
        using GetControllerStateWithPose_t = bool (*)(vr::IVRSystem*, vr::ETrackingUniverseOrigin, vr::TrackedDeviceIndex_t, vr::VRControllerState_t*, uint32_t,
            vr::TrackedDevicePose_t*);

        static inline GetControllerState_t _origGetControllerState = nullptr;
        static inline GetControllerStateWithPose_t _origGetControllerStateWithPose = nullptr;

        std::unordered_map<std::string, OwnerMask, StringHash, std::equal_to<>> _owners;
        HandMask _left;
        HandMask _right;
        std::atomic<bool> _installed{ false };
        std::atomic<bool> _leftHanded{ false };
        std::atomic<vr::TrackedDeviceIndex_t> _leftIndex{ vr::k_unTrackedDeviceIndexInvalid };
    };

    // Global singleton instance, matching the VRControllers convention.
    inline VRControllersSuppressor VRControllersSuppress; // NOLINT(clang-diagnostic-unique-object-duplication)
}
