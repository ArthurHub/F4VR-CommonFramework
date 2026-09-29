#include "Perf.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace f4cf::perf
{
    namespace
    {
        struct Registry
        {
            std::mutex lock;
            std::vector<Site*> sites;
            // by function and label; the sites are never freed, see dynamicSite
            std::map<std::pair<std::string, std::string>, Site*, std::less<>> dynamicSites;
        };

        /**
         * Function-local static, not a namespace-level one: a site can be a static in another translation unit whose
         * initializer runs before this one's, and registering into a not-yet-constructed registry would be undefined
         * behaviour. It also outlives every static site, since its construction completes inside the first site's
         * constructor, so unregistering from a destructor stays safe.
         */
        Registry& registry()
        {
            static Registry registry;
            return registry;
        }

        std::atomic<std::chrono::steady_clock::rep> s_windowStart{ 0 };

        std::atomic<const Site*> s_frameSite{ nullptr };

        /**
         * The frame site, published for readers as soon as it exists.
         */
        struct FrameSite
        {
            explicit FrameSite(const char* function)
                : site(function, nullptr)
            {
                s_frameSite.store(&site, std::memory_order_release);
            }

            Site site;
        };

        /**
         * The last two "::" parts of a function name, as a pointer into it.
         */
        const char* shortName(const char* function)
        {
            const std::string_view name(function);
            const auto last = name.rfind("::");
            if (last == std::string_view::npos || last == 0) {
                return function;
            }
            const auto previous = name.rfind("::", last - 1);
            return previous == std::string_view::npos ? function : function + previous + 2;
        }

        /**
         * A dynamic site with the storage for its label.
         */
        struct DynamicSite
        {
            DynamicSite(const char* function, const std::string_view label)
                : label(label),
                  site(function, this->label.c_str())
            {}

            std::string label;
            Site site;
        };
    }

    void setEnabled(const bool enabled)
    {
        if (enabled == isEnabled()) {
            return;
        }
        if (enabled) {
            reset();
        }
        internal::g_enabled.store(enabled, std::memory_order_relaxed);
    }

    void reset()
    {
        for (auto* site : sites()) {
            (void)site->drain();
        }
        s_windowStart.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    }

    std::chrono::steady_clock::time_point windowStart()
    {
        return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(s_windowStart.load(std::memory_order_relaxed)));
    }

    Site::Site(const char* function, const char* label)
        : _function(function),
          _label(label),
          _shortFunction(shortName(function))
    {
        auto& reg = registry();
        std::lock_guard lock(reg.lock);
        reg.sites.push_back(this);
    }

    Site::~Site()
    {
        auto& reg = registry();
        std::lock_guard lock(reg.lock);
        std::erase(reg.sites, this);
    }

    std::vector<Site*> sites()
    {
        auto& reg = registry();
        std::lock_guard lock(reg.lock);
        return reg.sites;
    }

    Site& dynamicSite(const char* function, const std::string_view label)
    {
        auto& reg = registry();
        {
            std::lock_guard lock(reg.lock);
            const auto found = reg.dynamicSites.find(std::pair<std::string_view, std::string_view>(function, label));
            if (found != reg.dynamicSites.end()) {
                return *found->second;
            }
        }
        // constructed outside the lock, since a Site registers itself under it; never freed, so the reference stays valid
        // for the life of the process and the registry never holds a destroyed site
        auto* created = new DynamicSite(function, label);
        std::lock_guard lock(reg.lock);
        const auto [it, inserted] = reg.dynamicSites.try_emplace(std::pair<std::string, std::string>(function, label), &created->site);
        return *it->second;
    }

    Site& declareFrameSite(const char* function)
    {
        static FrameSite frame(function);
        return frame.site;
    }

    const Site* frameSite()
    {
        return s_frameSite.load(std::memory_order_acquire);
    }
}
