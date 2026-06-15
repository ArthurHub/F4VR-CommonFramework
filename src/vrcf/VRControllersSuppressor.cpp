// ReSharper disable CppClangTidyBugproneMultiLevelImplicitPointerConversion
#include "VRControllersSuppressor.h"

#include <Windows.h>
#include <ranges>

#include <utility>

namespace f4cf::vrcf
{
    namespace
    {
        // Per-thread "this is our own read" depth. Non-zero while a SelfControllerReadScope is alive on
        // the calling thread; the hook then returns raw state instead of suppressing. A counter (not a
        // bool) so nested scopes compose. Lives on the reading thread, never touched by the OpenVR
        // polling thread that serves the game's own reads.
        thread_local int t_selfReadDepth = 0;

        // True when the current thread is inside one or more SelfControllerReadScope guards.
        bool isSelfControllerRead()
        {
            return t_selfReadDepth > 0;
        }
    }

    SelfControllerReadScope::SelfControllerReadScope()
    {
        ++t_selfReadDepth;
    }

    SelfControllerReadScope::~SelfControllerReadScope()
    {
        --t_selfReadDepth;
    }

    namespace
    {
        // IVRSystem vtable indices for the controller-state polls. Verified against ROCK
        // InputRemapRuntime.cpp:30-31 (gold) and F4VRControlsConfig RE (silver); consistent across
        // the OpenVR build shipped with FO4VR. These are interface-version constants, not module
        // offsets, so no AddressLib ID / VR-vs-NG split is needed.
        constexpr int VTABLE_INDEX_GET_CONTROLLER_STATE = 34;
        constexpr int VTABLE_INDEX_GET_CONTROLLER_STATE_WITH_POSE = 35;

        // Bit mask of all analog axes (bits 0..k_unControllerStateAxisCount-1).
        constexpr uint8_t ALL_AXES_MASK = (1u << vr::k_unControllerStateAxisCount) - 1u;

