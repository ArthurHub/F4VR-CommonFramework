#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ActivationSphereConfig.h"
#include "F4VRUtils.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersManager.h"

namespace f4cf::render
{
    class Texture;
}

namespace f4cf::f4vr
{
    /**
     * One binding fed to a sphere for a frame, paired with the haptic to play when it activates (std::nullopt
     * = silent). Gate the binding off for the frame by passing the disabled binding.
     */
    struct ActivationBinding
    {
        vrcf::InputBinding binding = vrcf::VRControllersManager::DisabledBinding;
        std::optional<vrcf::HapticPattern> activateHaptic = vrcf::HapticPattern::DoubleClick;
    };

    /**
     * Reusable proximity interaction zone: a sphere around a parent node that, when the player's hand
     * enters it, suppresses that hand's button (so it can't also fire its normal action) and pulses a
     * one-shot entry haptic. Two optional visuals mark the zone, each placed from the same zone transform as the
     * test so they always agree with it: a sphere mesh (Frame::showSphere, styled by Frame::sphereStyle or the default
     * style) and a small icon at the zone's center that faces the player (Frame::showIcon, Frame::iconStyle).
     *
     * Suppression is opt-in per binding (InputBinding::suppress): a binding inside the zone is hidden from
     * the game only when its flag is set; either way it still detects, haptics, and can fire. The entry
     * haptic is per frame (Frame::entryHaptic); the activation haptic is per binding
     * (ActivationBinding::activateHaptic); std::nullopt = silent. Each visual is drawn Never / Always / only
     * WhenInside a bound hand / WhenAvailable, i.e. while any binding is fed enabled this frame.
     *
     * The icon is drawn by the framework's primitive overlay rather than as a scene node: one layer shared by every
     * sphere, on top of the world and under the vrui panels, fading in and out rather than popping.
     *
     * Geometry: the center + radius come from a config transform carried off the parent node (the engine's
     * local->world convention, see MatrixUtils::calculateRelocation). The zone is a sphere, so only the
     * transform's translate and scale matter; its rotation is irrelevant to the test. The radius is the
     * transform scale times the parent's world scale times the sphere mesh's base radius. Handedness
     * mirroring, when needed, is the caller's job — mirror the zone transform before passing it in.
     *
     * The zone is measured from `node`, but `node` need not be part of the rendered scene graph: the raw VR
     * tracking nodes (HMD / wand) give the most accurate hand/head position yet don't render their children.
     * The sphere visual therefore hangs under `sphereAttachNode` — the VR primary-hand UI attach node by
     * default, or any known-visible node the caller sets — and is relocated each frame to the zone's
     * world-space center, so it still matches the test exactly. Don't attach it inside the player's 3D (the
     * skeleton / world root node): character creation rebuilds that tree, and a sphere inside it crashes the
     * game.
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
            std::initializer_list<ActivationBinding> bindings;
            // Entry haptic fired once per zone entry for whichever bound hand is inside (std::nullopt =
            // silent). The per-binding activation haptic lives on each ActivationBinding.
            std::optional<vrcf::HapticPattern> entryHaptic = vrcf::HapticPattern::Tick;
            // When the sphere visual is drawn; WhenInside / WhenAvailable are resolved against this frame's bindings.
            ActivationSphereVisibility showSphere = ActivationSphereVisibility::Never;
            // Tuning override: draw the sphere whatever showSphere says, and at the zone's full size (ignoring the
            // style's scale), so it shows exactly the volume the hit test uses. The icon is unaffected.
            bool showZone = false;
            // Optional known-visible node to attach the sphere visual under, for when `node` (the node the
            // zone is measured from) isn't part of the rendered scene graph — e.g. the raw HMD / wand tracking
            // nodes. The sphere is relocated to the zone's world-space center/radius regardless, so it still
            // matches the hit test. Left null (the default), it hangs under the VR primary-hand UI attach node
            // (PlayerNodes::primaryUIAttachNode), which renders and is not rebuilt with the player's 3D.
            RE::NiNode* sphereAttachNode = nullptr;
            // How the sphere visual looks: its mesh, the values set on the mesh's shader, and its size relative to the
            // zone (see SphereStyle). Null = getDefaultSphereStyle(). A style that draws the mesh differently from the
            // one on screen releases the clone and loads a freshly styled one on the next show (a scale change keeps
            // it); point this at config that outlives the frame.
            const SphereStyle* sphereStyle = nullptr;
            // Which way the sphere visual faces (the player's heading or the world axes); the hit test ignores it.
            ActivationSphereOrientation sphereOrientation = ActivationSphereOrientation::Body;
            // When the icon is drawn; resolved like showSphere.
            ActivationSphereVisibility showIcon = ActivationSphereVisibility::WhenInside;
            // How the icon looks (see ActivationIconStyle). Null = the default style: the framework's hand icon.
            const ActivationIconStyle* iconStyle = nullptr;
            // When the binding icon is drawn; resolved like showSphere.
            ActivationSphereVisibility showBindingIcon = ActivationSphereVisibility::Never;
            // Which bindings the binding icon shows and where (see ActivationBindingIconStyle). Null = the default style.
            // The first of `bindings` is the primary one and the second the secondary.
            const ActivationBindingIconStyle* bindingIconStyle = nullptr;
            // The config the frame is composed from: while the ray of Mods Config FO4 VR's menu is on a row of its
            // section, the sphere is drawn as showZone draws it. Null lights nothing.
            const WandActivationConfig* config = nullptr;
        };

        explicit WandActivationSphere(const char* key, const std::uint64_t cooldownMs = 400)
            : _sphereKey(key),
              _cooldownMs(cooldownMs)
        {}

        /**
         * Releases everything this zone owns on teardown: detaches the cached sphere visual from the scene
         * graph (its parent otherwise keeps it alive and on-screen, orphaning it) and drops any input
         * suppression still held under this zone's key. Main thread only, like the rest of the class.
         */
        ~WandActivationSphere();

