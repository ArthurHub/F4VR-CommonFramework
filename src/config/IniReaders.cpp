#include "IniReaders.h"

#include "IniValueParsers.h"
#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/WandActivationSphere.h"
#include "render/PrimitiveDraw.h"
#include "vrcf/InputBindingParser.h"
#include "vrcf/VRControllersHaptic.h"

namespace
{
    /**
     * Read how an activation sphere looks from its INI section. A preset (sSphereStyle) replaces `fallback`'s look
     * but not its size, then each key that is present overrides one value: sSphereNif, sSphereTexture ("none" keeps
     * the mesh's own), sSphereColor ("r,g,b[,a]" in 0..255), fSphereGlow (0..1), sSphereFalloff ("center,rim"
     * opacities) and fSphereScale (> 0). A malformed or out-of-range value is logged and ignored.
     */
    f4cf::f4vr::SphereStyle readSphereStyle(const CSimpleIniA& ini, const char* section, const f4cf::f4vr::SphereStyle& fallback)
    {
        auto style = fallback;

        const std::string presetName = ini.GetValue(section, "sSphereStyle", "");
        if (!presetName.empty()) {
            if (const auto preset = f4cf::f4vr::findSphereStylePreset(presetName)) {
                style = *preset;
                style.scale = fallback.scale;
            } else {
                logger::warn("Config: unknown sphere style '{}.sSphereStyle' = '{}'. Using default.", section, presetName);
            }
        }

        if (const std::string nif = ini.GetValue(section, "sSphereNif", ""); !nif.empty()) {
            style.nif = nif;
        }
        if (const std::string texture = ini.GetValue(section, "sSphereTexture", ""); !texture.empty()) {
            style.texture = f4cf::common::normalizeConfigToken(texture) == "none" ? "" : texture;
        }
        if (const char* rawColor = ini.GetValue(section, "sSphereColor", nullptr); rawColor && *rawColor) {
            if (!f4cf::config::parseColor255(rawColor, style.color)) {
                logger::warn("Config: malformed sphere color for '{}.sSphereColor' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Ignored.", section, rawColor);
            }
        }
        if (const char* rawGlow = ini.GetValue(section, "fSphereGlow", nullptr); rawGlow && *rawGlow) {
            const auto glow = static_cast<float>(ini.GetDoubleValue(section, "fSphereGlow", -1.0));
            if (glow >= 0.0f && glow <= 1.0f) {
                style.glow = glow;
            } else {
                logger::warn("Config: sphere glow '{}.fSphereGlow' = '{}' is out of range (expected 0..1). Ignored.", section, rawGlow);
            }
        }
        if (const char* rawFalloff = ini.GetValue(section, "sSphereFalloff", nullptr); rawFalloff && *rawFalloff) {
            float center, rim;
            if (std::sscanf(rawFalloff, " %f , %f", &center, &rim) == 2) {
                style.centerOpacity = center;
                style.rimOpacity = rim;
            } else {
                logger::warn("Config: malformed sphere falloff for '{}.sSphereFalloff' = '{}' (expected 'center,rim' opacities in 0..1). Ignored.", section, rawFalloff);
            }
        }
        if (const char* rawScale = ini.GetValue(section, "fSphereScale", nullptr); rawScale && *rawScale) {
            const auto scale = static_cast<float>(ini.GetDoubleValue(section, "fSphereScale", -1.0));
            if (scale > 0.0f) {
                style.scale = scale;
            } else {
                logger::warn("Config: sphere scale '{}.fSphereScale' = '{}' is out of range (expected > 0). Ignored.", section, rawScale);
            }
        }
        return style;
    }

    /**
     * Read how an activation sphere's icon looks from its INI section, each key that is present overriding one
     * value of `fallback`: sIcon (a .dds path), sIconColor ("r,g,b[,a]" in 0..255) and fIconSize (the longer side
     * in game units, > 0). A malformed or out-of-range value is logged and ignored.
     */
    f4cf::f4vr::ActivationIconStyle readIconStyle(const CSimpleIniA& ini, const char* section, const f4cf::f4vr::ActivationIconStyle& fallback)
    {
        auto style = fallback;

        if (const std::string texture = ini.GetValue(section, "sIcon", ""); !texture.empty()) {
            style.texture = texture;
        }
        if (const char* rawColor = ini.GetValue(section, "sIconColor", nullptr); rawColor && *rawColor) {
            if (!f4cf::config::parseColor255(rawColor, style.color)) {
                logger::warn("Config: malformed icon color for '{}.sIconColor' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Ignored.", section, rawColor);
            }
        }
        if (const char* rawSize = ini.GetValue(section, "fIconSize", nullptr); rawSize && *rawSize) {
            const auto size = static_cast<float>(ini.GetDoubleValue(section, "fIconSize", -1.0));
            if (size > 0.0f) {
                style.size = size;
            } else {
                logger::warn("Config: icon size '{}.fIconSize' = '{}' is out of range (expected > 0). Ignored.", section, rawSize);
            }
        }
        return style;
    }
}

namespace f4cf::config
{
    /**
     * Parse an NiTransform from "x,y,z;heading,roll,attitude;scale" (rotation in degrees), see parseTransform.
     * Returns defaultValue if the key is missing or the value is malformed.
     */
    RE::NiTransform IniReaders::getTransformValue(const CSimpleIniA& ini, const char* section, const char* key, const RE::NiTransform& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr) {
            return defaultValue;
        }

