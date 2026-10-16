#include "ConfigReaders.h"

#include "Overrides.h"
#include "ValueParsers.h"
#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "f4vr/WandActivationSphere.h"
#include "render/PrimitiveDraw.h"
#include "vrcf/InputBindingParser.h"
#include "vrcf/VRControllersHaptic.h"

namespace
{
    /**
     * The node of a section's key, or null when the config does not have it. The key can be a value in a nested
     * table, see config::ConfigKey.
     */
    const toml::node* nodeOf(const toml::table& config, const char* section, const char* key)
    {
        return f4cf::config::nodeOf(config, { section, key });
    }

    /**
     * The string of a section's key. No value when the key is missing, and with a warning when it is not a string,
     * as a value of more than one word written without its quotes would be.
     */
    std::optional<std::string> stringOf(const toml::table& config, const char* section, const char* key)
    {
        const auto* node = nodeOf(config, section, key);
        if (!node) {
            return std::nullopt;
        }
        if (const auto* string = node->as_string()) {
            return string->get();
        }
        logger::warn("Config: '{}.{}' is not a string, the text of it goes in quotes. Using default.", section, key);
        return std::nullopt;
    }

    /**
     * The number of a section's key, an integer or a float. No value when the key is missing, and with a warning
     * when it is not a number.
     */
    std::optional<float> numberOf(const toml::table& config, const char* section, const char* key)
    {
        const auto* node = nodeOf(config, section, key);
        if (!node) {
            return std::nullopt;
        }
        if (const auto number = node->value<double>()) {
            return static_cast<float>(*number);
        }
        logger::warn("Config: '{}.{}' is not a number. Using default.", section, key);
        return std::nullopt;
    }

    /**
     * Read how an activation sphere looks from its section. A preset (sSphereStyle) replaces `fallback`'s look but
     * not its size, then each key that is present overrides one value: sSphereNif, sSphereTexture ("none" keeps the
     * mesh's own), sSphereColor ("r,g,b[,a]" in 0..255), fSphereGlow (0..1), sSphereFalloff ("center,rim" opacities)
     * and fSphereScale (> 0). A malformed or out-of-range value is logged and ignored.
     */
    f4cf::f4vr::SphereStyle readSphereStyle(const toml::table& config, const char* section, const f4cf::f4vr::SphereStyle& fallback)
    {
        auto style = fallback;

        if (const auto presetName = stringOf(config, section, "sSphereStyle"); presetName && !presetName->empty()) {
            if (const auto preset = f4cf::f4vr::findSphereStylePreset(*presetName)) {
                style = *preset;
                style.scale = fallback.scale;
            } else {
                logger::warn("Config: unknown sphere style '{}.sSphereStyle' = '{}'. Using default.", section, *presetName);
            }
        }

        if (const auto nif = stringOf(config, section, "sSphereNif"); nif && !nif->empty()) {
            style.nif = *nif;
        }
        if (const auto texture = stringOf(config, section, "sSphereTexture"); texture && !texture->empty()) {
            style.texture = f4cf::common::normalizeConfigToken(*texture) == "none" ? "" : *texture;
        }
        if (const auto color = stringOf(config, section, "sSphereColor"); color && !color->empty()) {
            if (!f4cf::config::parseColor255(*color, style.color)) {
                logger::warn("Config: malformed sphere color for '{}.sSphereColor' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Ignored.", section, *color);
            }
        }
        if (const auto glow = numberOf(config, section, "fSphereGlow")) {
            if (*glow >= 0.0f && *glow <= 1.0f) {
                style.glow = *glow;
            } else {
                logger::warn("Config: sphere glow '{}.fSphereGlow' = {} is out of range (expected 0..1). Ignored.", section, *glow);
            }
        }
        if (const auto falloff = stringOf(config, section, "sSphereFalloff"); falloff && !falloff->empty()) {
            float center, rim;
            if (std::sscanf(falloff->c_str(), " %f , %f", &center, &rim) == 2) {
                style.centerOpacity = center;
                style.rimOpacity = rim;
            } else {
                logger::warn("Config: malformed sphere falloff for '{}.sSphereFalloff' = '{}' (expected 'center,rim' opacities in 0..1). Ignored.", section, *falloff);
            }
        }
        if (const auto scale = numberOf(config, section, "fSphereScale")) {
            if (*scale > 0.0f) {
                style.scale = *scale;
            } else {
                logger::warn("Config: sphere scale '{}.fSphereScale' = {} is out of range (expected > 0). Ignored.", section, *scale);
            }
        }
        return style;
    }

