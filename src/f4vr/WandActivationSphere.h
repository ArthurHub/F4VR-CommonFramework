#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "F4VRUtils.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersManager.h"

namespace f4cf::f4vr
{
    /**
     * When the activation sphere's visual (the sphere mesh — Frame::sphereNif or the framework default) is
     * drawn.
     */
    enum class ActivationSphereVisibility : std::uint8_t
    {
        Never = 0, // never draw the sphere
        Always, // always draw it (tuning / debug)
        WhenInside, // draw it only while a bound hand is inside the zone (a proximity hint)
    };

    /**
     * Parse an ActivationSphereVisibility from config (INI) text, case-insensitively: "never"/"off"/"false"
     * -> Never; "always"/"on"/"true" -> Always; "wheninside"/"inside"/"proximity"/"active" -> WhenInside.
     * Empty or unrecognized text returns `fallback` (unrecognized is logged).
     */
    ActivationSphereVisibility parseActivationSphereVisibility(std::string_view text, ActivationSphereVisibility fallback);

    /**
     * Authored, config-loadable description of one activation sphere: its zone (+ optional PA variant), its
     * two bindings (each carrying its own suppress flag), the entry haptic plus a per-binding activation
     * haptic, and when the sphere visual is shown. A mod parses one INI section into this
     * (ConfigBase::loadWandActivationConfig); the per-frame WandActivationSphere::Frame is composed from it
     * with live gating (real binding vs the disabled binding) and any runtime zone re-anchor on top. The
     * sphere only uses the zone's translate + scale (it is rotation-invariant). `secondary` defaults to
     * disabled for single-binding spheres; haptics are std::nullopt = silent.
     */
    struct WandActivationConfig
    {
        RE::NiTransform zone{};
        // Optional power-armor variant of the zone; when unset, zoneFor() falls back to `zone`. Lets a sphere
        // whose anchor moves in power armor (e.g. the chest-stowed body grab) carry both placements in one
        // bundle instead of a separate parallel field.
        std::optional<RE::NiTransform> zonePA;
        vrcf::InputBinding primary{};
        vrcf::InputBinding secondary = vrcf::VRControllersManager::DisabledBinding;
        std::optional<vrcf::HapticPattern> entryHaptic = vrcf::HapticPattern::Tick;
        std::optional<vrcf::HapticPattern> primaryHaptic = vrcf::HapticPattern::DoubleClick;
        std::optional<vrcf::HapticPattern> secondaryHaptic = vrcf::HapticPattern::DoubleClick;
        ActivationSphereVisibility showSphere = ActivationSphereVisibility::Never;
        std::string sphereNif;
        float sphereScale = 1.0f;

