#include "WandActivationSphere.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ModBase.h"
#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/F4VRUtils.h"
#include "f4vr/PlayerNodes.h"
#include "render/PrimitiveDrawRenderer.h"
#include "render/Texture.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersSuppressor.h"

namespace f4cf::f4vr
{
    namespace
    {
        // The icon drawn when a sphere's style names none.
        constexpr auto DEFAULT_ICON_TEXTURE = "f4cf\\activation-icon-hand.dds";

        // The longest time one fade step covers: frame updates stop while the game is paused, and the whole gap would
        // otherwise land on the next step, snapping the icon in or out instead of fading it.
        constexpr std::uint64_t MAX_ICON_FADE_STEP_MS = 50;

        /**
         * The overlay layer every sphere's icon is drawn on, so any number of spheres costs one draw callback. Not
         * occluded: an icon sits inside the player's body or on a held prop, and the hand covers it while inside the
         * zone, so depth-testing it against the world would hide it most of the time.
         */
        render::PrimitiveDrawRenderer& iconRenderer()
        {
            static render::PrimitiveDrawRenderer instance("ActivationIcons", render::DRAW_ORDER_HINTS, false);
            return instance;
        }

        /**
         * This frame's icons, added by every sphere's update and handed to the renderer at the frame's end.
         */
        render::PrimitiveDraw& pendingIcons()
        {
            static render::PrimitiveDraw frame;
            return frame;
        }

        /**
         * Hand the icons gathered this frame to the render thread. Runs after the mod's onFrameUpdate, where the spheres
         * update. An empty frame is still published: that is what puts the layer dormant once no icon shows.
         */
        void publishIcons()
        {
            auto icons = std::exchange(pendingIcons(), {});
            if (!icons.empty()) {
                iconRenderer().ensureInstalled();
            }
            iconRenderer().publish(std::move(icons));
        }

        /**
         * The frame to add an icon to. The first call hooks the publish onto the frame's end, so a mod whose spheres
         * never show an icon never builds the layer.
         */
        render::PrimitiveDraw& iconFrame()
        {
            static const bool registered = [] {
                registerFrameEndCallback(&publishIcons);
                return true;
            }();
            static_cast<void>(registered);
            return pendingIcons();
        }

        /**
         * Whether the style last loaded into the sphere clone draws the mesh the same way as `desired`. The scale is
         * left out: it only sizes the placed node, so changing it keeps the clone.
         */
        bool isSameLook(const std::optional<SphereStyle>& loaded, const SphereStyle& desired)
        {
            if (!loaded) {
                return false;
            }
            auto look = *loaded;
            look.scale = desired.scale;
            return look == desired;
        }
    }

