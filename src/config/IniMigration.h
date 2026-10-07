#pragma once

#include <SimpleIni.h>
#include <cstdint>
#include <string>
#include <vector>

// The move of a user's values into the INI of a newer version, apart from the game: plain std and SimpleIni only,
// so it is unit tested.
namespace f4cf::config
{
    /**
     * What a migration did with one value of the user's INI.
     */
    struct MigratedValue
    {
        enum class Outcome : std::uint8_t
        {
            // set in the new INI in place of its default
            Carried,
            // the new INI has the same value
            Unchanged,
            // the new INI does not have the key, and the value is dropped
            Removed,
        };

        std::string section;
        std::string key;
        std::string value;
        Outcome outcome = Outcome::Unchanged;
    };

    std::vector<MigratedValue> migrateIniValues(const CSimpleIniA& oldIni, CSimpleIniA& newIni);
}
