#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

#include "Histogram.h"

#ifdef F4CF_WITH_TRACY
#include <tracy/Tracy.hpp>
#endif

namespace f4cf::perf
{
    class Site;

    namespace internal
    {
        inline std::atomic<bool> g_enabled{ false };

#ifdef F4CF_WITH_TRACY
        // Set once the Tracy profiler has started (startTracy, when the plugin loads): a zone opened before that would
        // find no profiler.
        inline std::atomic<bool> g_tracyStarted{ false };
#endif

        // The innermost site open on this thread. A Scope keeps the one it replaced by value, never a pointer to another
        // Scope: an SEH recovery can skip destructors, and the next outer Scope to close restores this anyway.
        inline thread_local Site* t_openSite = nullptr;

        // the last run order handed out, see Site::runOrder
        inline std::atomic<std::uint64_t> g_lastRunOrder{ 0 };
    }

    /**
     * Whether sites record. While off, a measured site costs one relaxed atomic load. Any thread.
     */
    [[nodiscard]] inline bool isEnabled()
    {
        return internal::g_enabled.load(std::memory_order_relaxed);
    }

    /**
     * Switch recording on or off for every site in this DLL. Switching it on resets every site first, so the window
     * starts empty; switching it off keeps what was recorded. Any thread.
     */
    void setEnabled(bool enabled);

    /**
     * Drop what every site and the frame context recorded, and start a new window. Any thread.
     */
    void reset();

    /**
     * When the current window started: the last reset, including the one setEnabled(true) does.
     */
    [[nodiscard]] std::chrono::steady_clock::time_point windowStart();

    /**
     * What a site times: wall clock on the thread it runs on, or GPU time of work the mod issued, read back from GPU
     * timestamps (GpuTimer). A report nests each kind apart, GPU sites in a tree of their own.
     */
    enum class SiteKind : std::uint8_t
    {
        Cpu,
        Gpu,
    };

    /**
     * One measured place in the code: the duration of every call made while recording is on, how much of that was
     * spent in sites it opened (self time = total - children), and the site that opened it, which nests sites into a
     * tree per thread.
     *
     * A site is a function-local static that F4CF_PERF_SCOPE / F4CF_PERF_FUNCTION declare, or a dynamic one from
     * dynamicSite() for a label only known at runtime. Constructing it registers it in sites(), so it stays where it
     * was constructed. Recording and reading are safe on any thread.
     */
    class Site
    {
    public:
        /**
         * Everything a site recorded, as of a read or a drain.
         */
        struct Stats
        {
            Histogram::Snapshot durations;
            // the part of durations spent inside the sites this one opened
            std::uint64_t childrenNs = 0;

            [[nodiscard]] std::uint64_t selfNs() const
            {
                return durations.sumNs > childrenNs ? durations.sumNs - childrenNs : 0;
            }
        };

        /**
         * @param function the enclosing function (__FUNCTION__), which tells blocks with the same label apart
         * @param label    the block measured ("arms"), or nullptr for the whole function. Both must outlive the site;
         *                 the macros pass string literals.
         * @param file     the source file (__FILE__), "" when there is none, as for a dynamic site; it must outlive the
         *                 site too
         * @param line     the source line; with the file, it is where a Tracy build's viewer shows the site's source
         * @param kind     what it times: Gpu for a site GpuTimer records into, which never opens a Scope
         */
        Site(const char* function, const char* label, const char* file = "", std::uint32_t line = 0, SiteKind kind = SiteKind::Cpu);
        ~Site();

        Site(const Site&) = delete;
        Site& operator=(const Site&) = delete;
        Site(Site&&) = delete;
        Site& operator=(Site&&) = delete;

        /**
         * What the site measures: its label, or the short function name for a whole-function site.
         */
        [[nodiscard]] const char* label() const
        {
            return _label ? _label : _shortFunction;
        }

        [[nodiscard]] const char* function() const
        {
            return _function;
        }

        /**
         * The last two parts of the function's name, dropping its namespaces: "Skeleton::onFrameUpdate".
         */
        [[nodiscard]] const char* shortFunction() const
        {
            return _shortFunction;
        }

