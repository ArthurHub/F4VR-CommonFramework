#pragma once

#include <compare>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

#include "ConfigValue.h"

// The session overrides of a mod's config, apart from the game: plain std and toml++ only, so it is unit tested.
namespace f4cf::config
{
    /**
     * The section and key of a config value. Each is a TOML dotted key, so a value in a nested table is named by its
     * path: "Main" and "hand.x" for x in [Main.hand], or "g.x" for x in Main's inline table g. A name with a dot in
     * it is in quotes, as in TOML: "\"my.key\"". An empty section is the file's root.
     */
    struct ConfigKey
    {
        std::string section;
        std::string key;

        auto operator<=>(const ConfigKey&) const = default;

        std::optional<std::vector<std::string>> path() const;
        ConfigKey canonical() const;
    };

    /**
     * An override that applies: the value in place of the file's for a section and key, in its text form, and the
     * owner that set it.
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
        bool set(const std::string& owner, const ConfigKey& key, std::string value);
        std::vector<AppliedOverride> set(const std::string& owner, const std::vector<ConfigEntry>& overrides);
        bool clear(const std::string& owner, const ConfigKey& key);
        std::vector<ConfigKey> clear(const std::string& owner, const std::vector<ConfigKey>& keys);
        std::size_t clearAll(const std::string& owner);

        std::optional<std::string> find(const ConfigKey& key) const;
        std::vector<AppliedOverride> applied() const;
        void applyTo(toml::table& table) const;

    private:
        struct Entry
        {
            std::string owner;
            std::string value;
        };

        bool setEntry(const std::string& owner, const ConfigKey& key, std::string value);
        bool clearEntry(const std::string& owner, const ConfigKey& key);

        // The overrides of each overridden key in the order they were set, so the last one applies. An owner has
        // one for a key at most, and a key with none is not in the map.
        std::map<ConfigKey, std::vector<Entry>> _entries;
        mutable std::mutex _mutex;
    };

    bool applyOverride(toml::table& table, const ConfigKey& key, std::string_view value);

    const toml::node* nodeOf(const toml::table& table, const ConfigKey& key);
}
