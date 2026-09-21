#include "WandActivationSphere.h"

#include <string>

#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRUtils.h"
#include "f4vr/PlayerNodes.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersSuppressor.h"

namespace f4cf::f4vr
{
    /**
     * Releases everything this zone owns on teardown. The sphere visual is cloned once and attached under a
     * game scene-graph node (e.g. the belt Pipboy) that outlives this object; without an explicit detach the
     * parent keeps a strong ref, so the visual stays on-screen orphaned and every re-created zone stacks
     * another on top. resetInteraction() also drops any suppression still held under this zone's key.
     */
    WandActivationSphere::~WandActivationSphere()
    {
        detachVisual();
        resetInteraction();
    }

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
     * Case-insensitive name -> ActivationSphereOrientation, tolerating separators. Empty or unknown returns
     * `fallback` (unknown is logged).
     */
    ActivationSphereOrientation parseActivationSphereOrientation(const std::string_view text, const ActivationSphereOrientation fallback)
    {
        const std::string key = common::normalizeConfigToken(text);

        if (key.empty()) {
            return fallback;
        }
        if (key == "hmd" || key == "head") {
            return ActivationSphereOrientation::Hmd;
        }
        if (key == "body" || key == "skeleton") {
            return ActivationSphereOrientation::Body;
        }
        if (key == "world") {
            return ActivationSphereOrientation::World;
        }
        logger::warn("parseActivationSphereOrientation: unrecognized value '{}' - using fallback", key);
        return fallback;
    }

    namespace
    {
        /**
         * A world rotation turned about world up to `node`'s heading. The heading is the node's forward (local +Y,
         * which the HMD and the skeleton share) flattened onto the ground, plus its up axis (local +Z) weighted by
         * how far it is pitched — looking straight down or up, the forward axis has no horizontal part left but the
         * up axis points along the heading, so the sphere never spins there. The world axes while `node` is null.
         */
        RE::NiMatrix3 getHeadingRotation(const RE::NiAVObject* node)
        {
            if (!node) {
                return common::MatrixUtils::getIdentityMatrix();
            }

            // this codebase keeps world rotations transposed: row i of `rotate` is where local axis i points
            const RE::NiMatrix3& rotation = node->world.rotate;
            const RE::NiPoint3 forward(rotation.entry[1][0], rotation.entry[1][1], rotation.entry[1][2]);
            const RE::NiPoint3 up(rotation.entry[2][0], rotation.entry[2][1], rotation.entry[2][2]);
            const float x = forward.x - forward.z * up.x;
            const float y = forward.y - forward.z * up.y;
            const float length = std::sqrt(x * x + y * y);
            if (length < 0.0001f) {
                return common::MatrixUtils::getIdentityMatrix();
            }
            const float dx = x / length;
            const float dy = y / length;

            // local +Y along the heading, +Z world up, +X to its right
            RE::NiMatrix3 rotate = common::MatrixUtils::getIdentityMatrix();
            rotate.entry[0][0] = dy;
            rotate.entry[0][1] = -dx;
            rotate.entry[1][0] = dx;
            rotate.entry[1][1] = dy;
            return rotate;
        }