        [[nodiscard]] bool isWholeFunction() const
        {
            return _label == nullptr;
        }

        [[nodiscard]] SiteKind kind() const
        {
            return _kind;
        }

        /**
         * The site that was open on the same thread when this one ran, or nullptr for a thread's outermost site and for
         * one that has not run yet. With several callers this is the first one seen, and hasMultipleCallers() is set.
         */
        [[nodiscard]] Site* caller() const
        {
            const auto caller = _caller.load(std::memory_order_relaxed);
            return caller == UNSEEN ? nullptr : reinterpret_cast<Site*>(caller);
        }

        [[nodiscard]] bool hasMultipleCallers() const
        {
            return _multipleCallers.load(std::memory_order_relaxed);
        }

        /**
         * Whether the site has run while recording was on, ever: it has a caller (or is known to be outermost) and a thread.
         */
        [[nodiscard]] bool hasRun() const
        {
            return _caller.load(std::memory_order_relaxed) != UNSEEN;
        }

        /**
         * The thread the site first ran on, which is what groups a thread's outermost sites together. A default id until
         * it has run.
         */
        [[nodiscard]] std::thread::id thread() const
        {
            return _thread.load(std::memory_order_relaxed);
        }

        /**
         * Where the site runs among the sites sharing its caller: a number stamped when its caller became known, so
         * sorting by it lists them in the order they run in a frame. A site that ran first with no caller open (see
         * noteCaller) is stamped again when its real caller shows up. 0 until it has run.
         */
        [[nodiscard]] std::uint64_t runOrder() const
        {
            return _runOrder.load(std::memory_order_relaxed);
        }

        /**
         * What was recorded since the last drain, leaving it in place.
         */
        [[nodiscard]] Stats read() const
        {
            return { _durations.peek(), _childrenNs.load(std::memory_order_relaxed) };
        }

        /**
         * What was recorded since the last drain, starting the site empty. Its caller is kept.
         */
        [[nodiscard]] Stats drain()
        {
            return { _durations.drain(), _childrenNs.exchange(0, std::memory_order_relaxed) };
        }

        /**
         * Record one duration measured elsewhere, such as a GPU span GpuTimer read back, under caller: a site of the same
         * kind, or nullptr for an outermost one. Unlike a Scope it records whether or not perf is recording, so the one
         * measuring decides. Any thread.
         */
        void record(const std::uint64_t ns, Site* caller)
        {
            noteCaller(caller);
            _durations.recordNs(ns);
            if (caller) {
                caller->_childrenNs.fetch_add(ns, std::memory_order_relaxed);
            }
        }

    private:
        friend class Scope;

        // never the address of a Site, which is at least pointer-aligned
        static constexpr std::uintptr_t UNSEEN = 1;

        /**
         * Keep the first caller seen, the thread of that first run and the run order, and note when another caller
         * shows up. One relaxed load once the caller is known.
         */
        void noteCaller(const Site* caller)
        {
            if (caller == this) {
                return; // recursion
            }
            const auto value = reinterpret_cast<std::uintptr_t>(caller);
            auto known = _caller.load(std::memory_order_relaxed);
            // A site that ran with no caller open may only have looked outermost: recording switched on while its
            // caller was already running, or an SEH recovery skipped a restore. A real caller replaces that without
            // counting as a second one.
            while (known == UNSEEN || (known == 0 && value != 0)) {
                const bool firstRun = known == UNSEEN;
                if (_caller.compare_exchange_weak(known, value, std::memory_order_relaxed)) {
                    if (firstRun) {
                        _thread.store(std::this_thread::get_id(), std::memory_order_relaxed);
                    }
                    _runOrder.store(internal::g_lastRunOrder.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                    return;
                }
            }
            if (known != value && value != 0) {
                _multipleCallers.store(true, std::memory_order_relaxed);
            }
        }

        const char* _function;
        const char* _label;
        const char* _shortFunction;
        SiteKind _kind;
        Histogram _durations;
        std::atomic<std::uint64_t> _childrenNs{ 0 };
        std::atomic<std::uintptr_t> _caller{ UNSEEN };
        std::atomic<bool> _multipleCallers{ false };
        std::atomic<std::thread::id> _thread{};
        std::atomic<std::uint64_t> _runOrder{ 0 };
#ifdef F4CF_WITH_TRACY
        // what the site's Tracy zones are named and located by; after the members label() reads
        ::tracy::SourceLocationData _tracyLocation;
#endif
    };

