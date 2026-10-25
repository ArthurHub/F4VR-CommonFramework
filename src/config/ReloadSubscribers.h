#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <toml++/toml.hpp>
#include <unordered_map>
#include <vector>

#include "Overrides.h"

// The subscribers to a reload of the mod's config, apart from the game: plain std and toml++ only, so it is unit tested.
namespace f4cf::config
{
    /**
     * The callbacks to call when the config values were loaded again, each under a key. One is called for every
     * load, or only for a change of one of the config's keys it names.
     * A load runs on any thread, and only marks that it ran with its values, which are compared with the ones of
     * the load before. The game thread then calls the subscribers, once for all the loads since it last did.
     * Everything but markReloaded is for the game thread only.
     */
    class ReloadSubscribers
    {
    public:
        using Callback = std::function<void(const std::string&)>;

        // the keys of the subscription that changed, as it named them
        using KeysCallback = std::function<void(const std::vector<ConfigKey>& changed)>;

        void subscribe(const std::string& key, const Callback& callback);
        void subscribe(const std::string& key, const std::vector<ConfigKey>& configKeys, const KeysCallback& callback);
        void unsubscribe(const std::string& key);
        void markReloaded(const toml::table& config);
        void notify(const Callback& beforeEach = nullptr);

    private:
        /**
         * A subscriber: called for every load, or for a change of one of its keys, by their paths from the file's root.
         */
        struct Subscriber
        {
            Callback callback;
            std::vector<ConfigKey> keys;
            std::vector<std::vector<std::string>> paths;
            KeysCallback keysCallback;
        };

        void add(const std::string& key, Subscriber subscriber);

        std::unordered_map<std::string, Subscriber> _subscribers;

        // Set by a load, on the thread it runs on, and taken by notify
        std::atomic<bool> _reloaded = false;

        // The values of the last load, none before the first, and the paths of the values that changed since the
        // last notify. Under _mutex, as a load marks them on its thread.
        std::optional<toml::table> _loaded;
        std::set<std::vector<std::string>> _changed;
        std::mutex _mutex;
    };
}