        /**
         * The zone to use for the given power-armor state: the PA variant when one was configured, otherwise
         * the regular zone.
         */
        const RE::NiTransform& zoneFor(const bool inPowerArmor) const
        {
            return inPowerArmor && zonePA ? *zonePA : zone;
        }
    };

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
     * one-shot entry haptic, plus renders a sphere mesh at the zone's world-space center (per
     * Frame::showSphere, using Frame::sphereNif or the framework default) so any visual always matches the
     * test.
     *
     * Suppression is opt-in per binding (InputBinding::suppress): a binding inside the zone is hidden from
     * the game only when its flag is set; either way it still detects, haptics, and can fire. The entry
     * haptic is per frame (Frame::entryHaptic); the activation haptic is per binding
     * (ActivationBinding::activateHaptic); std::nullopt = silent. The sphere visual is drawn Never / Always /
     * only WhenInside a bound hand (Frame::showSphere).
     *
     * Geometry: the center + radius come from a config transform carried off the parent node (the engine's
     * local->world convention, see MatrixUtils::calculateRelocation). The zone is a sphere, so only the
     * transform's translate and scale matter; its rotation is irrelevant to the test. The radius is the
     * transform scale times the parent's world scale times the sphere mesh's base radius. Handedness
     * mirroring, when needed, is the caller's job — mirror the zone transform before passing it in.
     *
     * The zone is measured from `node`, but `node` need not be part of the rendered scene graph: the raw VR
     * tracking nodes (HMD / wand) give the most accurate hand/head position yet don't render their children.
     * The sphere visual therefore hangs under `sphereAttachNode` — the world root node by default (always
     * rendered), or any known-visible node the caller sets — and is relocated each frame to the zone's
     * world-space center, so it still matches the test exactly.
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
            // When the sphere visual is drawn (Never / Always / WhenInside); WhenInside is resolved after the
            // proximity test each frame.
            ActivationSphereVisibility showSphere = ActivationSphereVisibility::Never;
            // Optional known-visible node to attach the sphere visual under, for when `node` (the node the
            // zone is measured from) isn't part of the rendered scene graph — e.g. the raw HMD / wand tracking
            // nodes. The sphere is relocated to the zone's world-space center/radius regardless, so it still
            // matches the hit test. Left null (the default), it hangs under the world root node (getRootNode()).
            RE::NiNode* sphereAttachNode = nullptr;
            // The .nif cloned as the sphere visual. Empty = the framework's default sphere mesh
            // (vrui::UIUtils::getDebugSphereNifName()). A different value swaps the mesh; changing it between
            // frames releases the previous clone and loads the new one on the next show.
            std::string_view sphereNif = {};
            // Scale multiplier applied to the zone scale for the visual sphere only — the proximity hit test
            // always uses the full zone scale. < 1 draws the sphere smaller than the interaction radius (a
            // "hand is inside" indicator that sits within the real zone); 1 = the visual matches the zone.
            float sphereScale = 1.0f;
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
         * authored bundle a mod loads from one INI section — so callers pass the config once instead of
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
                    .sphereNif = config.sphereNif,
                    .sphereScale = config.sphereScale,
                },
                std::forward<OnActivated>(onActivated));
        }

        /**
         * Drives one frame of proximity-gated activation: sphere visual, per-binding hand proximity,
         * owner-keyed suppression of the bindings whose wand is inside the zone, a one-shot entry haptic,
         * the binding press check, success haptic, and cooldown. `onActivated` is invoked only when a
         * binding fires (and the zone isn't cooling down) and should return true when it handled the
         * activation; the first handled binding wins for the frame.
         */
        template <class OnActivated>
        bool onFrameUpdate(const Frame& frame, OnActivated&& onActivated)
        {
            if (!frame.enabled || !frame.node) {
                updateVisual(frame.node, frame.sphereAttachNode, frame.zone, false, frame.sphereNif, frame.sphereScale);
                resetInteraction();
                return false;
            }

            // Bindings inside the zone this frame that opt into suppression (InputBinding::suppress) — the
            // set we hide from the game. A binding with suppress=false still detects, haptics, and fires; it
            // just isn't suppressed (and so isn't reported by isSuppressing). Left empty (allocation-free) on
            // the common idle path; it only allocates once a suppressing wand is actually inside.
            std::vector<vrcf::InputBinding> toSuppress;

            bool anyInside = false;
            bool handled = false;
            for (const auto& action : frame.bindings) {
                if (!isInsideZone(frame, action.binding)) {
                    continue;
                }

                anyInside = true;
                if (action.binding.suppress) {
                    toSuppress.push_back(action.binding);
                }
                triggerHapticOnce(action.binding.hand, frame.entryHaptic);

                if (!isCoolingDown() && !handled && vrcf::VRControllers.check(action.binding) && onActivated(action.binding)) {
                    triggerActivation(action.binding.hand, action.activateHaptic);
                    handled = true;
                }
            }

            applySuppressions(toSuppress);

            if (!anyInside) {
                _hapticFired = false;
            }

            // Resolve the sphere visual after the proximity test so WhenInside can react to it.
            const bool show = frame.showSphere == ActivationSphereVisibility::Always || (frame.showSphere == ActivationSphereVisibility::WhenInside && anyInside);
            updateVisual(frame.node, frame.sphereAttachNode, frame.zone, show, frame.sphereNif, frame.sphereScale);

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

        // --- Sphere visual. ---
        void updateVisual(const RE::NiNode* testNode, RE::NiNode* attachParent, const RE::NiTransform& zone, bool show, std::string_view nifName, float sphereScale);

        bool isCoolingDown() const;
        static bool containsSuppression(std::span<const vrcf::InputBinding> bindings, const vrcf::InputBinding& binding);
        static bool sameSuppressionInput(const vrcf::InputBinding& lhs, const vrcf::InputBinding& rhs);

        const char* _sphereKey;
        std::uint64_t _cooldownMs;
        std::uint64_t _lastActivationTime = 0;
        RE::NiPointer<RE::NiNode> _sphereNode; // sphere visual only; null until the visual is first shown
        std::string _sphereNifName; // the .nif last loaded into _sphereNode (or last attempted); detects a runtime swap
        std::vector<vrcf::InputBinding> _suppressedBindings; // bindings currently suppressed under _sphereKey
        bool _hapticFired = false;

        static constexpr float SPHERE_NIF_BASE_RADIUS = 0.5f;
    };
}