    /**
     * RAII timer: records its lifetime into a site, and makes that site the caller of the sites opened inside it on the
     * same thread. While recording is off it does nothing, not even take a timestamp.
     *
     * In a Tracy build it is also a Tracy zone, open while a viewer is connected whether or not the site records.
     */
    class [[nodiscard]] Scope
    {
    public:
        explicit Scope(Site& site)
#ifdef F4CF_WITH_TRACY
            : _tracyZone(&site._tracyLocation, -1, internal::g_tracyStarted.load(std::memory_order_acquire))
#endif
        {
            if (!isEnabled()) {
                return;
            }
            _site = &site;
            _caller = internal::t_openSite;
            internal::t_openSite = &site;
            site.noteCaller(_caller);
            _start = std::chrono::steady_clock::now();
        }

        ~Scope()
        {
            if (!_site) {
                return;
            }
            const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - _start).count());
            _site->_durations.recordNs(ns);
            if (_caller) {
                _caller->_childrenNs.fetch_add(ns, std::memory_order_relaxed);
            }
            internal::t_openSite = _caller;
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

    private:
        Site* _site = nullptr;
        Site* _caller = nullptr;
        std::chrono::steady_clock::time_point _start{};
#ifdef F4CF_WITH_TRACY
        // ends after the destructor's body, so the zone spans the recorded time
        ::tracy::ScopedZone _tracyZone;
#endif
    };

    /**
     * Every site in this DLL, in construction order. Any thread. Sites live until the DLL unloads.
     */
    [[nodiscard]] std::vector<Site*> sites();

    /**
     * The site for a label only known at runtime, such as a callback's registration tag, in the given function
     * (__FUNCTION__). The first call for a function and label creates it, later ones return the same site, and it is
     * never freed. Takes a lock, so look it up once and keep the reference rather than calling this per measurement.
     * Any thread.
     *
     * A GPU site (kind Gpu) is another site from a CPU one with the same function and label.
     */
    [[nodiscard]] Site& dynamicSite(const char* function, std::string_view label, SiteKind kind = SiteKind::Cpu);

    /**
     * The site that times one whole frame of the mod, created by the first call with the function it times. ModBase
     * declares it around onFrameUpdateSafe, so a mod does not call this. Readers count frames by it and call the
     * thread it runs on the game thread.
     */
    [[nodiscard]] Site& declareFrameSite(const char* function);

    /**
     * The frame site, or nullptr before the mod's first frame. Any thread.
     */
    [[nodiscard]] const Site* frameSite();
}

#define F4CF_PERF_CONCAT_INNER_(a, b) a##b
#define F4CF_PERF_CONCAT_(a, b) F4CF_PERF_CONCAT_INNER_(a, b)
#define F4CF_PERF_SITE_(label, line)                                                                       \
    static ::f4cf::perf::Site F4CF_PERF_CONCAT_(f4cfPerfSite_, line)(__FUNCTION__, label, __FILE__, line); \
    const ::f4cf::perf::Scope F4CF_PERF_CONCAT_(f4cfPerfScope_, line)(F4CF_PERF_CONCAT_(f4cfPerfSite_, line))

/**
 * Measure the rest of the enclosing block as a site labelled label, a string literal: F4CF_PERF_SCOPE("arms").
 * The site nests under whatever site is open on the thread, so the label only has to be unique within its function.
 */
#define F4CF_PERF_SCOPE(label) F4CF_PERF_SITE_(label, __LINE__)

/**
 * Measure the rest of the enclosing function as a site named after the function.
 */
#define F4CF_PERF_FUNCTION() F4CF_PERF_SITE_(nullptr, __LINE__)
