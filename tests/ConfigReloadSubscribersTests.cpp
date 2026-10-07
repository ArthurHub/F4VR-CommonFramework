#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "config/ReloadSubscribers.h"

using f4cf::config::ReloadSubscribers;

namespace
{
    /**
     * A subscriber that counts its calls.
     */
    ReloadSubscribers::Callback counting(int& calls)
    {
        return [&calls](const std::string&) {
            ++calls;
        };
    }
}

TEST_CASE("ConfigReloadSubscribers: a subscriber is not called when no load ran")
{
    ReloadSubscribers subscribers;
    int calls = 0;
    subscribers.subscribe("menu", counting(calls));

    subscribers.notify();
    subscribers.notify();

    REQUIRE(calls == 0);
}

TEST_CASE("ConfigReloadSubscribers: a subscriber is called once for all the loads since the notify before")
{
    ReloadSubscribers subscribers;
    int calls = 0;
    subscribers.subscribe("menu", counting(calls));

    // a slider that is dragged sets its override many times in one frame
    for (int i = 0; i < 50; ++i) {
        subscribers.markReloaded();
    }
    subscribers.notify();
    REQUIRE(calls == 1);

    // the frames after it, with no load
    subscribers.notify();
    subscribers.notify();
    REQUIRE(calls == 1);

    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(calls == 2);
}

TEST_CASE("ConfigReloadSubscribers: every subscriber is called, with the key it subscribed under")
{
    ReloadSubscribers subscribers;
    std::vector<std::string> called;
    const auto record = [&called](const std::string& key) {
        called.push_back(key);
    };
    subscribers.subscribe("menu", record);
    subscribers.subscribe("pointer", record);

    subscribers.markReloaded();
    subscribers.notify();

    REQUIRE(called.size() == 2);
    REQUIRE(std::ranges::find(called, "menu") != called.end());
    REQUIRE(std::ranges::find(called, "pointer") != called.end());
}

TEST_CASE("ConfigReloadSubscribers: a key that is already subscribed throws, and its subscriber stays")
{
    ReloadSubscribers subscribers;
    int first = 0;
    int second = 0;
    subscribers.subscribe("menu", counting(first));

    REQUIRE_THROWS(subscribers.subscribe("menu", counting(second)));

    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(first == 1);
    REQUIRE(second == 0);
}

TEST_CASE("ConfigReloadSubscribers: an unsubscribed key is not called, and can be subscribed again")
{
    ReloadSubscribers subscribers;
    int first = 0;
    int second = 0;
    subscribers.subscribe("menu", counting(first));
    subscribers.unsubscribe("menu");
    // a key that is not subscribed is not an error
    subscribers.unsubscribe("pointer");

    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(first == 0);

    subscribers.subscribe("menu", counting(second));
    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(first == 0);
    REQUIRE(second == 1);
}

TEST_CASE("ConfigReloadSubscribers: a subscriber can unsubscribe from inside its call")
{
    ReloadSubscribers subscribers;
    int calls = 0;
    subscribers.subscribe("once", [&](const std::string& key) {
        ++calls;
        subscribers.unsubscribe(key);
    });

    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(calls == 1);

    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(calls == 1);
}

TEST_CASE("ConfigReloadSubscribers: a load that a subscriber causes is told at the next notify")
{
    ReloadSubscribers subscribers;
    int calls = 0;
    subscribers.subscribe("menu", [&](const std::string&) {
        // as a subscriber that sets an override, which loads the values again
        if (++calls == 1) {
            subscribers.markReloaded();
        }
    });

    subscribers.markReloaded();
    subscribers.notify();
    REQUIRE(calls == 1);

    subscribers.notify();
    REQUIRE(calls == 2);

    subscribers.notify();
    REQUIRE(calls == 2);
}

TEST_CASE("ConfigReloadSubscribers: the caller is told the key of each subscriber before it is called")
{
    ReloadSubscribers subscribers;
    std::vector<std::string> order;
    subscribers.subscribe("menu", [&order](const std::string& key) {
        order.push_back("called " + key);
    });

    const auto beforeEach = [&order](const std::string& key) {
        order.push_back("before " + key);
    };
    subscribers.notify(beforeEach);
    REQUIRE(order.empty());

    subscribers.markReloaded();
    subscribers.notify(beforeEach);
    REQUIRE(order == std::vector<std::string>{ "before menu", "called menu" });
}