    /**
     * Read how an activation sphere's icon looks from its section, each key that is present overriding one value of
     * `fallback`: sIcon (a .dds path), sIconColor ("r,g,b[,a]" in 0..255) and fIconSize (the longer side in game
     * units, > 0). A malformed or out-of-range value is logged and ignored.
     */
    f4cf::f4vr::ActivationIconStyle readIconStyle(const toml::table& config, const char* section, const f4cf::f4vr::ActivationIconStyle& fallback)
    {
        auto style = fallback;

        if (const auto texture = stringOf(config, section, "sIcon"); texture && !texture->empty()) {
            style.texture = *texture;
        }
        if (const auto color = stringOf(config, section, "sIconColor"); color && !color->empty()) {
            if (!f4cf::config::parseColor255(*color, style.color)) {
                logger::warn("Config: malformed icon color for '{}.sIconColor' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Ignored.", section, *color);
            }
        }
        if (const auto size = numberOf(config, section, "fIconSize")) {
            if (*size > 0.0f) {
                style.size = *size;
            } else {
                logger::warn("Config: icon size '{}.fIconSize' = {} is out of range (expected > 0). Ignored.", section, *size);
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
    RE::NiTransform ConfigReaders::getTransformValue(const toml::table& config, const char* section, const char* key, const RE::NiTransform& defaultValue)
    {
        const auto raw = stringOf(config, section, key);
        if (!raw) {
            return defaultValue;
        }

        const auto parsed = parseTransform(raw->c_str());
        if (!parsed) {
            logger::warn("Config: malformed transform value for '{}.{}' = '{}' (expected 'x,y,z;heading,roll,attitude;scale' in degrees). Using default.", section, key, *raw);
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
    render::Color ConfigReaders::getColorValue(const toml::table& config, const char* section, const char* key, const render::Color& defaultValue)
    {
        const auto raw = stringOf(config, section, key);
        if (!raw || raw->empty()) {
            return defaultValue;
        }

        std::array<float, 4> channels{ defaultValue.r, defaultValue.g, defaultValue.b, defaultValue.a };
        if (!parseColor255(*raw, channels)) {
            logger::warn("Config: malformed color value for '{}.{}' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Using default.", section, key, *raw);
            return defaultValue;
        }
        return { channels[0], channels[1], channels[2], channels[3] };
    }

    /**
     * Read an array of strings, such as ["skelly", "perf"], in its order.
     * Returns defaultValue if the key is missing, and with a warning if it is not an array. An item of the array that
     * is not a string is skipped with a warning.
     */
    std::vector<std::string> ConfigReaders::getStringListValue(const toml::table& config, const char* section, const char* key, const std::vector<std::string>& defaultValue)
    {
        const auto* node = nodeOf(config, { section, key });
        if (!node) {
            return defaultValue;
        }
        const auto* array = node->as_array();
        if (!array) {
            logger::warn("Config: '{}.{}' is not an array, write it as [\"one\", \"two\"]. Using default.", section, key);
            return defaultValue;
        }

        std::vector<std::string> list;
        list.reserve(array->size());
        for (const auto& item : *array) {
            if (const auto* string = item.as_string()) {
                list.push_back(string->get());
            } else {
                logger::warn("Config: '{}.{}' has an item that is not a string, it goes in quotes. Skipped.", section, key);
            }
        }
        return list;
    }

    /**
     * Parse a 22-float hand pose from the value at section/key, see parseHandPose for its format.
     * Returns defaultValue (and logs a warning) if the key is missing or the structure doesn't match.
     */
    std::array<float, 22> ConfigReaders::getHandPoseValue(const toml::table& config, const char* section, const char* key, const std::array<float, 22>& defaultValue)
    {
        const auto raw = stringOf(config, section, key);
        if (!raw) {
            return defaultValue;
        }

        const auto parsed = parseHandPose(raw->c_str());
        if (!parsed) {
            logger::warn(
                "Config: malformed hand pose value for '{}.{}' = '{}' "
                "(expected 'p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;pp,py'). Using default.",
                section,
                key,
                *raw);
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Parse a controller input binding from the value at section/key.
     * Delegates to vrcf::parseInputBinding (see vrcf/InputBindingParser.h for the grammar).
     * Returns defaultValue if the key is missing, and with a warning that says what is wrong if the value is malformed.
     */
    vrcf::InputBinding ConfigReaders::getInputBindingValue(const toml::table& config, const char* section, const char* key, const vrcf::InputBinding& defaultValue)
    {
        const auto raw = stringOf(config, section, key);
        if (!raw) {
            return defaultValue;
        }

        const auto parsed = vrcf::parseInputBinding(*raw);
        if (!parsed) {
            logger::warn("Config: input binding '{}.{}' = '{}' is not valid: {}. Using default.", section, key, *raw, parsed.error());
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Read the whole section of an activation sphere into a f4vr::WandActivationConfig, each key falling back to the
     * matching field of `defaults`: the zone (tZone, and tZonePA for power armor), the two bindings (sPrimaryBinding,
     * sSecondaryBinding), the haptics (sEntryHaptic, sPrimaryHaptic, sSecondaryHaptic), the sphere (sShowSphere,
     * sSphereOrientation, and its look, see readSphereStyle) and the icon (sShowIcon, and its look, see readIconStyle).
     */
    f4vr::WandActivationConfig ConfigReaders::loadWandActivationConfig(const toml::table& config, const char* section, const f4vr::WandActivationConfig& defaults)
    {
        f4vr::WandActivationConfig cfg;
        cfg.zone = getTransformValue(config, section, "tZone", defaults.zone);
        cfg.zonePA = nodeOf(config, { section, "tZonePA" }) ? std::optional{ getTransformValue(config, section, "tZonePA", cfg.zone) } : defaults.zonePA;
        cfg.primary = getInputBindingValue(config, section, "sPrimaryBinding", defaults.primary);
        cfg.secondary = getInputBindingValue(config, section, "sSecondaryBinding", defaults.secondary);

        const auto entryHaptic = stringOf(config, section, "sEntryHaptic");
        cfg.entryHaptic = entryHaptic ? vrcf::parseHapticPattern(*entryHaptic) : defaults.entryHaptic;
        const auto primaryHaptic = stringOf(config, section, "sPrimaryHaptic");
        cfg.primaryHaptic = primaryHaptic ? vrcf::parseHapticPattern(*primaryHaptic) : defaults.primaryHaptic;
        const auto secondaryHaptic = stringOf(config, section, "sSecondaryHaptic");
        cfg.secondaryHaptic = secondaryHaptic ? vrcf::parseHapticPattern(*secondaryHaptic) : defaults.secondaryHaptic;

        cfg.showSphere = f4vr::parseActivationSphereVisibility(stringOf(config, section, "sShowSphere").value_or(""), defaults.showSphere);

        cfg.sphereStyle = readSphereStyle(config, section, defaults.sphereStyle);
        cfg.sphereOrientation = f4vr::parseActivationSphereOrientation(stringOf(config, section, "sSphereOrientation").value_or(""), defaults.sphereOrientation);

        cfg.showIcon = f4vr::parseActivationSphereVisibility(stringOf(config, section, "sShowIcon").value_or(""), defaults.showIcon);
        cfg.iconStyle = readIconStyle(config, section, defaults.iconStyle);
        return cfg;
    }
}
