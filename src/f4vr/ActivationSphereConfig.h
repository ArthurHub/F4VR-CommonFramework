#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "SphereStyle.h"
#include "vrcf/VRControllersHaptic.h"
#include "vrcf/VRControllersManager.h"

// An activation sphere's config as values: what a mod reads from its config and Mods Config FO4 VR's menu edits, on
// both runtimes. The sphere that acts on it is WandActivationSphere, VR only.
namespace f4cf::f4vr
{
    /**
     * When one of the activation sphere's visuals — the sphere mesh or the icon — is drawn. Each visual has its own
     * setting, so e.g. the icon can mark where to reach while the sphere lights up only once the hand is inside.
     */
    enum class ActivationSphereVisibility : std::uint8_t
    {
        Never = 0, // never draw it
        Always, // always draw it (tuning / debug)
        WhenInside, // draw it only while a bound hand is inside the zone (a proximity hint)
        WhenAvailable, // draw it while at least one binding can fire this frame, i.e. while the gesture is available
    };

    /**
     * Parse an ActivationSphereVisibility from config text, case-insensitively: "never"/"off"/"false"
     * -> Never; "always"/"on"/"true" -> Always; "wheninside"/"inside"/"proximity"/"active" -> WhenInside;
     * "whenavailable"/"available" -> WhenAvailable. Empty or unrecognized text returns `fallback` (unrecognized is
     * logged).
     */
    ActivationSphereVisibility parseActivationSphereVisibility(std::string_view text, ActivationSphereVisibility fallback);

    /**
     * Which way the activation sphere's visual faces. The zone is a sphere, so only a patterned look (the debug
     * grid) shows the difference.
     */
    enum class ActivationSphereOrientation : std::uint8_t
    {
        Hmd = 0, // turn with the HMD's heading (kept upright), so it holds still in the player's view
        Body, // turn with the body's heading (the rendered skeleton's, kept upright)
        World, // hold still in the world
    };

    /**
     * Parse an ActivationSphereOrientation from config text, case-insensitively: "hmd"/"head" -> Hmd;
     * "body"/"skeleton" -> Body; "world" -> World. Empty or unrecognized text returns `fallback` (unrecognized is
     * logged).
     */
    ActivationSphereOrientation parseActivationSphereOrientation(std::string_view text, ActivationSphereOrientation fallback);

    /**
     * How an activation sphere's icon looks: a small image at the zone's center, turned to face the player and drawn
     * on top of the world. The image is best a white glyph on a transparent background, so the tint colors it.
     *
     * The texture resolves like any framework resource: as given, then under Data\, then under Data\Textures\, then
     * under this mod's own folder there — so "Textures\<ModName>\f4cf\activation-icon-hand.dds" and
     * "f4cf\activation-icon-hand.dds" both find Data\Textures\<ModName>\f4cf\activation-icon-hand.dds.
     */
    struct ActivationIconStyle
    {
        // The .dds drawn. Empty = the framework's hand icon.
        std::string texture;
        // Tint as r, g, b, a (0..1 each), multiplied into the image; its alpha is the overall opacity.
        render::Color color{ 1.0f, 1.0f, 1.0f, 0.9f };
        // The icon's longer side, in game units.
        float size = 1.0f;

        /**
         * The image to draw: `texture`, or the framework's hand icon when it is empty.
         */
        const std::string& textureOrDefault() const;

        bool operator==(const ActivationIconStyle&) const = default;
    };

    /**
     * Which of an activation sphere's bindings its binding icon shows. Only a binding fed enabled this frame shows,
     * whatever the choice, and two that look the same show once.
     */
    enum class ActivationBindingIcons : std::uint8_t
    {
        All = 0, // every binding fed enabled this frame, in their order
        Primary, // the first binding only
        Secondary, // the second binding only
        Inside, // only the bindings whose hand is inside the zone
    };

    /**
     * Parse an ActivationBindingIcons from config text, case-insensitively: "all"/"both" -> All; "primary" -> Primary;
     * "secondary" -> Secondary; "inside"/"wheninside"/"hand" -> Inside. Empty or unrecognized text returns `fallback`
     * (unrecognized is logged).
     */
    ActivationBindingIcons parseActivationBindingIcons(std::string_view text, ActivationBindingIcons fallback);

    /**
     * How an activation sphere's binding icon looks: the icons of the bindings that fire the gesture (see
     * vrui::bindingIconPaths), in a row facing the player, drawn on top of the world like the icon.
     *
     * The transform places the row in the plane that faces the HMD, measured from the zone's center in game units:
     * translate x is to the right, y up and z toward the HMD; its scale is each icon's longer side, and its rotation
     * is ignored. Positive x aligns the row's left edge there, so it grows away from an icon at the center, negative
     * x its right edge, and zero centers it. The offset is in view space, so it is never mirrored for handedness.
     */
    struct ActivationBindingIconStyle
    {
        ActivationBindingIcons bindings = ActivationBindingIcons::Inside;
        RE::NiTransform transform{};
        // Tint as r, g, b, a (0..1 each), multiplied into the icons; the shipped icons are white.
        render::Color color{ 1.0f, 1.0f, 1.0f, 0.9f };
    };

    /**
     * Authored, config-loadable description of one activation sphere: its zone (+ optional PA variant), its
     * two bindings (each carrying its own suppress flag), the entry haptic plus a per-binding activation
     * haptic, and when each visual (the sphere, the icon, the binding icon) is shown and how it looks. A mod parses one config section into this
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
        SphereStyle sphereStyle = getDefaultSphereStyle();
        ActivationSphereOrientation sphereOrientation = ActivationSphereOrientation::Body;
        ActivationSphereVisibility showIcon = ActivationSphereVisibility::WhenInside;
        ActivationIconStyle iconStyle{};
        ActivationSphereVisibility showBindingIcon = ActivationSphereVisibility::Never;
        ActivationBindingIconStyle bindingIconStyle{};

        // The config section it was read from, which Mods Config FO4 VR's menu names while its ray is on the sphere's
        // rows (see WandActivationSphere::isPointedAt). Empty for a config not read from a section.
        std::string section;

        /**
         * The zone to use for the given power-armor state: the PA variant when one was configured, otherwise
         * the regular zone.
         */
        const RE::NiTransform& zoneFor(const bool inPowerArmor) const
        {
            return inPowerArmor && zonePA ? *zonePA : zone;
        }
    };
}