        /**
         * Overwrites one pointer-sized vtable slot, saving the previous entry. Idempotent: if the
         * slot already holds the hook, it leaves it and reports success.
         */
        bool patchVtableSlot(void** slot, void* hook, void*& outOriginal)
        {
            if (!slot) {
                return false;
            }
            if (*slot == hook) {
                return outOriginal != nullptr; // already installed
            }
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                return false;
            }
            outOriginal = *slot;
            *slot = hook;
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
            return true;
        }
    }

    /**
     * Patches IVRSystem vtable slots 34/35 once. vr::VRSystem() must be live; returns false (retry
     * next frame) until it is and both slots are patched.
     */
    bool VRControllersSuppressor::initialize()
    {
        if (_installed.load(std::memory_order_acquire)) {
            return true;
        }

        auto* system = vr::VRSystem();
        if (!system) {
            return false;
        }

        // The vtable pointer sits at offset 0 of the COM-style interface object.
        void** vtable = *reinterpret_cast<void***>(system);

        void* origState = reinterpret_cast<void*>(_origGetControllerState);
        void* origStateWithPose = reinterpret_cast<void*>(_origGetControllerStateWithPose);

        const bool ok1 = patchVtableSlot(&vtable[VTABLE_INDEX_GET_CONTROLLER_STATE], reinterpret_cast<void*>(&hookedGetControllerState), origState);
        const bool ok2 = patchVtableSlot(&vtable[VTABLE_INDEX_GET_CONTROLLER_STATE_WITH_POSE], reinterpret_cast<void*>(&hookedGetControllerStateWithPose), origStateWithPose);

        if (!ok1 || !ok2) {
            logger::warn("VRControllersSuppressor: failed to patch IVRSystem vtable, will retry");
            return false;
        }

        _origGetControllerState = reinterpret_cast<GetControllerState_t>(origState);
        _origGetControllerStateWithPose = reinterpret_cast<GetControllerStateWithPose_t>(origStateWithPose);

        _installed.store(true, std::memory_order_release);
        logger::info("VRControllersSuppressor: IVRSystem vtable hook installed (slots 34/35)");
        return true;
    }

    /**
     * Per-frame driver: keeps retrying the installation until it succeeds and refreshes the cached
     * left-handed flag and physical left-controller index used to resolve logical hands.
     */
    void VRControllersSuppressor::update(const bool isLeftHanded)
    {
        _leftHanded.store(isLeftHanded, std::memory_order_relaxed);

        auto* system = vr::VRSystem();
        if (!system) {
            return;
        }
        _leftIndex.store(system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand), std::memory_order_relaxed);

        if (!_installed.load(std::memory_order_acquire)) {
            initialize();
        }
    }

    /**
     * True when an owner suppresses nothing on either hand, so it can be pruned from the map.
     */
    bool VRControllersSuppressor::isOwnerEmpty(const OwnerMask& owner)
    {
        return owner.left.buttons == 0 && owner.left.axes == 0 && owner.right.buttons == 0 && owner.right.axes == 0;
    }

    /**
     * Finds (or, when mayCreate, inserts) the owner, applies fn to its mask, prunes it if it ends up
     * empty, then republishes the aggregate -- but only when fn actually changed the mask, so an
     * idempotent every-frame suppress costs just a map lookup. A release on an unknown owner is a
     * no-op. Main thread only.
     */
    template <class Fn>
    void VRControllersSuppressor::editOwner(const std::string_view key, const bool mayCreate, Fn&& fn)
    {
        auto it = _owners.find(key);
        if (it == _owners.end()) {
            if (!mayCreate) {
                return; // nothing to clear for an unknown owner
            }
            it = _owners.emplace(std::string(key), OwnerMask{}).first;
        }
        const OwnerMask before = it->second;
        fn(it->second);
        const bool changed = it->second != before;
        if (isOwnerEmpty(it->second)) {
            _owners.erase(it);
        }
        if (changed) {
            republishAggregate();
            logState(key, "updated");
        }
    }

    /**
     * Recomputes the union of every owner's masks and publishes it into the per-hand atomics the
     * hook reads.
     */
    void VRControllersSuppressor::republishAggregate()
    {
        uint64_t leftButtons = 0, rightButtons = 0;
        uint8_t leftAxes = 0, rightAxes = 0;
        for (const auto& owner : _owners | std::views::values) {
            leftButtons |= owner.left.buttons;
            leftAxes |= owner.left.axes;
            rightButtons |= owner.right.buttons;
            rightAxes |= owner.right.axes;
        }
        _left.buttons.store(leftButtons, std::memory_order_release);
        _left.axes.store(leftAxes, std::memory_order_release);
        _right.buttons.store(rightButtons, std::memory_order_release);
        _right.axes.store(rightAxes, std::memory_order_release);
    }

    /**
     * Logs the effective per-hand masks and owner count at debug level. Called only on real changes,
     * so it is at most one line per suppression transition -- never per frame.
     */
    void VRControllersSuppressor::logState(const std::string_view key, const char* const action) const
    {
        logger::debug("VRControllersSuppress: {} '{}' -> L(btn=0x{:X} ax=0x{:X}) R(btn=0x{:X} ax=0x{:X}); {} owner(s)",
            action,
            key,
            _left.buttons.load(std::memory_order_relaxed),
            static_cast<unsigned>(_left.axes.load(std::memory_order_relaxed)),
            _right.buttons.load(std::memory_order_relaxed),
            static_cast<unsigned>(_right.axes.load(std::memory_order_relaxed)),
            _owners.size());
    }

    /**
     * Suppresses a button on the given hand for `key`; persists until that key releases it.
     */
    void VRControllersSuppressor::suppress(const std::string_view key, const Hand hand, const vr::EVRButtonId button)
    {
        setSuppressed(key, hand, button, true);
    }

    /**
     * Stops `key` suppressing a button on the given hand.
     */
    void VRControllersSuppressor::release(const std::string_view key, const Hand hand, const vr::EVRButtonId button)
    {
        setSuppressed(key, hand, button, false);
    }

    /**
     * Sets or clears `key`'s suppression for a button. Analog-backed buttons (trigger/grip/thumbstick-
     * click) also toggle their backing axis, because the game re-derives those presses from rAxis.
     */
    void VRControllersSuppressor::setSuppressed(const std::string_view key, const Hand hand, const vr::EVRButtonId button, const bool suppressed)
    {
        const bool left = isLeftHand(hand);
        const uint64_t bit = 1ull << button;
        const int axis = backingAxisIndex(button);
        editOwner(key, suppressed, [&](OwnerMask& owner) {
            SideMask& side = left ? owner.left : owner.right;
            if (suppressed) {
                side.buttons |= bit;
                if (axis >= 0) {
                    side.axes |= static_cast<uint8_t>(1u << axis);
                }
            } else {
                side.buttons &= ~bit;
                if (axis >= 0) {
                    side.axes &= static_cast<uint8_t>(~(1u << axis));
                }
            }
        });
    }

    /**
     * True if the button is currently suppressed on the given hand by any owner.
     */
    bool VRControllersSuppressor::isSuppressed(const Hand hand, const vr::EVRButtonId button) const
    {
        const HandMask& mask = isLeftHand(hand) ? _left : _right;
        return (mask.buttons.load(std::memory_order_acquire) & (1ull << button)) != 0;
    }

    /**
     * True if `key` specifically is suppressing the button on the given hand.
     */
    bool VRControllersSuppressor::isSuppressedBy(const std::string_view key, const Hand hand, const vr::EVRButtonId button) const
    {
        const auto it = _owners.find(key);
        if (it == _owners.end()) {
            return false;
        }
        const SideMask& side = isLeftHand(hand) ? it->second.left : it->second.right;
        return (side.buttons & (1ull << button)) != 0;
    }

    /**
     * Suppresses one analog axis on the given hand for `key`.
     */
    void VRControllersSuppressor::suppressAxis(const std::string_view key, const Hand hand, const Axis axis)
    {
        setAxisSuppressed(key, hand, axis, true);
    }

    /**
     * Stops `key` suppressing one analog axis on the given hand.
     */
    void VRControllersSuppressor::releaseAxis(const std::string_view key, const Hand hand, const Axis axis)
    {
        setAxisSuppressed(key, hand, axis, false);
    }

    /**
     * Sets or clears `key`'s suppression for one analog axis; out-of-range axes are ignored.
     */
    void VRControllersSuppressor::setAxisSuppressed(const std::string_view key, const Hand hand, const Axis axis, const bool suppressed)
    {
        const auto axisIndex = static_cast<int>(axis);
        if (std::cmp_greater_equal(axisIndex, vr::k_unControllerStateAxisCount)) {
            return;
        }
        const bool left = isLeftHand(hand);
        const uint8_t bit = static_cast<uint8_t>(1u << axisIndex);
        editOwner(key, suppressed, [&](OwnerMask& owner) {
            SideMask& side = left ? owner.left : owner.right;
            if (suppressed) {
                side.axes |= bit;
            } else {
                side.axes &= static_cast<uint8_t>(~bit);
            }
        });
    }

    /**
     * Suppresses every analog axis on the given hand for `key`.
     */
    void VRControllersSuppressor::suppressAllAxes(const std::string_view key, const Hand hand)
    {
        setAllAxesSuppressed(key, hand, true);
    }

    /**
     * Releases `key`'s suppression of every analog axis on the given hand.
     */
    void VRControllersSuppressor::releaseAllAxes(const std::string_view key, const Hand hand)
    {
        setAllAxesSuppressed(key, hand, false);
    }

    /**
     * Sets or clears `key`'s suppression for all analog axes on the hand in a single masked op.
     */
    void VRControllersSuppressor::setAllAxesSuppressed(const std::string_view key, const Hand hand, const bool suppressed)
    {
        const bool left = isLeftHand(hand);
        editOwner(key, suppressed, [&](OwnerMask& owner) {
            SideMask& side = left ? owner.left : owner.right;
            if (suppressed) {
                side.axes |= ALL_AXES_MASK;
            } else {
                side.axes &= static_cast<uint8_t>(~ALL_AXES_MASK);
            }
        });
    }

    /**
     * Suppresses every button and analog axis on the given hand for `key`.
     */
    void VRControllersSuppressor::suppressAll(const std::string_view key, const Hand hand)
    {
        setAllSuppressed(key, hand, true);
    }

    /**
     * Suppresses every button and analog axis on both hands for `key`.
     */
    void VRControllersSuppressor::suppressAll(const std::string_view key)
    {
        setAllSuppressed(key, true);
    }

    /**
     * Sets or clears `key`'s suppression of every button and analog axis on the given hand. Setting
     * all 64 button bits makes the game see no press at all on that hand.
     */
    void VRControllersSuppressor::setAllSuppressed(const std::string_view key, const Hand hand, const bool suppressed)
    {
        const bool left = isLeftHand(hand);
        editOwner(key, suppressed, [&](OwnerMask& owner) {
            SideMask& side = left ? owner.left : owner.right;
            if (suppressed) {
                side.buttons = ~0ull;
                side.axes = ALL_AXES_MASK;
            } else {
                side.buttons = 0;
                side.axes = 0;
            }
        });
    }

    /**
     * Sets or clears `key`'s suppression of every button and analog axis on both hands.
     */
    void VRControllersSuppressor::setAllSuppressed(const std::string_view key, const bool suppressed)
    {
        setAllSuppressed(key, Hand::Left, suppressed);
        setAllSuppressed(key, Hand::Right, suppressed);
    }

    /**
     * Drops everything `key` suppressed on both hands and republishes the aggregate.
     */
    void VRControllersSuppressor::release(const std::string_view key)
    {
        const auto it = _owners.find(key);
        if (it != _owners.end()) {
            _owners.erase(it);
            republishAggregate();
            logState(key, "released owner");
        }
    }

    /**
     * Wipes every owner and clears the published masks. For lifecycle reload (clean slate).
     */
    void VRControllersSuppressor::reset()
    {
        _owners.clear();
        _left.buttons.store(0, std::memory_order_release);
        _left.axes.store(0, std::memory_order_release);
        _right.buttons.store(0, std::memory_order_release);
        _right.axes.store(0, std::memory_order_release);
        logger::info("VRControllersSuppress: reset (all owners cleared)");
    }

    /**
     * Resolves a logical hand to its physical side (true == left). Mirrors VRControllersManager::getHand:
     * Primary is the dominant hand (right unless left-handed); Offhand is the other.
     */
    bool VRControllersSuppressor::isLeftHand(const Hand hand) const
    {
        const bool leftHanded = _leftHanded.load(std::memory_order_relaxed);
        switch (hand) {
        case Hand::Primary:
            return leftHanded;
        case Hand::Offhand:
            return !leftHanded;
        case Hand::Left:
            return true;
        case Hand::Right:
        default:
            return false;
        }
    }

    /**
     * Maps an analog-backed button to the rAxis index the game can re-derive its press from, or -1.
     */
    int VRControllersSuppressor::backingAxisIndex(const vr::EVRButtonId button)
    {
        // Axis0...Axis4 buttons (32..36) ARE rAxis[0..4] (thumbstick-click=0, trigger=1, ...).
        if (button >= vr::k_EButton_Axis0 && button <= vr::k_EButton_Axis4) {
            return static_cast<int>(button) - static_cast<int>(vr::k_EButton_Axis0);
        }
        // Grip is a distinct button bit (2) but its analog pull lives in rAxis[2] on FO4VR controllers.
        if (button == vr::k_EButton_Grip) {
            return 2;
        }
        return -1;
    }

    /**
     * Clears the suppressed button/axis bits from a state read meant for the game or another mod.
     * No-op for our own reads (shouldSuppress == false, i.e. inside a SelfControllerReadScope) so we
     * keep seeing raw hardware input. OpenVR / polling thread.
     */
    void VRControllersSuppressor::applyTo(const vr::TrackedDeviceIndex_t idx, vr::VRControllerState_t* state, const bool shouldSuppress) const
    {
        if (!shouldSuppress || !state) {
            return;
        }

        const bool isLeft = idx == _leftIndex.load(std::memory_order_relaxed);
        const HandMask& mask = isLeft ? _left : _right;

        const uint64_t bmask = mask.buttons.load(std::memory_order_acquire);
        state->ulButtonPressed &= ~bmask;
        state->ulButtonTouched &= ~bmask;

        if (const uint8_t amask = mask.axes.load(std::memory_order_acquire)) {
            for (int i = 0; std::cmp_less(i, vr::k_unControllerStateAxisCount); ++i) {
                if (amask & (1u << i)) {
                    state->rAxis[i].x = 0.0f;
                    state->rAxis[i].y = 0.0f;
                }
            }
        }
    }

    /**
     * vtable trampoline for slot 34: runs the original poll, then masks the result unless this is one
     * of our own reads (thread marked by SelfControllerReadScope). Runs on the calling thread.
     */
    bool VRControllersSuppressor::hookedGetControllerState(vr::IVRSystem* system, const vr::TrackedDeviceIndex_t index, vr::VRControllerState_t* state, const uint32_t stateSize)
    {
        const bool ok = _origGetControllerState(system, index, state, stateSize);
        if (ok) {
            VRControllersSuppress.applyTo(index, state, !isSelfControllerRead());
        }
        return ok;
    }

    /**
     * vtable trampoline for slot 35: runs the original poll, then masks the result unless this is one
     * of our own reads (thread marked by SelfControllerReadScope). Runs on the calling thread.
     */
    bool VRControllersSuppressor::hookedGetControllerStateWithPose(vr::IVRSystem* system, const vr::ETrackingUniverseOrigin origin, const vr::TrackedDeviceIndex_t index,
        vr::VRControllerState_t* state, const uint32_t stateSize, vr::TrackedDevicePose_t* pose)
    {
        const bool ok = _origGetControllerStateWithPose(system, origin, index, state, stateSize, pose);
        if (ok) {
            VRControllersSuppress.applyTo(index, state, !isSelfControllerRead());
        }
        return ok;
    }
}
