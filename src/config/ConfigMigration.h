#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

#include "common/TomlEditor.h"

// The move of a user's values into the config of a newer version, and of a mod's INI config into TOML, apart from the
// game: plain std, toml++ and SimpleIni only, so it is unit tested.
namespace f4cf::config
{
    // where a config has its version, which its migrations count by: [Debug] iVersion
    constexpr auto VERSION_SECTION = "Debug";
    constexpr auto VERSION_KEY = "iVersion";

    /**
     * What importIniFile did with a mod's INI config.
     */
    struct ImportedIni
    {
        // the INI's own version, which names its backup
        int version = 0;
        // where the INI is kept
        std::filesystem::path backup;
        // why the INI was not moved to its backup, empty when it was
        std::string backupError;
    };

    /**
     * What a migration did with one value of the user's config.
     */
    struct MigratedValue
    {
        enum class Outcome : std::uint8_t
        {
            // set in the new config in place of its default
            Carried,
            // the new config has the same value
            Unchanged,
            // the new config does not have the key, and the value is dropped
            Removed,
            // the new config has the key with a value of another type, or the value is an array of [[tables]]: it is
            // left to the mod's own migration, see ConfigBase::updateConfigToLatestVersionCustom
            NotCarried,
        };

        // the keys from the section to the value, "Main", "fScale"
        std::vector<std::string> path;
        // the user's value as TOML text
        std::string value;
        Outcome outcome = Outcome::Unchanged;

        std::string pathText() const;
    };

    std::vector<MigratedValue> migrateValues(const toml::table& old, common::TomlEditor& latest);

    std::expected<std::string, std::string> iniToToml(std::string_view iniText, const toml::table& like);

    std::expected<ImportedIni, std::string> importIniFile(const std::filesystem::path& iniPath, const std::filesystem::path& tomlPath, const toml::table& like);

    std::filesystem::path backupPathOf(const std::filesystem::path& path, int version);
}
