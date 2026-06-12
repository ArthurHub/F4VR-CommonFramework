#include "PerfMonitor.h"

#include <algorithm>
#include <cmath>

namespace f4cf::common
{
    namespace
    {
        constexpr double NS_PER_MS = 1'000'000.0;

        /**
         * Nearest-rank percentile (0-100) over an ascending-sorted sample vector, returned in milliseconds.
         */
        double percentileMs(const std::vector<int64_t>& sortedSamples, const double percentile)
        {
            if (sortedSamples.empty()) {
                return 0.0;
            }
            const auto rank = static_cast<size_t>(std::ceil(percentile / 100.0 * static_cast<double>(sortedSamples.size())));
            const size_t index = std::min(rank == 0 ? 0 : rank - 1, sortedSamples.size() - 1);
            return static_cast<double>(sortedSamples[index]) / NS_PER_MS;
        }
    }

    PerfMonitor::PerfMonitor(std::string name, const std::chrono::milliseconds logInterval)
        : _name(std::move(name)),
          _logInterval(logInterval)
    {}

    void PerfMonitor::record(std::chrono::nanoseconds duration)
    {
        if (duration < std::chrono::nanoseconds::zero()) {
            duration = std::chrono::nanoseconds::zero();
        }

        const auto now = std::chrono::steady_clock::now();
        if (!_windowStarted) {
            _windowStart = now;
            _windowStarted = true;
        }

        _sum += duration;
        _samples.push_back(duration.count());

        if (now - _windowStart >= _logInterval) {
            flush();
        }
    }

    void PerfMonitor::flush()
    {
        const auto now = std::chrono::steady_clock::now();
        const double windowMs = std::chrono::duration<double, std::milli>(now - _windowStart).count();

        if (!_samples.empty()) {
            std::sort(_samples.begin(), _samples.end());

            const auto count = _samples.size();
            const double totalMs = static_cast<double>(_sum.count()) / NS_PER_MS;
            const double avgMs = totalMs / static_cast<double>(count);
            const double minMs = static_cast<double>(_samples.front()) / NS_PER_MS;
            const double maxMs = static_cast<double>(_samples.back()) / NS_PER_MS;
            const double p95Ms = percentileMs(_samples, 95.0);
            const double p99Ms = percentileMs(_samples, 99.0);
            const double callsPerSec = windowMs > 0.0 ? static_cast<double>(count) * 1000.0 / windowMs : 0.0;
            const double busyPct = windowMs > 0.0 ? totalMs / windowMs * 100.0 : 0.0;

            logger::info("[Perf] {}: n={} ({:.0f}/s) avg={:.3f}ms p95={:.3f}ms p99={:.3f}ms min={:.3f}ms max={:.3f}ms busy={:.1f}% over {:.1f}s",
                _name,
                count,
                callsPerSec,
                avgMs,
                p95Ms,
                p99Ms,
                minMs,
                maxMs,
                busyPct,
                windowMs / 1000.0);
        }

        _sum = std::chrono::nanoseconds::zero();
        _samples.clear(); // keeps capacity, so steady-state has no per-call allocations
        _windowStarted = false;
    }
}