        /**
         * World rotation of a sphere visual oriented per `orientation`: the HMD's heading, the body's (the rendered
         * third-person skeleton root, which FRIK turns with the body), or the world axes. The world axes while the
         * chosen node isn't loaded.
         */
        RE::NiMatrix3 getSphereWorldRotation(const ActivationSphereOrientation orientation)
        {
            if (!getPlayer()) {
                return common::MatrixUtils::getIdentityMatrix();
            }
            switch (orientation) {
            case ActivationSphereOrientation::Hmd:
                return getHeadingRotation(getPlayerNodes()->HmdNode);
            case ActivationSphereOrientation::Body:
                return getHeadingRotation(getRootNode());
            case ActivationSphereOrientation::World:
            default:
                return common::MatrixUtils::getIdentityMatrix();
            }
        }
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
     * first show (cached for reuse, collision stripped), (re)attaches it under the frame's sphereAttachNode, and
     * relocates it to the zone's world-space center/radius, facing per the frame's sphereOrientation, so the visual
     * and the test always agree — even when the attach node differs from `frame.node` (the node the zone is
     * measured from). This lets the zone be measured off a raw, non-rendering tracking node (HMD / wand) while the
     * sphere renders under a visible one. A null attach node defaults to the VR primary-hand UI attach node: it
     * renders, and it lives outside the player's 3D, which character creation rebuilds — a sphere attached inside
     * that tree crashes the game.
     *
     * The frame's sphereStyle selects the mesh and the values set on its shader (null = the default style); a style
     * that differs from the cloned one releases the cached clone so a freshly styled one is loaded on the next show
     * (a style is applied only to a clone the renderer hasn't seen yet). sphereScale shrinks (or grows) the
     * *visual* relative to the zone — the hit test always uses the unscaled zone.
     */
    void WandActivationSphere::updateVisual(const Frame& frame, const bool show)
    {
        const RE::NiNode* testNode = frame.node;
        RE::NiNode* attachParent = frame.sphereAttachNode;
        if (!attachParent && getPlayer()) {
            attachParent = getPlayerNodes()->primaryUIAttachNode;
        }

        // Fast idle path: nothing shown and nothing cached, so there is no style to compare while the sphere is
        // hidden — the common case.
        if (!_sphereNode && (!show || !testNode || !attachParent)) {
            return;
        }

        const SphereStyle& desiredStyle = frame.sphereStyle ? *frame.sphereStyle : getDefaultSphereStyle();

        // Runtime restyle: the configured style changed, so drop the stale clone; it is re-loaded below.
        if (_sphereNode && _sphereStyle != desiredStyle) {
            detachVisual();
            _sphereNode.reset();
        }

        if (!show || !testNode || !attachParent) {
            detachVisual();
            return;
        }

        // Lazily (re)load the mesh once per distinct style. _sphereStyle records the last *attempted* style
        // (set before the load) so a bad/mistyped mesh path — which throws out of the loader — is tried only once,
        // not re-thrown every shown frame; changing the style retries. A load failure leaves the visual off while
        // the interaction/haptics keep running.
        if (!_sphereNode && _sphereStyle != desiredStyle) {
            _sphereStyle = desiredStyle;
            const auto& nif = desiredStyle.nifOrDefault();
            try {
                _sphereNode.reset(getClonedNiNodeForNifFileSetName(nif, _sphereKey));
            } catch (const std::exception& ex) {
                logger::error("WandActivationSphere: failed to load sphere NIF '{}': {}", nif, ex.what());
            }
            if (_sphereNode) {
                _sphereNode->collisionObject.reset();
                applySphereStyle(_sphereNode.get(), desiredStyle);
                logger::info("WandActivationSphere: loaded sphere NIF '{}' with texture '{}' ({})", nif, desiredStyle.texture, _sphereKey);
            }
        }

        if (!_sphereNode) {
            return;
        }

        if (_sphereNode->parent != attachParent) {
            attachParent->AttachChild(_sphereNode.get(), true);
        }

        // Place the zone (local to testNode) in world space, then re-express it as a local transform under
        // attachParent, so the sphere renders exactly where the hit test measures even when it hangs under a
        // different node. Its orientation comes from sphereOrientation, not the parent: under a moving parent (the
        // primary-hand UI node) a sphere with a patterned texture, like the debug grid, would otherwise turn with
        // the hand. sphereScale shrinks/grows only the visual radius (the zone center is unchanged), so the drawn
        // sphere can sit inside the unscaled interaction zone.
        RE::NiTransform visualZone = frame.zone;
        visualZone.scale *= frame.sphereScale;
        RE::NiTransform visualWorld = common::MatrixUtils::localToWorldTransform(testNode->world, visualZone);
        visualWorld.rotate = getSphereWorldRotation(frame.sphereOrientation);
        _sphereNode->local = common::MatrixUtils::worldToLocalTransform(attachParent->world, visualWorld);

        // Push the placement to world ourselves rather than relying on the parent's subtree update to pick it up
        // (under the skeleton, FRIK's per-frame skeleton update did); this also avoids a frame of lag.
        updateTransformsDown(_sphereNode.get(), true);
    }

    /**
     * Detaches the sphere visual from its parent while keeping the clone cached for reuse.
     */
    void WandActivationSphere::detachVisual() const
    {
        if (_sphereNode && _sphereNode->parent) {
            RE::NiPointer<RE::NiAVObject> held;
            _sphereNode->parent->DetachChild(_sphereNode.get(), held);
        }
    }
}
