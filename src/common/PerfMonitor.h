#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace f4cf::common
{
    /**
     * Lightweight performance monitor for hot-path functions.
     *
     * Accumulates per-call duration statistics (count, min, max, average, total, p95, p99) over a
     * time window and logs a one-line summary at INFO level once the window elapses, then resets.
     * Logging is driven by calls to record()/the scope timer, so there is no background thread.
     *
     * Per-call durations are retained in a vector so percentiles are exact. The vector keeps its
     * capacity across windows, so after warm-up there are no per-call allocations; steady-state
     * memory is ~8 bytes per call made within one window.
     *
     * Intended for single-threaded use (e.g. the game main loop). It is NOT thread-safe.
     *
     * Collection is gated on the logger being at DEBUG level or lower: when debug logging is off,
     * scope() returns an inert timer that takes no timestamp and records nothing, so the cost at a
     * measured site is a single cheap level check. Enabling debug at runtime starts a fresh window.
     *
     * Typical usage with the RAII scope helper (one static monitor per measured site):
     * @code
     *     void onFrameUpdate()
     *     {
     *         static common::PerfMonitor perf("onFrameUpdate");
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
         * @param name        label used in the log line.
         * @param logInterval how often a summary is emitted (defaults to every 30 seconds).
         */
        explicit PerfMonitor(std::string name, std::chrono::milliseconds logInterval = std::chrono::seconds(30));

        /**
         * Create an RAII timer that records its scope duration into this monitor on destruction.
         * When debug logging is disabled the returned timer is inert (no timestamp, no recording).
         */
        [[nodiscard]] ScopedTimer scope()
        {
            return ScopedTimer(logger::isDebugEnabled() ? this : nullptr);
        }

        /**
         * Record a single measured duration. Emits and resets the summary when the log interval has elapsed.
         */
        void record(std::chrono::nanoseconds duration);

        /**
         * Emit the current summary (if any samples were collected) and reset the window immediately.
         */
        void flush();

    private:
        std::string _name;
        std::chrono::nanoseconds _logInterval;

        // The window starts lazily on the first recorded sample so that gaps (e.g. debug disabled)
        // don't inflate the reported window length.
        bool _windowStarted = false;
        std::chrono::steady_clock::time_point _windowStart;

        std::chrono::nanoseconds _sum{ 0 };

        // Per-call durations (nanoseconds) for the current window; sorted at flush for percentiles.
        std::vector<int64_t> _samples;
    };
}
