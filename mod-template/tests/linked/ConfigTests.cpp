#include <catch2/catch_test_macros.hpp>

#include "Config.h"
#include "common/CommonUtils.h"

// The mod's config as it ships: data/config/MyMod.toml, embedded in this executable as in the mod's DLL.

TEST_CASE("Config: the shipped config loads with its values")
{
    my_mod::Config config;

    // the embedded default alone, with no file in the user's Documents read or written
    config.loadEmbeddedDefaultOnly();

    REQUIRE(f4cf::common::fEqual(config.myConfigValue, 3.0f));

    // the head gesture's section, read as the framework reads an activation sphere
    CHECK(config.headActivation.section == "MyMod_HeadActivation");
    CHECK(f4cf::common::fEqual(config.headActivation.zone.scale, 18.0f));
    CHECK(config.headActivation.secondary.type == f4cf::vrcf::ActivationType::LongPress);
}