        const auto parsed = parseTransform(raw);
        if (!parsed) {
            logger::warn("Config: malformed transform value for '{}.{}' = '{}' (expected 'x,y,z;heading,roll,attitude;scale' in degrees). Using default.", section, key, raw);
            return defaultValue;
        }

        RE::NiTransform result;
        result.translate.x = parsed->x;
        result.translate.y = parsed->y;
        result.translate.z = parsed->z;
        result.rotate = common::MatrixUtils::getMatrixFromEulerAnglesDegrees(parsed->heading, parsed->roll, parsed->attitude);
        result.scale = parsed->scale;
        return result;
    }

    /**
     * Parse a color from "r,g,b" or "r,g,b,a", each a whole number in 0..255, where a is the opacity. Without
     * the a, the default's opacity stays. See parseColor255.
     * Returns defaultValue if the key is missing or empty, and with a warning if the value is malformed.
     */
    render::Color IniReaders::getColorValue(const CSimpleIniA& ini, const char* section, const char* key, const render::Color& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr || *raw == '\0') {
            return defaultValue;
        }

        std::array<float, 4> channels{ defaultValue.r, defaultValue.g, defaultValue.b, defaultValue.a };
        if (!parseColor255(raw, channels)) {
            logger::warn("Config: malformed color value for '{}.{}' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Using default.", section, key, raw);
            return defaultValue;
        }
        return { channels[0], channels[1], channels[2], channels[3] };
    }

    /**
     * Parse a 22-float hand pose from the INI value at section/key, see parseHandPose for its format.
     * Returns defaultValue (and logs a warning) if the key is missing or the structure doesn't match.
     */
    std::array<float, 22> IniReaders::getHandPoseValue(const CSimpleIniA& ini, const char* section, const char* key, const std::array<float, 22>& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr) {
            return defaultValue;
        }

        const auto parsed = parseHandPose(raw);
        if (!parsed) {
            logger::warn(
                "Config: malformed hand pose value for '{}.{}' = '{}' "
                "(expected 'p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;pp,py'). Using default.",
                section,
                key,
                raw);
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Parse a controller input binding from the INI value at section/key.
     * Delegates to vrcf::parseInputBinding (see vrcf/InputBindingParser.h for the grammar).
     * Returns defaultValue (and logs a warning) if the key is missing or the value is malformed.
     */
    vrcf::InputBinding IniReaders::getInputBindingValue(const CSimpleIniA& ini, const char* section, const char* key, const vrcf::InputBinding& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr) {
            return defaultValue;
        }

        const auto parsed = vrcf::parseInputBinding(raw);
        if (!parsed) {
            logger::warn("Config: malformed input binding for '{}.{}' = '{}'. Using default.", section, key, raw);
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Read the whole INI section of an activation sphere into a f4vr::WandActivationConfig, each key falling back
     * to the matching field of `defaults`: the zone (tZone, and tZonePA for power armor), the two bindings
     * (sPrimaryBinding, sSecondaryBinding), the haptics (sEntryHaptic, sPrimaryHaptic, sSecondaryHaptic), the
     * sphere (sShowSphere, sSphereOrientation, and its look, see readSphereStyle) and the icon (sShowIcon, and
     * its look, see readIconStyle).
     */
    f4vr::WandActivationConfig IniReaders::loadWandActivationConfig(const CSimpleIniA& ini, const char* section, const f4vr::WandActivationConfig& defaults)
    {
        f4vr::WandActivationConfig cfg;
        cfg.zone = getTransformValue(ini, section, "tZone", defaults.zone);
        cfg.zonePA = ini.GetValue(section, "tZonePA", nullptr) ? std::optional{ getTransformValue(ini, section, "tZonePA", cfg.zone) } : defaults.zonePA;
        cfg.primary = getInputBindingValue(ini, section, "sPrimaryBinding", defaults.primary);
        cfg.secondary = getInputBindingValue(ini, section, "sSecondaryBinding", defaults.secondary);

        const char* rawEntry = ini.GetValue(section, "sEntryHaptic", nullptr);
        cfg.entryHaptic = rawEntry ? vrcf::parseHapticPattern(rawEntry) : defaults.entryHaptic;
        const char* rawPrimaryHaptic = ini.GetValue(section, "sPrimaryHaptic", nullptr);
        cfg.primaryHaptic = rawPrimaryHaptic ? vrcf::parseHapticPattern(rawPrimaryHaptic) : defaults.primaryHaptic;
        const char* rawSecondaryHaptic = ini.GetValue(section, "sSecondaryHaptic", nullptr);
        cfg.secondaryHaptic = rawSecondaryHaptic ? vrcf::parseHapticPattern(rawSecondaryHaptic) : defaults.secondaryHaptic;

        cfg.showSphere = f4vr::parseActivationSphereVisibility(ini.GetValue(section, "sShowSphere", ""), defaults.showSphere);

        cfg.sphereStyle = readSphereStyle(ini, section, defaults.sphereStyle);
        cfg.sphereOrientation = f4vr::parseActivationSphereOrientation(ini.GetValue(section, "sSphereOrientation", ""), defaults.sphereOrientation);

        cfg.showIcon = f4vr::parseActivationSphereVisibility(ini.GetValue(section, "sShowIcon", ""), defaults.showIcon);
        cfg.iconStyle = readIconStyle(ini, section, defaults.iconStyle);
        return cfg;
    }
}