        WandActivationSphere(const WandActivationSphere&) = delete;
        WandActivationSphere& operator=(const WandActivationSphere&) = delete;
        WandActivationSphere(WandActivationSphere&&) = delete;
        WandActivationSphere& operator=(WandActivationSphere&&) = delete;

        /**
         * Convenience overload that builds the per-frame Frame straight from a WandActivationConfig — the
         * authored bundle a mod loads from one config section — so callers pass the config once instead of
         * re-listing every field.
         */
        template <class OnActivated>
        bool onFrameUpdate(RE::NiNode* const node, const WandActivationConfig& config, OnActivated&& onActivated, RE::NiNode* const sphereAttachNode = nullptr)
        {
            return onFrameUpdate(
                Frame{
                    .node = node,
                    .zone = config.zoneFor(isInPowerArmor()),
                    .bindings = {
                        { config.primary, config.primaryHaptic },
                        { config.secondary, config.secondaryHaptic },
                    },
                    .entryHaptic = config.entryHaptic,
                    .showSphere = config.showSphere,
                    .sphereAttachNode = sphereAttachNode,
                    .sphereStyle = &config.sphereStyle,
                    .sphereOrientation = config.sphereOrientation,
                    .showIcon = config.showIcon,
                    .iconStyle = &config.iconStyle,
                    .showBindingIcon = config.showBindingIcon,
                    .bindingIconStyle = &config.bindingIconStyle,
                    .config = &config,
                },
                std::forward<OnActivated>(onActivated));
        }

        /**
         * Drives one frame of proximity-gated activation: the visuals, per-binding hand proximity,
         * owner-keyed suppression of the bindings whose wand is inside the zone, a one-shot entry haptic,
         * the binding press check, success haptic, and cooldown. `onActivated` is invoked only when a
         * binding fires (and the zone isn't cooling down) and should return true when it handled the
         * activation; the first handled binding wins for the frame.
         */
        template <class OnActivated>
        bool onFrameUpdate(const Frame& frame, OnActivated&& onActivated)
        {
            if (!frame.enabled || !frame.node) {
                updateVisual(frame, false, false);
                updateIcon(frame, false);
                updateBindingIcons(frame, false);
                resetInteraction();
                return false;
            }

            // Bindings inside the zone this frame that opt into suppression (InputBinding::suppress) — the
            // set we hide from the game. A binding with suppress=false still detects, haptics, and fires; it
            // just isn't suppressed (and so isn't reported by isSuppressing). Left empty (allocation-free) on
            // the common idle path; it only allocates once a suppressing wand is actually inside.
            std::vector<vrcf::InputBinding> toSuppress;

            bool anyAvailable = false;
            bool anyInside = false;
            bool handled = false;
            _frameBindings.clear();
            const auto updateBinding = [&](const vrcf::InputBinding& binding, const std::optional<vrcf::HapticPattern> activateHaptic) {
                if (!isInsideZone(frame, binding)) {
                    return false;
                }

                anyInside = true;
                if (binding.suppress) {
                    toSuppress.push_back(binding);
                }
                triggerHapticOnce(binding.hand, frame.entryHaptic);

                if (!isCoolingDown() && !handled && vrcf::VRControllers.check(binding) && onActivated(binding)) {
                    triggerActivation(binding.hand, activateHaptic);
                    handled = true;
                }
                return true;
            };
            for (const auto& action : frame.bindings) {
                anyAvailable = anyAvailable || action.binding.type != vrcf::ActivationType::Disabled;
                // a binding of any hand is the binding of whichever hand is in the zone, which onActivated gets; its
                // icon is that hand's too, or the primary hand's while neither is inside
                if (action.binding.hand == vrcf::Hand::Any) {
                    const auto onPrimary = action.binding.on(vrcf::Hand::Primary);
                    const auto onOffhand = action.binding.on(vrcf::Hand::Offhand);
                    const bool primaryInside = updateBinding(onPrimary, action.activateHaptic);
                    const bool offhandInside = updateBinding(onOffhand, action.activateHaptic);
                    _frameBindings.push_back({ .binding = offhandInside && !primaryInside ? onOffhand : onPrimary, .inside = primaryInside || offhandInside });
                } else {
                    _frameBindings.push_back({ .binding = action.binding, .inside = updateBinding(action.binding, action.activateHaptic) });
                }
            }

            applySuppressions(toSuppress);

            if (!anyInside) {
                _hapticFired = false;
            }

            // Resolve the visuals after the proximity test so WhenInside can react to it. The zone shows at its true
            // size while tuning, and while the menu points at the sphere's section.
            const bool showZone = frame.showZone || isMenuPointing(frame);
            updateVisual(frame, showZone || isShown(frame.showSphere, anyAvailable, anyInside), showZone);
            updateIcon(frame, isShown(frame.showIcon, anyAvailable, anyInside));
            updateBindingIcons(frame, isShown(frame.showBindingIcon, anyAvailable, anyInside));

            return handled;
        }

