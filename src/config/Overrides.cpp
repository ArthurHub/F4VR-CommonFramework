#include "Overrides.h"

#include <algorithm>

namespace f4cf::config
{
    /**
     * Set the owner's override of a key, which applies from now, also over another owner's.
     * Returns whether the owner had no override of the key before, so a new value for one it already has
     * can be told from a new override.
     */
    bool Overrides::set(const std::string& owner, const ConfigKey& key, std::string value)
    {
        std::lock_guard lock(_mutex);
        return setEntry(owner, key, std::move(value));
    }

    /**
     * Set the owner's overrides of several keys, see set, each value in its text form. Returns the
     * ones for a key that the owner did not override before, in the order given.
     */
    std::vector<AppliedOverride> Overrides::set(const std::string& owner, const std::vector<ConfigEntry>& overrides)
    {
        std::lock_guard lock(_mutex);

        std::vector<AppliedOverride> added;
        for (const auto& entry : overrides) {
            auto value = entry.value.toString();
            if (setEntry(owner, { entry.section, entry.key }, value)) {
                added.push_back({ entry.section, entry.key, std::move(value), owner });
            }
        }
        return added;
    }

    /**
     * Remove the owner's override of a key, and no other owner's. If it was the one that applied, the one
     * set before it applies again, and with none left the file's value does.
     * Returns whether the owner had one.
     */
    bool Overrides::clear(const std::string& owner, const ConfigKey& key)
    {
        std::lock_guard lock(_mutex);
        return clearEntry(owner, key);
    }

    /**
     * Remove the owner's overrides of several keys, see clear. Returns the keys it had one for, in the order
     * given.
     */
    std::vector<ConfigKey> Overrides::clear(const std::string& owner, const std::vector<ConfigKey>& keys)
    {
        std::lock_guard lock(_mutex);

        std::vector<ConfigKey> cleared;
        for (const auto& key : keys) {
            if (clearEntry(owner, key)) {
                cleared.push_back(key);
            }
        }
        return cleared;
    }

    /**
     * Remove every override of the owner, and no other owner's. Returns how many it had.
     */
    std::size_t Overrides::clearAll(const std::string& owner)
    {
        std::lock_guard lock(_mutex);

        std::size_t cleared = 0;
        for (auto it = _entries.begin(); it != _entries.end();) {
            cleared += std::erase_if(it->second, [&](const Entry& entry) {
                return entry.owner == owner;
            });
            it = it->second.empty() ? _entries.erase(it) : std::next(it);
        }
        return cleared;
    }

    /**
     * The value that applies for a key, or no value when no owner overrides it.
     */
    std::optional<std::string> Overrides::find(const ConfigKey& key) const
    {
        std::lock_guard lock(_mutex);

        const auto found = _entries.find(key);
        return found == _entries.end() ? std::nullopt : std::optional{ found->second.back().value };
    }

    /**
     * The override that applies for every overridden key, ordered by section and key.
     */
    std::vector<AppliedOverride> Overrides::applied() const
    {
        std::lock_guard lock(_mutex);

        std::vector<AppliedOverride> applied;
        applied.reserve(_entries.size());
        for (const auto& [key, entries] : _entries) {
            applied.push_back({ key.section, key.key, entries.back().value, entries.back().owner });
        }
        return applied;
    }

    /**
     * Put the value that applies for every overridden key into a loaded config, in place of the file's, before
     * the mod reads its values from it, see applyOverride.
     */
    void Overrides::applyTo(toml::table& table) const
    {
        std::lock_guard lock(_mutex);

        for (const auto& [key, entries] : _entries) {
            applyOverride(table, key, entries.back().value);
        }
    }

    /**
     * Set the owner's override of a key, with the lock already held. Returns whether it had none before.
     */
    bool Overrides::setEntry(const std::string& owner, const ConfigKey& key, std::string value)
    {
        auto& entries = _entries[key];
        const bool added = std::erase_if(entries, [&](const Entry& entry) {
            return entry.owner == owner;
        }) == 0;
        entries.push_back({ owner, std::move(value) });
        return added;
    }

    /**
     * Remove the owner's override of a key, with the lock already held. Returns whether it had one.
     */
    bool Overrides::clearEntry(const std::string& owner, const ConfigKey& key)
    {
        const auto found = _entries.find(key);
        if (found == _entries.end()) {
            return false;
        }

        const bool removed = std::erase_if(found->second, [&](const Entry& entry) {
            return entry.owner == owner;
        }) > 0;
        if (found->second.empty()) {
            _entries.erase(found);
        }
        return removed;
    }

    /**
     * Put a value given as text into a loaded config, in place of the file's, in the type the file has for the key,
     * see typedLike. A key or a section that the file does not have is added. Text that is not of the file's type is
     * put as a string, so the mod's read of the key gets its default, as from a value of the file of another type.
     */
    void applyOverride(toml::table& table, const ConfigKey& key, const std::string_view value)
    {
        auto* section = table.get_as<toml::table>(key.section);
        if (!section) {
            section = table.insert_or_assign(key.section, toml::table{}).first->second.as_table();
        }
        const auto typed = typedLike(section->get(key.key), value);
        if (!typed) {
            section->insert_or_assign(key.key, std::string(value));
            return;
        }
        typed->get("v")->visit([&](const auto& v) {
            section->insert_or_assign(key.key, v);
        });
    }
}
