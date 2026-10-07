#include "ReloadSubscribers.h"

#include <stdexcept>

namespace f4cf::config
{
    /**
     * Add a callback under a key, by which it is unsubscribed. A key that is already subscribed throws.
     */
    void ReloadSubscribers::subscribe(const std::string& key, const Callback& callback)
    {
        if (_subscribers.contains(key)) {
            throw std::runtime_error("ConfigBase::subscribeForIniChangedEvent: Key '" + key + "' is already subscribed!");
        }
        _subscribers.emplace(key, callback);
    }

    /**
     * Remove the callback of a key, if there is one.
     */
    void ReloadSubscribers::unsubscribe(const std::string& key)
    {
        _subscribers.erase(key);
    }

    /**
     * Mark that the config values were loaded again, for the next notify. Called on any thread.
     */
    void ReloadSubscribers::markReloaded()
    {
        _reloaded = true;
    }

    /**
     * Call every subscriber with its key if a load was marked since the last call, and none otherwise.
     * `beforeEach` is called with the key of a subscriber before it, for the caller to log it.
     * A load that a subscriber marks from inside its call is for the next notify.
     */
    void ReloadSubscribers::notify(const Callback& beforeEach)
    {
        if (!_reloaded.exchange(false)) {
            return;
        }

        // a copy, so a subscriber can unsubscribe from inside its call
        const auto subscribers = _subscribers;
        for (const auto& [key, subscriber] : subscribers) {
            if (beforeEach) {
                beforeEach(key);
            }
            subscriber(key);
        }
    }
}