        /**
         * Detaches the sphere visual from its parent while keeping the clone cached for reuse.
         */
        void detachVisual() const;

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

        /**
         * The binding icons on screen, as texture paths in their order: empty once the binding icon has faded out. Kept
         * through the fade, so the icons of a hand that just left still show for a moment.
         */
        std::span<const std::string> shownBindingIcons() const
        {
            return _bindingIconOpacity > 0.0f ? std::span<const std::string>(_bindingIconPaths) : std::span<const std::string>();
        }

        static bool isPointedAt(const WandActivationConfig& config, const std::vector<std::string>& pointedIds, bool inPowerArmor);

        /**
         * A binding fed to the sphere this frame as its binding icon sees it: a binding of any hand resolved to the
         * hand inside the zone (the primary hand while neither is), and whether that hand is inside.
         */
        struct FrameBinding
        {
            vrcf::InputBinding binding;
            bool inside = false;
        };

        static std::vector<vrcf::InputBinding> selectIconBindings(std::span<const FrameBinding> bindings, ActivationBindingIcons which);
        static std::vector<float> layoutIconRow(std::span<const float> widths, float gap, float anchor);

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
        void applySuppressions(std::span<const vrcf::InputBinding> desired);
        void resetInteraction();

        // --- One-shot haptics (std::nullopt pattern = silent). ---
        void triggerHapticOnce(vrcf::Hand hand, std::optional<vrcf::HapticPattern> pattern);
        void triggerActivation(vrcf::Hand hand, std::optional<vrcf::HapticPattern> pattern);

        // --- Visuals. ---
        static bool isShown(ActivationSphereVisibility visibility, bool anyAvailable, bool anyInside);
        static bool isMenuPointing(const Frame& frame);
        void updateVisual(const Frame& frame, bool show, bool showZone);
        void updateIcon(const Frame& frame, bool show);
        void updateBindingIcons(const Frame& frame, bool show);

        bool isCoolingDown() const;
        static bool containsSuppression(std::span<const vrcf::InputBinding> bindings, const vrcf::InputBinding& binding);
        static bool sameSuppressionInput(const vrcf::InputBinding& lhs, const vrcf::InputBinding& rhs);

        const char* _sphereKey;
        std::uint64_t _cooldownMs;
        std::uint64_t _lastActivationTime = 0;
        RE::NiPointer<RE::NiNode> _sphereNode; // sphere visual only; null until the visual is first shown
        std::optional<SphereStyle> _sphereStyle; // the style last loaded into _sphereNode (or last attempted); detects a runtime change
        std::vector<vrcf::InputBinding> _suppressedBindings; // bindings currently suppressed under _sphereKey
        bool _hapticFired = false;
        std::shared_ptr<render::Texture> _iconTexture; // the icon's image; null until the icon is first shown
        std::string _iconTexturePath; // the path _iconTexture was loaded for, as configured (the loader resolves it)
        float _iconOpacity = 0.0f; // how far the icon has faded in: 0 hidden, 1 fully shown
        std::uint64_t _iconFadeTime = 0; // when _iconOpacity was last stepped
        std::vector<FrameBinding> _frameBindings; // this frame's bindings, refilled each frame without reallocating
        std::vector<std::string> _bindingIconPaths; // the binding icons last shown, kept while they fade out
        std::vector<std::shared_ptr<render::Texture>> _bindingIconTextures; // the images of _bindingIconPaths, in order
        float _bindingIconOpacity = 0.0f; // like _iconOpacity, for the binding icon
        std::uint64_t _bindingIconFadeTime = 0;
        bool _missingBindingIconLogged = false; // a binding with no icon is logged once per sphere

        static constexpr float SPHERE_NIF_BASE_RADIUS = 0.5f;
        static constexpr float ICON_FADE_MS = 100.0f;
        static constexpr float BINDING_ICON_GAP = 0.15f; // between the binding icons in a row, as a part of their size
    };
}
