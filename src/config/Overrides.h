#pragma once

#include <SimpleIni.h>
#include <compare>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "IniValue.h"

// The session overrides of a mod's config, apart from the game: plain std and SimpleIni only, so it is unit tested.
namespace f4cf::config
{
    /**
     * The INI section and key of a value.
     */
    struct IniKey
    {
        std::string section;
        std::string key;

        auto operator<=>(const IniKey&) const = default;
    };

    /**
     * A session override to set: the value that applies in place of the file's for a section and key, see
     * ConfigBase::setConfigOverrides.
     */
    struct IniOverride
    {
        std::string section;
        std::string key;
        IniValue value;
    };

    /**
     * An override that applies: the value in place of the file's for a section and key, in its INI string
     * form, and the owner that set it.
     */
    struct AppliedOverride
    {
        std::string section;
        std::string key;
        std::string value;
        std::string owner;
    };

    /**
     * The session overrides of a mod's config, each under the name of the owner that set it, as several callers
     * override one config. An owner sets and clears only its own, and of two on one key the one set last applies.
     * Every call is under the class's own lock: overrides are set on one thread and applied on another.
     */
    class Overrides
    {
    public:
        bool set(const std::string& owner, const IniKey& key, std::string value);
        std::vector<AppliedOverride> set(const std::string& owner, const std::vector<IniOverride>& overrides);
        bool clear(const std::string& owner, const IniKey& key);
        std::vector<IniKey> clear(const std::string& owner, const std::vector<IniKey>& keys);
        std::size_t clearAll(const std::string& owner);

        std::optional<std::string> find(const IniKey& key) const;
        std::vector<AppliedOverride> applied() const;
        void applyTo(CSimpleIniA& ini) const;

    private:
        struct Entry
        {
            std::string owner;
            std::string value;
        };

        bool setEntry(const std::string& owner, const IniKey& key, std::string value);
        bool clearEntry(const std::string& owner, const IniKey& key);

        // The overrides of each overridden key in the order they were set, so the last one applies. An owner has
        // one for a key at most, and a key with none is not in the map.
        std::map<IniKey, std::vector<Entry>> _entries;
        mutable std::mutex _mutex;
    };
}
