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
        subscribers.markReloaded({});
    }
    subscribers.notify();
    REQUIRE(calls == 1);

    // the frames after it, with no load
    subscribers.notify();
    subscribers.notify();
    REQUIRE(calls == 1);

    subscribers.markReloaded({});
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

    subscribers.markReloaded({});
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

    subscribers.markReloaded({});
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

    subscribers.markReloaded({});
    subscribers.notify();
    REQUIRE(first == 0);

    subscribers.subscribe("menu", counting(second));
    subscribers.markReloaded({});
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

    subscribers.markReloaded({});
    subscribers.notify();
    REQUIRE(calls == 1);

    subscribers.markReloaded({});
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
            subscribers.markReloaded({});
        }
    });

    subscribers.markReloaded({});
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

    subscribers.markReloaded({});
    subscribers.notify(beforeEach);
    REQUIRE(order == std::vector<std::string>{ "before menu", "called menu" });
}

namespace
{
    using f4cf::config::ConfigKey;

    /**
     * A subscriber of keys that keeps what it was told, each call as the keys it was told joined with spaces.
     */
    ReloadSubscribers::KeysCallback recording(std::vector<std::string>& calls)
    {
        return [&calls](const std::vector<ConfigKey>& changed) {
            std::string call;
            for (const auto& key : changed) {
                call += (call.empty() ? "" : " ") + key.section + (key.key.empty() ? "" : "." + key.key);
            }
            calls.push_back(call);
        };
    }
}

TEST_CASE("ConfigReloadSubscribers: a subscriber of keys is called for a load that changed one of them, with the ones that changed")
{
    ReloadSubscribers subscribers;
    std::vector<std::string> calls;
    subscribers.subscribe("scale", { { "Main", "fScale" }, { "Main", "iMode" }, { "Hand", "fSize" } }, recording(calls));

    // the first load has nothing to compare with
    subscribers.markReloaded(toml::parse("[Main]\nfScale = 1.0\niMode = 0\nbOn = true\n"));
    subscribers.notify();
    REQUIRE(calls.empty());

    // a load of the values it had, and of another value, changes none of its keys
    subscribers.markReloaded(toml::parse("[Main]\nfScale = 1.0\niMode = 0\nbOn = false\n"));
    subscribers.notify();
    REQUIRE(calls.empty());

    // the ones that changed since the notify before, in the order it named them, and a key that is added
    subscribers.markReloaded(toml::parse("[Main]\nfScale = 2.0\niMode = 0\nbOn = false\n"));
    subscribers.markReloaded(toml::parse("[Main]\nfScale = 2.0\niMode = 3\nbOn = false\n[Hand]\nfSize = 1.0\n"));
    subscribers.notify();
    REQUIRE(calls == std::vector<std::string>{ "Main.fScale Main.iMode Hand.fSize" });

    // a value set back before the notify is a change too, and so is one of another type
    calls.clear();
    subscribers.markReloaded(toml::parse("[Main]\nfScale = 3.0\niMode = 3\nbOn = false\n[Hand]\nfSize = 1.0\n"));
    subscribers.markReloaded(toml::parse("[Main]\nfScale = 2.0\niMode = \"3\"\nbOn = false\n[Hand]\nfSize = 1.0\n"));
    subscribers.notify();
    REQUIRE(calls == std::vector<std::string>{ "Main.fScale Main.iMode" });
}

TEST_CASE("ConfigReloadSubscribers: a key of a section or a table is told a change in it, and a key in a table a change of the table")
{
    ReloadSubscribers subscribers;
    std::vector<std::string> section;
    std::vector<std::string> value;
    subscribers.subscribe("section", { { "Main", "" }, { "Main", "hand" } }, recording(section));
    subscribers.subscribe("value", { { "Main", "hand.x" }, { "Main", "list" } }, recording(value));
    subscribers.markReloaded(toml::parse("[Main]\nlist = [1, 2]\nhand = { x = 1.0, y = 2.0 }\n"));

    subscribers.markReloaded(toml::parse("[Main]\nlist = [1, 2]\nhand = { x = 1.5, y = 2.0 }\n"));
    subscribers.notify();
    REQUIRE(section == std::vector<std::string>{ "Main Main.hand" });
    REQUIRE(value == std::vector<std::string>{ "Main.hand.x" });

    // an array is one value, and a table that is removed changes the values in it
    section.clear();
    value.clear();
    subscribers.markReloaded(toml::parse("[Main]\nlist = [1, 3]\n"));
    subscribers.notify();
    REQUIRE(section == std::vector<std::string>{ "Main Main.hand" });
    REQUIRE(value == std::vector<std::string>{ "Main.hand.x Main.list" });
}

TEST_CASE("ConfigReloadSubscribers: a subscriber of every load is called for a load that changed nothing, and one of keys is not")
{
    ReloadSubscribers subscribers;
    int every = 0;
    std::vector<std::string> keys;
    subscribers.subscribe("every", counting(every));
    subscribers.subscribe("keys", { { "Main", "fScale" } }, recording(keys));
    const auto config = toml::parse("[Main]\nfScale = 1.0\n");

    subscribers.markReloaded(config);
    subscribers.notify();
    subscribers.markReloaded(config);
    subscribers.notify();
    REQUIRE(every == 2);
    REQUIRE(keys.empty());
}

TEST_CASE("ConfigReloadSubscribers: a config key that is no TOML key throws, and nothing is subscribed")
{
    ReloadSubscribers subscribers;
    std::vector<std::string> keys;
    REQUIRE_THROWS(subscribers.subscribe("keys", { { "Main", "fScale" }, { "Main", "a..b" } }, recording(keys)));
    REQUIRE_NOTHROW(subscribers.subscribe("keys", { { "Main", "fScale" } }, recording(keys)));
}
