#include "WandActivationSphere.h"

#include <string>

#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRUtils.h"
#include "f4vr/PlayerNodes.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersSuppressor.h"
#include "vrui/UIUtils.h"

namespace f4cf::f4vr
{
    /**
     * Case-insensitive name -> ActivationSphereVisibility, tolerating separators. Empty or unknown returns
     * `fallback` (unknown is logged).
     */
    ActivationSphereVisibility parseActivationSphereVisibility(const std::string_view text, const ActivationSphereVisibility fallback)
    {
        const std::string key = common::normalizeConfigToken(text);

        if (key.empty()) {
            return fallback;
        }
        if (key == "never" || key == "off" || key == "false" || key == "none") {
            return ActivationSphereVisibility::Never;
        }
        if (key == "always" || key == "on" || key == "true") {
            return ActivationSphereVisibility::Always;
        }
        if (key == "wheninside" || key == "inside" || key == "proximity" || key == "active" || key == "near") {
            return ActivationSphereVisibility::WhenInside;
        }
        logger::warn("parseActivationSphereVisibility: unrecognized value '{}' - using fallback", key);
        return fallback;
    }

    /**
     * World-space sphere test: derives the zone center (the zone translate carried off `parent` via the
     * engine's local->world convention) and radius (zone scale * parent world scale * the mesh base
     * radius), then checks `point` against it. Rotation is ignored — a sphere is rotation-invariant.
     * Returns false when `parent` is null.
     */
    bool WandActivationSphere::contains(const RE::NiNode* node, const RE::NiTransform& zone, const RE::NiPoint3& point)
    {
        if (!node) {
            return false;
        }

        const RE::NiPoint3 center = common::MatrixUtils::localToWorldPoint(node->world, zone.translate);
        const float radius = zone.scale * node->world.scale * SPHERE_NIF_BASE_RADIUS;
        return common::MatrixUtils::vec3Len(point - center) <= radius;
    }

    /**
     * Whether `binding` is active and its hand's wand node currently sits inside the zone this frame.
     */
    bool WandActivationSphere::isInsideZone(const Frame& frame, const vrcf::InputBinding& binding)
    {
        if (binding.type == vrcf::ActivationType::Disabled) {
            return false;
        }

        const auto wand = isPrimaryHand(binding.hand) ? getPlayerNodes()->primaryWandNode : getPlayerNodes()->SecondaryWandNode;
        return wand && contains(frame.node, frame.zone, wand->world.translate);
    }

    /**
     * Reconciles the suppressed set with the bindings inside the zone this frame: suppresses the ones
     * that just entered, releases the ones that left, then records the new set. Calls land only on these
     * edges, so the owner-keyed suppressor logs/republishes at most once per transition, never per frame.
     */
    void WandActivationSphere::applySuppressions(const std::span<const vrcf::InputBinding> desired)
    {
        // Suppress bindings that just entered the zone.
        for (const auto& binding : desired) {
            if (!containsSuppression(_suppressedBindings, binding)) {
                vrcf::VRControllersSuppress.suppress(_sphereKey, binding);
            }
        }

        // Release bindings that left the zone (no longer desired).
        for (const auto& binding : _suppressedBindings) {
            if (!containsSuppression(desired, binding)) {
                vrcf::VRControllersSuppress.release(_sphereKey, binding);
            }
        }

        _suppressedBindings.assign(desired.begin(), desired.end());
    }

    /**
     * Drops everything this zone owns — releases all of its suppressions and re-arms the entry haptic.
     * Used on the disabled / missing-node path so nothing stays suppressed while the zone is inactive.
     */
    void WandActivationSphere::resetInteraction()
    {
        vrcf::VRControllersSuppress.release(_sphereKey);
        _suppressedBindings.clear();
        _hapticFired = false;
    }

    /**
     * Fires the entry haptic once per zone entry (skipped when the pattern is std::nullopt); re-armed by the
     * caller once no wand remains inside. The latch is set regardless so a silent zone doesn't re-evaluate
     * each frame.
     */
    void WandActivationSphere::triggerHapticOnce(const vrcf::Hand hand, const std::optional<vrcf::HapticPattern> pattern)
    {
        if (!_hapticFired) {
            _hapticFired = true;
            if (pattern) {
                vrcf::VRHaptics.trigger(hand, *pattern);
            }
        }
    }

    /**
     * Plays the activation haptic (skipped when the pattern is std::nullopt) and starts the post-activation
     * cooldown.
     */
    void WandActivationSphere::triggerActivation(const vrcf::Hand hand, const std::optional<vrcf::HapticPattern> pattern)
    {
        if (pattern) {
            vrcf::VRHaptics.trigger(hand, *pattern);
        }
        _lastActivationTime = common::nowMillis();
    }

    /**
     * Whether we are still within the cooldown window after the last activation (always false when the
     * cooldown is zero).
     */
    bool WandActivationSphere::isCoolingDown() const
    {
        return _cooldownMs > 0 && !common::isNowTimePassed(_lastActivationTime, static_cast<int>(_cooldownMs));
    }

