#include "ReloadSubscribers.h"

#include <algorithm>
#include <stdexcept>

#include "common/TomlEditor.h"

namespace
{
    using Path = std::vector<std::string>;

    /**
     * Adds the paths of the values that differ between two tables of the config: one that is in one of them only, one
     * of another type, and one whose TOML text is another. A table that is in both is compared value by value, and an
     * array as a whole.
     */
    void addChanges(const toml::table& before, const toml::table& after, Path& path, std::set<Path>& changed)
    {
        for (const auto& [key, node] : before) {
            path.emplace_back(key.str());
            const auto* other = after.get(key.str());
            if (!other || node.type() != other->type()) {
                changed.insert(path);
            } else if (node.is_table()) {
                addChanges(*node.as_table(), *other->as_table(), path, changed);
            } else if (f4cf::common::TomlEditor::Value(node).text() != f4cf::common::TomlEditor::Value(*other).text()) {
                changed.insert(path);
            }
            path.pop_back();
        }
        for (const auto& [key, node] : after) {
            if (!before.contains(key.str())) {
                path.emplace_back(key.str());
                changed.insert(path);
                path.pop_back();
            }
        }
    }

    /**
     * Whether one path is the other or leads to it: a subscription of a table is told the change of a value in it, and
     * one of a value the change of the table it is in, as a table that is added or removed.
     */
    bool overlaps(const Path& path, const Path& other)
    {
        const auto size = std::min(path.size(), other.size());
        return std::equal(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(size), other.begin());
    }
}

namespace f4cf::config
{
    /**
     * Add a callback under a key, by which it is unsubscribed, which is called for every load. A key that is already
     * subscribed throws.
     */
    void ReloadSubscribers::subscribe(const std::string& key, const Callback& callback)
    {
        add(key, { .callback = callback });
    }

    /**
     * Add a callback under a key, by which it is unsubscribed, which is called for a load that changed a value of one
     * of the config's keys, with the ones that changed. A key with no key of its own names a whole section or table,
     * as {"Main", ""}, and a value in a nested table is named by its path, see ConfigKey. A key that is already
     * subscribed, and a config key that is no TOML key, throw.
     */
    void ReloadSubscribers::subscribe(const std::string& key, const std::vector<ConfigKey>& configKeys, const KeysCallback& callback)
    {
        Subscriber subscriber{ .keys = configKeys, .keysCallback = callback };
        for (const auto& configKey : configKeys) {
            auto path = configKey.key.empty() ? ConfigKey{ "", configKey.section }.path() : configKey.path();
            if (!path) {
                throw std::invalid_argument("ConfigBase::subscribeForConfigChangedEvent: '" + configKey.section + "." + configKey.key + "' of '" + key + "' is not a TOML key");
            }
            subscriber.paths.push_back(std::move(*path));
        }
        add(key, std::move(subscriber));
    }

    /**
     * Add a subscriber under a key. A key that is already subscribed throws, and its subscriber stays.
     */
    void ReloadSubscribers::add(const std::string& key, Subscriber subscriber)
    {
        if (_subscribers.contains(key)) {
            throw std::runtime_error("ConfigBase::subscribeForConfigChangedEvent: Key '" + key + "' is already subscribed!");
        }
        _subscribers.emplace(key, std::move(subscriber));
    }

    /**
     * Remove the callback of a key, if there is one.
     */
    void ReloadSubscribers::unsubscribe(const std::string& key)
    {
        _subscribers.erase(key);
    }

    /**
     * Mark that the config values were loaded again, for the next notify, with the values loaded: the ones that differ
     * from the load before are changed. The first load changes none, as there is nothing to compare it with. Called on
     * any thread.
     */
    void ReloadSubscribers::markReloaded(const toml::table& config)
    {
        {
            std::lock_guard lock(_mutex);
            if (_loaded) {
                Path path;
                addChanges(*_loaded, config, path, _changed);
            }
            _loaded = config;
        }
        _reloaded = true;
    }

    /**
     * If a load was marked since the last call, call every subscriber of every load with its key, and every subscriber
     * of keys one of which changed with those that did, in the order it named them. None is called otherwise.
     * `beforeEach` is called with the key of a subscriber before it, for the caller to log it.
     * A load that a subscriber marks from inside its call is for the next notify.
     */
    void ReloadSubscribers::notify(const Callback& beforeEach)
    {
        if (!_reloaded.exchange(false)) {
            return;
        }
        std::set<Path> changed;
        {
            std::lock_guard lock(_mutex);
            changed.swap(_changed);
        }

        // a copy, so a subscriber can unsubscribe from inside its call
        const auto subscribers = _subscribers;
        for (const auto& [key, subscriber] : subscribers) {
            std::vector<ConfigKey> changedKeys;
            for (std::size_t i = 0; i < subscriber.paths.size(); ++i) {
                if (std::ranges::any_of(changed, [&](const Path& path) {
                        return overlaps(subscriber.paths[i], path);
                    })) {
                    changedKeys.push_back(subscriber.keys[i]);
                }
            }
            if (subscriber.keysCallback && changedKeys.empty()) {
                continue;
            }
            if (beforeEach) {
                beforeEach(key);
            }
            if (subscriber.keysCallback) {
                subscriber.keysCallback(changedKeys);
            } else {
                subscriber.callback(key);
            }
        }
    }
}
