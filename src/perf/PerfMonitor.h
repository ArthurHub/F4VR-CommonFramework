#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include "PerfStats.h"

namespace f4cf::perf
{
    /**
     * Lightweight performance monitor for hot-path functions.
     *
     * Every monitor feeds two independent consumers:
     *  - The log. While the logger is at DEBUG level or lower it accumulates per-call durations over a time
     *    window, logs a one-line summary (count, calls/s, avg, p95, p99, min, max, busy%) at INFO level once
     *    the window elapses, and starts a new window.
     *  - On-demand reads. While collection is switched on (setCollecting) it also accumulates into stats(),
     *    which nothing clears but resetStats(), so a reader chooses its own window: reset, hold the
     *    condition, read. all() lists every live monitor for such a reader.
     *
     * With both off, scope() returns an inert timer that takes no timestamp and records nothing, so the cost
     * at a measured site is a level check and a relaxed atomic load. Enabling either one at runtime starts
     * collecting from the next call. Percentiles are exact: per-call durations are kept in vectors that keep
     * their capacity, so steady state has no per-call allocations (PerfStats caps how many are kept).
     *
     * GAME THREAD ONLY, and NOT thread-safe. Constructing a monitor registers it in all(), and a reader walks
     * that list and every monitor's stats on the game thread, so a monitor used on another thread races with
     * it. Measure other threads with a PerfStats of their own.
     *
     * Typical usage with the RAII scope helper (one static monitor per measured site):
     * @code
     *     void onFrameUpdate()
     *     {
     *         static perf::PerfMonitor perf("onFrameUpdate");
     *         const auto timer = perf.scope();
     *         // ... work being measured ...
     *     }
     * @endcode
     */
    class PerfMonitor
    {
    public:
        /**
         * RAII timer that measures the lifetime of its scope and records it into the owning
         * PerfMonitor when destroyed.
         */
        class ScopedTimer
        {
        public:
            /**
             * @param monitor monitor to record into, or nullptr for an inert timer that does nothing
             *                (used when collection is disabled, avoiding even the start timestamp).
             */
            explicit ScopedTimer(PerfMonitor* monitor)
                : _monitor(monitor),
                  _start(monitor ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{})
            {}

            ScopedTimer(const ScopedTimer&) = delete;
            ScopedTimer& operator=(const ScopedTimer&) = delete;
            ScopedTimer& operator=(ScopedTimer&&) = delete;

            ScopedTimer(ScopedTimer&& other) noexcept
                : _monitor(other._monitor),
                  _start(other._start)
            {
                other._monitor = nullptr;
            }

            ~ScopedTimer()
            {
                if (_monitor) {
                    _monitor->record(std::chrono::steady_clock::now() - _start);
                }
            }

        private:
            PerfMonitor* _monitor;
            std::chrono::steady_clock::time_point _start;
        };

        /**
         * @param name        label used in the log line and by on-demand readers.
         * @param logInterval how often a summary is logged (defaults to every 60 seconds).
         */
        explicit PerfMonitor(std::string name, std::chrono::milliseconds logInterval = std::chrono::seconds(60));

        ~PerfMonitor();

        // registered by address in all(), so it must stay where it was constructed
        PerfMonitor(const PerfMonitor&) = delete;
        PerfMonitor& operator=(const PerfMonitor&) = delete;
        PerfMonitor(PerfMonitor&&) = delete;
        PerfMonitor& operator=(PerfMonitor&&) = delete;

        /**
         * Create an RAII timer that records its scope duration into this monitor on destruction.
         * When debug logging and collection are both off the returned timer is inert (no timestamp, no recording).
         */
        [[nodiscard]] ScopedTimer scope()
        {
            return ScopedTimer(logger::isDebugEnabled() || isCollecting() ? this : nullptr);
        }

        /**
         * Record a single measured duration into whichever consumers are on. Logs and starts a new log window
         * when the log interval has elapsed.
         */
        void record(std::chrono::nanoseconds duration);

        /**
         * Log the current log window's summary (if it has any samples) and start a new window immediately.
         * Does not touch stats().
         */
        void flush();

        [[nodiscard]] const std::string& name() const
        {
            return _name;
        }

        /**
         * Everything recorded while collection was on, since the last resetStats().
         */
        [[nodiscard]] const PerfStats& stats() const
        {
            return _stats;
        }

        void resetStats()
        {
            _stats.reset();
        }

        /**
         * Every live monitor in this DLL, in construction order.
         */
        [[nodiscard]] static const std::vector<PerfMonitor*>& all();

        /**
         * Switch the on-demand accumulation into stats() on or off for every monitor in this DLL. Switching it
         * off keeps what was collected; resetStats() is what clears it. Callable from any thread.
         */
        static void setCollecting(const bool collecting)
        {
            _collecting.store(collecting, std::memory_order_relaxed);
        }

        [[nodiscard]] static bool isCollecting()
        {
            return _collecting.load(std::memory_order_relaxed);
        }

    private:
        inline static std::atomic<bool> _collecting{ false };

        std::string _name;
        std::chrono::nanoseconds _logInterval;

        // The log window starts at its first sample, so a gap before it (e.g. debug disabled) does not
        // inflate the reported window length.
        PerfStats _logWindow;

        // The on-demand window, owned by the reader through resetStats().
        PerfStats _stats;
    };
}