    /**
     * Whether `bindings` already holds an entry targeting the same physical input as `binding`.
     */
    bool WandActivationSphere::containsSuppression(const std::span<const vrcf::InputBinding> bindings, const vrcf::InputBinding& binding)
    {
        for (const auto& candidate : bindings) {
            if (sameSuppressionInput(candidate, binding)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Whether two bindings address the same physical input — same hand and either the same backing axis
     * (for an AxisDirection binding) or the same button. The activation type otherwise doesn't matter;
     * only the bit that gets suppressed does.
     */
    bool WandActivationSphere::sameSuppressionInput(const vrcf::InputBinding& lhs, const vrcf::InputBinding& rhs)
    {
        if (lhs.hand != rhs.hand) {
            return false;
        }

        const bool lhsAxis = lhs.type == vrcf::ActivationType::AxisDirection;
        const bool rhsAxis = rhs.type == vrcf::ActivationType::AxisDirection;
        if (lhsAxis != rhsAxis) {
            return false;
        }

        return lhsAxis ? lhs.axis == rhs.axis : lhs.button == rhs.button;
    }

    /**
     * Keeps the optional sphere visual in sync with the zone: detaches it when hidden, lazily clones it on
     * first show (cached for reuse, collision stripped), (re)attaches it under `debugParent`, and relocates
     * it to the zone's world-space center/radius so the visual and the test always agree — even when
     * `debugParent` differs from `testNode` (the node the zone is measured from). This lets the zone be
     * measured off a raw, non-rendering tracking node (HMD / wand) while the sphere renders under a visible
     * one.
     *
     * `nifName` selects the mesh (empty = the framework default debug sphere); a change from the currently
     * cloned nif releases the cached clone so the new one is loaded on the next show. `sphereScale` shrinks
     * (or grows) the *visual* relative to the zone — the hit test always uses the unscaled zone.
     */
    void WandActivationSphere::updateDebug(const RE::NiNode* testNode, RE::NiNode* debugParent, const RE::NiTransform& zone, const bool show, const std::string_view nifName,
        const float sphereScale)
    {
        // Fast idle path: nothing shown and nothing cached, so there is no nif work to do (avoids resolving
        // the default name every frame while the sphere is hidden — the common case).
        if (!_sphereNode && (!show || !testNode || !debugParent)) {
            return;
        }

        const std::string desiredNif = nifName.empty() ? vrui::UIUtils::getDebugSphereNifName() : std::string(nifName);

        // Runtime nif swap: the configured mesh changed, so drop the stale clone; it is re-loaded below.
        if (_sphereNode && _sphereNifName != desiredNif) {
            detachDebug();
            _sphereNode.reset();
        }

        if (!show || !testNode || !debugParent) {
            detachDebug();
            return;
        }

        // Lazily (re)load the mesh once per distinct name. _sphereNifName records the last *attempted* name
        // (set before the load) so a bad/mistyped path — which throws out of the loader — is tried only once,
        // not re-thrown every shown frame; changing the path to another value retries. A load failure leaves
        // the visual off while the interaction/haptics keep running.
        if (!_sphereNode && _sphereNifName != desiredNif) {
            _sphereNifName = desiredNif;
            try {
                _sphereNode.reset(getClonedNiNodeForNifFileSetName(desiredNif, _sphereKey));
            } catch (const std::exception& ex) {
                logger::warn("WandActivationSphere: failed to load sphere NIF '{}': {}", desiredNif, ex.what());
            }
            if (_sphereNode) {
                _sphereNode->collisionObject.reset();
                logger::info("WandActivationSphere: loaded sphere NIF '{}' ({})", desiredNif, _sphereKey);
            }
        }

        if (!_sphereNode) {
            return;
        }

        if (_sphereNode->parent != debugParent) {
            debugParent->AttachChild(_sphereNode.get(), true);
        }

        // Re-express the zone (local to testNode) as a local transform under debugParent, keeping its world
        // placement, so the sphere renders exactly where the hit test measures even when it hangs under a
        // different node. Rotation is dropped (irrelevant for a sphere); when debugParent == testNode this
        // round-trips back to the zone's translate/scale. sphereScale shrinks/grows only the visual radius
        // (the zone center is unchanged), so the drawn sphere can sit inside the unscaled interaction zone.
        RE::NiTransform visualZone = zone;
        visualZone.scale *= sphereScale;
        _sphereNode->local = common::MatrixUtils::reparentTransform(testNode->world, visualZone, debugParent->world, false);
    }

    /**
     * Detaches the debug sphere from its parent while keeping the clone cached for reuse.
     */
    void WandActivationSphere::detachDebug() const
    {
        if (_sphereNode && _sphereNode->parent) {
            RE::NiPointer<RE::NiAVObject> held;
            _sphereNode->parent->DetachChild(_sphereNode.get(), held);
        }
    }
}
