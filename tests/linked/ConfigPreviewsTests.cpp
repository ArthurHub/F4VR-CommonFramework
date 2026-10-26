#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "TestFiles.h"
#include "common/CommonUtils.h"
#include "common/TomlEditor.h"
#include "config/ConfigBase.h"
#include "modsconfig/ConfigPreviews.h"

using f4cf::modsconfig::ConfigPreviews;
using mods_config::api::Change;
using test_files::readFile;
using test_files::TempFolder;
using test_files::writeFile;

namespace
{
    // the resource of the default config in this executable, see Resources.rc
    constexpr WORD DEFAULT_CONFIG_RESOURCE_ID = 101;

    constexpr auto USER_FILE = "[Main]\nfScale = 1.0\niMode = 0\n\n[Debug]\niVersion = 2\n";

    /**
     * A mod's config with two values of its own, as ConfigBaseTests.cpp has it.
     */
    class PreviewedConfig : public f4cf::ConfigBase
    {
    public:
        explicit PreviewedConfig(const std::string& configFilePath)
            : ConfigBase("F4CFTests", configFilePath, DEFAULT_CONFIG_RESOURCE_ID)
        {}

        using ConfigBase::loadConfigValues;

        float scale = 0;
        int mode = 0;

    protected:
        virtual void loadConfigInternal(const toml::table& config) override
        {
            scale = config["Main"]["fScale"].value_or(0.0f);
            mode = config["Main"]["iMode"].value_or(0);
        }
    };

    /**
     * Whether an owner has an override of a key of the config.
     */
    bool hasOverrideOf(const f4cf::ConfigBase& config, const std::string& owner, const std::string& key)
    {
        return std::ranges::any_of(config.getConfigOverrides(), [&](const f4cf::config::AppliedOverride& applied) {
            return applied.owner == owner && applied.key == key;
        });
    }
}

TEST_CASE("ConfigPreviews: a preview of a setting of the mod's config file overrides it until its discard, and a Save clears it")
{
    const TempFolder folder;
    const auto path = folder.file("TestMod.toml");
    writeFile(path, USER_FILE);
    PreviewedConfig config(path);
    REQUIRE(config.loadConfigValues());
    ConfigPreviews previews(config);

    // the menu names the file in its own case and separators
    auto filePath = f4cf::common::str_tolower(path);
    std::ranges::replace(filePath, '\\', '/');
    const Change scale{ .id = "fScale", .fileName = "config", .filePath = filePath, .path = { "Main", "fScale" }, .previous = 1.0f, .value = 2.5f };

    previews.onChange(scale);
    CHECK(config.scale == 2.5f);
    CHECK(hasOverrideOf(config, ConfigPreviews::OWNER, "fScale"));
    CHECK(readFile(path) == USER_FILE);

    auto discard = scale;
    discard.discard = true;
    discard.value = 1.0f;
    previews.onChange(discard);
    CHECK(config.scale == 1.0f);
    CHECK_FALSE(config.hasConfigOverride("Main", "fScale"));

    SECTION("a setting of another file, and one of the game, is not previewed")
    {
        auto other = scale;
        other.filePath = folder.file("Other.toml");
        previews.onChange(other);
        auto global = scale;
        global.fileName = {};
        global.filePath = {};
        global.path = {};
        global.form = "MyMod.esp|800";
        previews.onChange(global);
        CHECK(config.scale == 1.0f);
        CHECK(config.getConfigOverrides().empty());
    }

    SECTION("a Save clears the overrides of what it wrote, and the file's values apply at once, with another owner's override kept")
    {
        previews.onChange(scale);
        REQUIRE(config.setConfigOverride("devbench", "Main", "iMode", 3));
        // Mods Config FO4 VR writes the file before it tells the Save
        f4cf::common::TomlEditor editor(readFile(path));
        REQUIRE(editor.set({ "Main", "fScale" }, 2.5f));
        writeFile(path, editor.text());

        previews.onSaved({ .settings = { scale }, .files = {}, .revision = 1 });
        CHECK(config.scale == 2.5f);
        CHECK(config.mode == 3);
        CHECK_FALSE(hasOverrideOf(config, ConfigPreviews::OWNER, "fScale"));
        CHECK(hasOverrideOf(config, "devbench", "iMode"));
    }
}