    const std::string& ActivationIconStyle::textureOrDefault() const
    {
        static const std::string defaultTexture = DEFAULT_ICON_TEXTURE;
        return texture.empty() ? defaultTexture : texture;
    }

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
        if (key == "whenavailable" || key == "available") {
            return ActivationSphereVisibility::WhenAvailable;
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
     * that draws the mesh differently from the cloned one releases the cached clone so a freshly styled one is loaded
     * on the next show (a style is applied only to a clone the renderer hasn't seen yet). The style's scale shrinks (or
     * grows) the *visual* relative to the zone — the hit test always uses the unscaled zone — unless the frame's
     * showZone asks for the zone's true size.
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
        if (_sphereNode && !isSameLook(_sphereStyle, desiredStyle)) {
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
        if (!_sphereNode && !isSameLook(_sphereStyle, desiredStyle)) {
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
        // the hand. The style's scale shrinks/grows only the visual radius (the zone center is unchanged), so the drawn
        // sphere can sit inside the unscaled interaction zone.
        RE::NiTransform visualZone = frame.zone;
        if (!frame.showZone) {
            visualZone.scale *= desiredStyle.scale;
        }
        RE::NiTransform visualWorld = common::MatrixUtils::localToWorldTransform(testNode->world, visualZone);
        visualWorld.rotate = getSphereWorldRotation(frame.sphereOrientation);
        _sphereNode->local = common::MatrixUtils::worldToLocalTransform(attachParent->world, visualWorld);

        // Push the placement to world ourselves rather than relying on the parent's subtree update to pick it up
        // (under the skeleton, FRIK's per-frame skeleton update did); this also avoids a frame of lag.
        updateTransformsDown(_sphereNode.get(), true);
    }

    /**
     * Whether a visual set to `visibility` shows this frame: while any binding is fed enabled (WhenAvailable), while
     * a bound hand is inside the zone (WhenInside), always, or never.
     */
    bool WandActivationSphere::isShown(const ActivationSphereVisibility visibility, const bool anyAvailable, const bool anyInside)
    {
        switch (visibility) {
        case ActivationSphereVisibility::Always:
            return true;
        case ActivationSphereVisibility::WhenAvailable:
            return anyAvailable;
        case ActivationSphereVisibility::WhenInside:
            return anyInside;
        case ActivationSphereVisibility::Never:
        default:
            return false;
        }
    }

    /**
     * Draws the optional icon: the style's image as a quad at the zone's world-space center, turned to face the HMD,
     * fading in over ICON_FADE_MS when it starts showing and out when it stops. Kept upright to the HMD rather than to
     * world up, so it reads upright however the head is tilted and stays well defined seen from straight above, which
     * an icon on the chest nearly is. The image loads on the first frame the icon is drawn, and again only when the
     * style names a different one; a missing file draws nothing (the texture loader logs it once).
     */
    void WandActivationSphere::updateIcon(const Frame& frame, const bool show)
    {
        const std::uint64_t now = common::nowMillis();
        const float elapsed = _iconFadeTime ? static_cast<float>((std::min)(now - _iconFadeTime, MAX_ICON_FADE_STEP_MS)) : 0.0f;
        _iconFadeTime = now;
        const float step = elapsed / ICON_FADE_MS;
        _iconOpacity = std::clamp(_iconOpacity + (show ? step : -step), 0.0f, 1.0f);

        const auto hmd = getPlayer() ? getPlayerNodes()->HmdNode : nullptr;
        if (_iconOpacity <= 0.0f || !frame.node || !hmd) {
            return;
        }

        static const ActivationIconStyle defaultStyle{};
        const ActivationIconStyle& style = frame.iconStyle ? *frame.iconStyle : defaultStyle;
        if (!_iconTexture || _iconTexturePath != style.textureOrDefault()) {
            _iconTexturePath = style.textureOrDefault();
            _iconTexture = render::Texture::load(_iconTexturePath);
        }
        if (!_iconTexture) {
            return;
        }
        const render::TextureView view = _iconTexture->view();
        const float longestSide = static_cast<float>((std::max)(_iconTexture->width(), _iconTexture->height()));
        if (!view || longestSide <= 0.0f) {
            return;
        }

        const RE::NiPoint3 center = common::MatrixUtils::localToWorldPoint(frame.node->world, frame.zone.translate);
        const RE::NiPoint3 toViewer = hmd->world.translate - center;
        const float distance = common::MatrixUtils::vec3Len(toViewer);
        if (distance < 1.0f) {
            return; // at the eye, there is no direction left to face
        }
        const RE::NiPoint3 facing = toViewer * (1.0f / distance);

        // this codebase keeps world rotations transposed: row i of `rotate` is where local axis i points
        const RE::NiMatrix3& hmdRotation = hmd->world.rotate;
        const RE::NiPoint3 hmdRight(hmdRotation.entry[0][0], hmdRotation.entry[0][1], hmdRotation.entry[0][2]);
        const RE::NiPoint3 hmdUp(hmdRotation.entry[2][0], hmdRotation.entry[2][1], hmdRotation.entry[2][2]);
        RE::NiPoint3 right = common::MatrixUtils::vec3Cross(hmdUp, facing);
        const float rightLength = common::MatrixUtils::vec3Len(right);
        right = rightLength > 0.001f ? right * (1.0f / rightLength) : hmdRight; // straight above/below the head's up axis
        const RE::NiPoint3 up = common::MatrixUtils::vec3Cross(facing, right);

        // the style's size is the longer side, so a non-square image keeps its proportions inside the same bounds
        const float halfWidth = 0.5f * style.size * static_cast<float>(_iconTexture->width()) / longestSide;
        const float halfHeight = 0.5f * style.size * static_cast<float>(_iconTexture->height()) / longestSide;
        const render::Color tint{ style.color[0], style.color[1], style.color[2], style.color[3] * _iconOpacity };
        iconFrame().addImage(view,
            _iconTexture->isSRGB(),
            center - right * halfWidth + up * halfHeight,
            center + right * halfWidth + up * halfHeight,
            center + right * halfWidth - up * halfHeight,
            center - right * halfWidth - up * halfHeight,
            tint);
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
