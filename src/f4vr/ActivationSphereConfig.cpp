#include "ActivationSphereConfig.h"

#include "common/CommonUtils.h"

namespace f4cf::f4vr
{
    namespace
    {
        // The icon drawn when a sphere's style names none.
        constexpr auto DEFAULT_ICON_TEXTURE = "f4cf\\activation-icon-hand.dds";
    }

    const std::string& ActivationIconStyle::textureOrDefault() const
    {
        static const std::string defaultTexture = DEFAULT_ICON_TEXTURE;
        return texture.empty() ? defaultTexture : texture;
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

    /**
     * Case-insensitive name -> ActivationBindingIcons, tolerating separators. Empty or unknown returns `fallback`
     * (unknown is logged).
     */
    ActivationBindingIcons parseActivationBindingIcons(const std::string_view text, const ActivationBindingIcons fallback)
    {
        const std::string key = common::normalizeConfigToken(text);

        if (key.empty()) {
            return fallback;
        }
        if (key == "all" || key == "both") {
            return ActivationBindingIcons::All;
        }
        if (key == "primary") {
            return ActivationBindingIcons::Primary;
        }
        if (key == "secondary") {
            return ActivationBindingIcons::Secondary;
        }
        if (key == "inside" || key == "wheninside" || key == "hand") {
            return ActivationBindingIcons::Inside;
        }
        logger::warn("parseActivationBindingIcons: unrecognized value '{}' - using fallback", key);
        return fallback;
    }
}
