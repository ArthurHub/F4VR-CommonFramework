#include "Config.h"

#include "common/MatrixUtils.h"

namespace
{
    const char* DEFAULT_SECTION = Version::PROJECT.data();
    constexpr auto HEAD_ACTIVATION_SECTION = "MyMod_HeadActivation";

    /**
     * The head gesture's defaults, for a key its section leaves out: a zone just in front of the eyes, the offhand's
     * trigger tapped or held there, and nothing drawn, as the zone fills the view.
     */
    f4vr::WandActivationConfig headActivationDefaults()
    {
        auto zone = common::MatrixUtils::getTransform(0.28f, 6.74f, 4.15f, 0.0f, 0.0f, 0.0f);
        zone.scale = 18.0f;
        return {
            .zone = zone,
            .primary = { .hand = vrcf::Hand::Offhand, .type = vrcf::ActivationType::Tap, .button = vr::k_EButton_SteamVR_Trigger, .suppress = true },
            .secondary = { .hand = vrcf::Hand::Offhand, .type = vrcf::ActivationType::LongPress, .button = vr::k_EButton_SteamVR_Trigger, .suppress = true },
            .showIcon = f4vr::ActivationSphereVisibility::Never,
        };
    }
}

namespace my_mod
{
    void Config::loadConfigInternal(const toml::table& config)
    {
        // TODO: read the mod's values; value_or gives the default for a key that is missing or of another type
        myConfigValue = config[DEFAULT_SECTION]["fMyConfigValue"].value_or(3.0f);
        headActivation = loadWandActivationConfig(config, HEAD_ACTIVATION_SECTION, headActivationDefaults());
    }
}
