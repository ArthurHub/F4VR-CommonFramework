#include "ConfigReaders.h"

#include "Overrides.h"
#include "ValueParsers.h"
#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "common/TomlEditor.h"
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
     * Set the color of a section's key over `color`, each channel it does not have keeping `color`'s, see
     * config::parseColor. A key that is missing changes nothing, and one that is not a color is logged and leaves
     * `color` as it was.
     */
    void readColor(const toml::table& config, const char* section, const char* key, std::array<float, 4>& color)
    {
        const auto* node = nodeOf(config, section, key);
        if (!node) {
            return;
        }
        if (const auto parsed = f4cf::config::parseColor(*node, color); !parsed) {
            logger::warn("Config: malformed color for '{}.{}' = {}: {} (expected {{ rgb = [r, g, b], alpha = a }} in 0..255). Using default.",
                section,
                key,
                f4cf::common::TomlEditor::Value(*node).text(),
                parsed.error());
        }
    }

    /**
     * The two numbers of a section's array key, as [center, rim] for a sphere's falloff. No value when the key is
     * missing, and with a warning when it is not an array of two numbers.
     */
    std::optional<std::array<float, 2>> pairOf(const toml::table& config, const char* section, const char* key)
    {
        const auto* node = nodeOf(config, section, key);
        if (!node) {
            return std::nullopt;
        }
        const auto* array = node->as_array();
        if (array && array->size() == 2) {
            const auto first = (*array)[0].value<double>();
            const auto second = (*array)[1].value<double>();
            if (first && second) {
                return std::array{ static_cast<float>(*first), static_cast<float>(*second) };
            }
        }
        logger::warn("Config: '{}.{}' = {} is not an array of two numbers. Using default.", section, key, f4cf::common::TomlEditor::Value(*node).text());
        return std::nullopt;
    }

    /**
     * Read how an activation sphere looks from its section. A preset (sSphereStyle) replaces `fallback`'s look but
     * not its size, then each key that is present overrides one value: sSphereNif, sSphereTexture ("none" keeps the
     * mesh's own), cSphereColor ({ rgb = [r, g, b], alpha = a } in 0..255, each optional), fSphereGlow (0..1),
     * aSphereFalloff ([center, rim] opacities) and fSphereScale (> 0). A malformed or out-of-range value is logged
     * and ignored.
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
        readColor(config, section, "cSphereColor", style.color);
        if (const auto glow = numberOf(config, section, "fSphereGlow")) {
            if (*glow >= 0.0f && *glow <= 1.0f) {
                style.glow = *glow;
            } else {
                logger::warn("Config: sphere glow '{}.fSphereGlow' = {} is out of range (expected 0..1). Ignored.", section, *glow);
            }
        }
        if (const auto falloff = pairOf(config, section, "aSphereFalloff")) {
            style.centerOpacity = (*falloff)[0];
            style.rimOpacity = (*falloff)[1];
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
     * `fallback`: sIcon (a .dds path), cIconColor ({ rgb = [r, g, b], alpha = a } in 0..255, each optional) and
     * fIconSize (the longer side in game units, > 0). A malformed or out-of-range value is logged and ignored.
     */
    f4cf::f4vr::ActivationIconStyle readIconStyle(const toml::table& config, const char* section, const f4cf::f4vr::ActivationIconStyle& fallback)
    {
        auto style = fallback;

        if (const auto texture = stringOf(config, section, "sIcon"); texture && !texture->empty()) {
            style.texture = *texture;
        }
        readColor(config, section, "cIconColor", style.color);
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
     * Read an NiTransform from its inline table, { pos = [x, y, z], rot = [heading, roll, attitude], scale = s } with
     * the rotation in degrees and each key optional, see parseTransform.
     * Returns defaultValue if the key is missing, and with a warning if the value is malformed.
     */
    RE::NiTransform ConfigReaders::getTransformValue(const toml::table& config, const char* section, const char* key, const RE::NiTransform& defaultValue)
    {
        const auto* node = nodeOf(config, { section, key });
        if (!node) {
            return defaultValue;
        }

        const auto parsed = parseTransform(*node);
        if (!parsed) {
            logger::warn(
                "Config: malformed transform value for '{}.{}' = {}: {} (expected {{ pos = [x, y, z], rot = [heading, roll, attitude], scale = s }} in degrees). "
                "Using default.",
                section,
                key,
                common::TomlEditor::Value(*node).text(),
                parsed.error());
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
     * Read a color from its inline table, { rgb = [r, g, b], alpha = a } with each a whole number in 0..255, where
     * alpha is the opacity. Each key is optional, and one that is missing keeps the default's channels, see
     * parseColor.
     * Returns defaultValue if the key is missing, and with a warning if the value is malformed.
     */
    render::Color ConfigReaders::getColorValue(const toml::table& config, const char* section, const char* key, const render::Color& defaultValue)
    {
        std::array<float, 4> channels{ defaultValue.r, defaultValue.g, defaultValue.b, defaultValue.a };
        readColor(config, section, key, channels);
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
     * Read a 22-float hand pose from its inline table at section/key, { thumb = [prox, mid, dist, splay], index =
     * [..], middle = [..], ring = [..], pinky = [..], palm = [pitch, yaw] } with each key optional, see
     * parseHandPose.
     * Returns defaultValue if the key is missing, and with a warning if the value is malformed.
     */
    std::array<float, 22> ConfigReaders::getHandPoseValue(const toml::table& config, const char* section, const char* key, const std::array<float, 22>& defaultValue)
    {
        const auto* node = nodeOf(config, { section, key });
        if (!node) {
            return defaultValue;
        }

        const auto parsed = parseHandPose(*node);
        if (!parsed) {
            logger::warn(
                "Config: malformed hand pose value for '{}.{}' = {}: {} (expected {{ thumb = [prox, mid, dist, splay], index = .., middle = .., ring = .., "
                "pinky = .., palm = [pitch, yaw] }}). Using default.",
                section,
                key,
                common::TomlEditor::Value(*node).text(),
                parsed.error());
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Read a controller input binding from its inline table at section/key, { hand = "offhand", type = "tap",
     * button = "grip" }, see vrcf::parseInputBindingTable.
     * Returns defaultValue if the key is missing, and with a warning that says what is wrong if the value is malformed.
     */
    vrcf::InputBinding ConfigReaders::getInputBindingValue(const toml::table& config, const char* section, const char* key, const vrcf::InputBinding& defaultValue)
    {
        const auto* node = nodeOf(config, { section, key });
        if (!node) {
            return defaultValue;
        }

        const auto parsed = vrcf::parseInputBindingTable(*node);
        if (!parsed) {
            logger::warn("Config: input binding '{}.{}' = {} is not valid: {}. Using default.", section, key, common::TomlEditor::Value(*node).text(), parsed.error());
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Read the whole section of an activation sphere into a f4vr::WandActivationConfig, each key falling back to the
     * matching field of `defaults`: the zone (tZone, and tZonePA for power armor), the two bindings (kPrimaryBinding,
     * kSecondaryBinding), the haptics (sEntryHaptic, sPrimaryHaptic, sSecondaryHaptic), the sphere (sShowSphere,
     * sSphereOrientation, and its look, see readSphereStyle) and the icon (sShowIcon, and its look, see readIconStyle).
     */
    f4vr::WandActivationConfig ConfigReaders::loadWandActivationConfig(const toml::table& config, const char* section, const f4vr::WandActivationConfig& defaults)
    {
        f4vr::WandActivationConfig cfg;
        cfg.zone = getTransformValue(config, section, "tZone", defaults.zone);
        cfg.zonePA = nodeOf(config, { section, "tZonePA" }) ? std::optional{ getTransformValue(config, section, "tZonePA", cfg.zone) } : defaults.zonePA;
        cfg.primary = getInputBindingValue(config, section, "kPrimaryBinding", defaults.primary);
        cfg.secondary = getInputBindingValue(config, section, "kSecondaryBinding", defaults.secondary);

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
