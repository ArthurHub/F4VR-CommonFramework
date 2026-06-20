#pragma once

#include <span>
#include <vector>

#include "vrcf/VRControllersManager.h"

namespace f4cf::f4vr
{
    /**
     * Reusable proximity interaction zone: a sphere around a parent node that, when the player's hand
     * enters it, suppresses that hand's button (so it can't also fire its normal action) and pulses a
     * one-shot entry haptic, plus — only while debugging — renders the framework debug sphere at the zone's
     * world-space center so it always matches the test.
     *
     * Geometry: the center + radius come from a config transform carried off the parent node (the engine's
     * local->world convention, see MatrixUtils::calculateRelocation). The zone is a sphere, so only the
     * transform's translate and scale matter; its rotation is irrelevant to the test. The radius is the
     * transform scale times the parent's world scale times the debug sphere mesh's base radius. Handedness
     * mirroring, when needed, is the caller's job — mirror the zone transform before passing it in.
     *
     * The zone is measured from `node`, but `node` need not be part of the rendered scene graph: the raw VR
     * tracking nodes (HMD / wand) give the most accurate hand/head position yet don't render their children.
     * For those, pass a known-visible `debugNode` (e.g. the rendered skeleton root); the debug sphere is then
     * parented there but relocated each frame to the zone's world-space center, so the visual still matches
     * the test exactly.
     *
     * Input suppression is owner-keyed (vrcf::VRControllersSuppress): every suppress/release this zone
     * issues is tagged with the key passed at construction, so independent zones never fight over a button.
     * All suppress/release and haptic calls must run on the main thread, as the framework requires.
     */
    class WandActivationSphere
    {
    public:
        struct Frame
        {
            bool enabled = true;
            RE::NiNode* node = nullptr;
            RE::NiTransform zone;
            std::initializer_list<vrcf::InputBinding> bindings;
            bool showDebug = false;
            // Optional known-visible node to render the debug sphere under, for when `node` (the node the
            // zone is measured from) isn't part of the rendered scene graph — e.g. the raw HMD / wand
            // tracking nodes. The sphere is relocated to the zone's world-space center/radius regardless, so
            // it still matches the hit test. Defaults to `node` when null.
            RE::NiNode* debugNode = nullptr;
        };

        explicit WandActivationSphere(const char* key, const std::uint64_t cooldownMs = 400)
            : _sphereKey(key),
              _cooldownMs(cooldownMs)
        {}

        /**
         * Drives one frame of proximity-gated activation: debug visual, per-binding hand proximity,
         * owner-keyed suppression of the bindings whose wand is inside the zone, a one-shot entry haptic,
         * the binding press check, success haptic, and cooldown. `onActivated` is invoked only when a
         * binding fires (and the zone isn't cooling down) and should return true when it handled the
         * activation; the first handled binding wins for the frame.
         */
        template <class OnActivated>
        bool onFrameUpdate(const Frame& frame, OnActivated&& onActivated)
        {
            updateDebug(frame.node, frame.debugNode ? frame.debugNode : frame.node, frame.zone, frame.enabled && frame.showDebug);

            if (!frame.enabled || !frame.node) {
                resetInteraction();
                return false;
            }

            // Bindings whose wand is inside the zone this frame — the set we want suppressed. Left empty
            // (allocation-free) on the common idle path; it only allocates once a wand is actually inside.
            std::vector<vrcf::InputBinding> inside;

            bool handled = false;
            for (const auto& binding : frame.bindings) {
                if (!isInsideZone(frame, binding)) {
                    continue;
                }

                inside.push_back(binding);
                triggerHapticOnce(binding.hand);

                if (!isCoolingDown() && !handled && vrcf::VRControllers.check(binding) && onActivated(binding)) {
                    triggerActivation(binding.hand);
                    handled = true;
                }
            }

            applySuppressions(inside);

            if (inside.empty()) {
                _hapticFired = false;
            }

            return handled;
        }

        /**
         * Detaches the debug sphere from its parent while keeping the clone cached for reuse.
         */
        void detachDebug() const;

        /**
         * Whether this zone is currently suppressing the same physical input (hand + button/axis) as
         * `binding` this frame — i.e. a bound hand is inside the zone for a gesture on that input. Lets a
         * zone-less handler that shares the input defer to an in-progress proximity gesture instead of
         * double-firing. Reflects this frame once onFrameUpdate has run for the zone.
         */
        bool isSuppressing(const vrcf::InputBinding& binding) const
        {
            return containsSuppression(_suppressedBindings, binding);
        }

    private:
        /**
         * True when the world-space `point` lies inside the zone defined by `zone` carried off `parent`
         * (engine local->world convention; see MatrixUtils::calculateRelocation). A sphere is rotation-
         * invariant, so only the zone's translate and scale matter. Returns false when `parent` is null.
         */
        static bool contains(const RE::NiNode* node, const RE::NiTransform& zone, const RE::NiPoint3& point);

        /**
         * True when `binding` is enabled and its hand's wand is inside the zone this frame.
         */
        static bool isInsideZone(const Frame& frame, const vrcf::InputBinding& binding);

        // --- Input suppression, owner-keyed to this zone (main thread only). ---
        // Diffs the desired (inside-the-zone) set against what we currently suppress: suppresses the
        // newly-entered bindings, releases the ones that left, and stores the new set. Suppress/release
        // are issued only on these edges, matching the suppressor's "act only on real changes" contract.
        void applySuppressions(std::span<const vrcf::InputBinding> desired);
        void resetInteraction();

        // --- One-shot haptics. ---
        void triggerHapticOnce(vrcf::Hand hand);
        void triggerActivation(vrcf::Hand hand);

        // --- Debug visual. ---
        void updateDebug(RE::NiNode* testNode, RE::NiNode* debugParent, const RE::NiTransform& zone, bool show);

        bool isCoolingDown() const;
        static bool containsSuppression(std::span<const vrcf::InputBinding> bindings, const vrcf::InputBinding& binding);
        static bool sameSuppressionInput(const vrcf::InputBinding& lhs, const vrcf::InputBinding& rhs);

        const char* _sphereKey;
        std::uint64_t _cooldownMs;
        std::uint64_t _lastActivationTime = 0;
        RE::NiPointer<RE::NiNode> _sphereNode; // debug visual only; null until the debug flag is first enabled
        std::vector<vrcf::InputBinding> _suppressedBindings; // bindings currently suppressed under _sphereKey
        bool _hapticFired = false;

        static constexpr float SPHERE_NIF_BASE_RADIUS = 0.5f;
    };
}
