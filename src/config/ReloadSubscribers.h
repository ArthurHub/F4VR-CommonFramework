#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>

// The subscribers to a reload of the mod's config, apart from the game: plain std only, so it is unit tested.
namespace f4cf::config
{
    /**
     * The callbacks to call when the config values were loaded again, each under a key.
     * A load runs on any thread and only marks that it ran. The game thread then calls the subscribers, once
     * for all the loads since it last did. Everything but markReloaded is for the game thread only.
     */
    class ReloadSubscribers
    {
    public:
        using Callback = std::function<void(const std::string&)>;

        void subscribe(const std::string& key, const Callback& callback);
        void unsubscribe(const std::string& key);
        void markReloaded();
        void notify(const Callback& beforeEach = nullptr);

    private:
        std::unordered_map<std::string, Callback> _subscribers;

        // Set by a load, on the thread it runs on, and taken by notify
        std::atomic<bool> _reloaded = false;
    };
}
