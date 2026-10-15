#include "Overrides.h"

#include <algorithm>
#include <format>

namespace
{
    /**
     * Whether a name is a bare TOML key, which is its own path of one key.
     */
    bool isBareKey(const std::string_view name)
    {
        return !name.empty() && std::ranges::all_of(name, [](const char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    }

    /**
     * A key's name as TOML text: bare when it can be, and in quotes otherwise.
     */
    std::string keyText(const std::string_view name)
    {
        if (isBareKey(name)) {
            return std::string(name);
        }
        std::string text = "\"";
        for (const char c : name) {
            if (c == '"' || c == '\\') {
                text += '\\';
            }
            text += c;
        }
        return text + "\"";
    }

    /**
     * The keys of a TOML dotted key ("hand.x", "\"my.key\""), read by toml++ as the key of a value, or no value when
     * it is not one key. An empty text is a path of no keys.
     */
    std::optional<std::vector<std::string>> dottedKeyPath(const std::string_view text)
    {
        if (text.empty()) {
            return std::vector<std::string>{};
        }
        if (isBareKey(text)) {
            return std::vector{ std::string(text) };
        }
        try {
            const auto table = toml::parse(std::format("{} = 0", text));
            std::vector<std::string> path;
            for (const toml::table* at = &table; at && at->size() == 1;) {
                const auto& [key, node] = *at->cbegin();
                path.emplace_back(key.str());
                if (!node.is_table()) {
                    return path;
                }
                at = node.as_table();
            }
            return std::nullopt;
        } catch (const toml::parse_error&) {
            return std::nullopt;
        }
    }
}

namespace f4cf::config
{
    /**
     * The keys from the file's root to the value: the section's and then the key's, each read as a TOML dotted key.
     * No value when either is not a TOML key, or the key is empty.
     */
    std::optional<std::vector<std::string>> ConfigKey::path() const
    {
        auto keys = dottedKeyPath(section);
        const auto keyKeys = dottedKeyPath(key);
        if (!keys || !keyKeys || keyKeys->empty()) {
            return std::nullopt;
        }
        keys->insert(keys->end(), keyKeys->begin(), keyKeys->end());
        return keys;
    }

    /**
     * The one spelling of the key's path: the first key as the section and the rest as a dotted key, each bare when
     * it can be, as "Main" and "hand.x" for both ("Main.hand", "x") and ("Main", "hand . x"). A key of the root has
     * an empty section. The overrides are kept by it, so two spellings of one value are one override. A key that is
     * not a TOML key is itself.
     */
    ConfigKey ConfigKey::canonical() const
    {
        const auto keys = path();
        if (!keys) {
            return *this;
        }
        ConfigKey canonical;
        const auto first = keys->size() == 1 ? 0 : 1;
        if (first == 1) {
            canonical.section = keyText(keys->front());
        }
        for (auto it = keys->begin() + first; it != keys->end(); ++it) {
            canonical.key += (canonical.key.empty() ? "" : ".") + keyText(*it);
        }
        return canonical;
    }

    /**
     * The value of a config at a section and key, see ConfigKey, or null when the config does not have it, or the
     * section or key is not a TOML key.
     */
    const toml::node* nodeOf(const toml::table& table, const ConfigKey& key)
    {
        const auto path = key.path();
        if (!path) {
            return nullptr;
        }
        const toml::node* node = &table;
        for (const auto& name : *path) {
            const auto* sub = node->as_table();
            node = sub ? sub->get(name) : nullptr;
            if (!node) {
                return nullptr;
            }
        }
        return node;
    }

    /**
     * Set the owner's override of a key, which applies from now, also over another owner's.
     * Returns whether the owner had no override of the key before, so a new value for one it already has
     * can be told from a new override. Every call takes a key in any spelling of its path, and keeps it in one,
     * see ConfigKey::canonical.
     */
    bool Overrides::set(const std::string& owner, const ConfigKey& key, std::string value)
    {
        std::lock_guard lock(_mutex);
        return setEntry(owner, key.canonical(), std::move(value));
    }

    /**
     * Set the owner's overrides of several keys, see set, each value in its text form. Returns the
     * ones for a key that the owner did not override before, in the order given, by their kept key.
     */
    std::vector<AppliedOverride> Overrides::set(const std::string& owner, const std::vector<ConfigEntry>& overrides)
    {
        std::lock_guard lock(_mutex);

        std::vector<AppliedOverride> added;
        for (const auto& entry : overrides) {
            auto value = entry.value.toString();
            auto key = ConfigKey{ entry.section, entry.key }.canonical();
            if (setEntry(owner, key, value)) {
                added.push_back({ std::move(key.section), std::move(key.key), std::move(value), owner });
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
        return clearEntry(owner, key.canonical());
    }

    /**
     * Remove the owner's overrides of several keys, see clear. Returns the keys it had one for, in the order
     * given, by their kept key.
     */
    std::vector<ConfigKey> Overrides::clear(const std::string& owner, const std::vector<ConfigKey>& keys)
    {
        std::lock_guard lock(_mutex);

        std::vector<ConfigKey> cleared;
        for (const auto& key : keys) {
            if (auto canonical = key.canonical(); clearEntry(owner, canonical)) {
                cleared.push_back(std::move(canonical));
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

        const auto found = _entries.find(key.canonical());
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
     * see typedLike. The key is a path, see ConfigKey, and a table on it that the file does not have is added. Text
     * that is not of the file's type is put as a string, so the mod's read of the key gets its default, as from a
     * value of the file of another type. A key that is not a TOML key, or whose path goes through a value that is not
     * a table, is left out. Returns whether the value is put.
     */
    bool applyOverride(toml::table& table, const ConfigKey& key, const std::string_view value)
    {
        const auto path = key.path();
        if (!path) {
            return false;
        }
        toml::table* parent = &table;
        for (std::size_t i = 0; i + 1 < path->size(); i++) {
            auto* node = parent->get((*path)[i]);
            if (!node) {
                node = &parent->insert_or_assign((*path)[i], toml::table{}).first->second;
            }
            parent = node->as_table();
            if (!parent) {
                return false;
            }
        }

        const auto& name = path->back();
        const auto typed = typedLike(parent->get(name), value);
        if (!typed) {
            parent->insert_or_assign(name, std::string(value));
            return true;
        }
        typed->get("v")->visit([&](const auto& v) {
            parent->insert_or_assign(name, v);
        });
        return true;
    }
}
