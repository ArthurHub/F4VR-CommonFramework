#include "Config.h"

using namespace common;

namespace
{
    const char* DEFAULT_SECTION = Version::PROJECT.data();
}

namespace my_mod
{
    void Config::loadConfigInternal(const toml::table& config)
    {
        // TODO: load config values; value_or returns the default for a key that is missing or of another type
        myConfigValue = config[DEFAULT_SECTION]["fMyConfigValue"].value_or(3.0f);
    }
}
