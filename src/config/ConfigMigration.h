#pragma once

#include <cstdint>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

#include "common/TomlEditor.h"

// The move of a user's values into the config of a newer version, apart from the game: plain std and toml++ only, so
// it is unit tested.
namespace f4cf::config
{
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
}
