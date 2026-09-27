#include "PerfMonitor.h"

namespace f4cf::perf
{
    namespace
    {
        /**
         * Function-local static, not a namespace-level one: a monitor can be a static in another translation
         * unit whose initializer runs before this one's, and registering into a not-yet-constructed vector
         * would be undefined behaviour. It also outlives every monitor, since its construction completes
         * inside the first monitor's constructor, so unregistering from a destructor stays safe.
         */
        std::vector<PerfMonitor*>& registry()
        {
            static std::vector<PerfMonitor*> monitors;
            return monitors;
        }
    }

    PerfMonitor::PerfMonitor(std::string name, const std::chrono::milliseconds logInterval)
        : _name(std::move(name)),
          _logInterval(logInterval)
    {
        registry().push_back(this);
    }

    PerfMonitor::~PerfMonitor()
    {
        std::erase(registry(), this);
    }

    const std::vector<PerfMonitor*>& PerfMonitor::all()
    {
        return registry();
    }

    void PerfMonitor::record(const std::chrono::nanoseconds duration)
    {
        const auto now = PerfStats::Clock::now();

        if (logger::isDebugEnabled()) {
            _logWindow.record(duration, now);
            if (now - _logWindow.firstSampleAt() >= _logInterval) {
                flush();
            }
        }

        if (isCollecting()) {
            _stats.record(duration, now);
        }
    }

    void PerfMonitor::flush()
    {
        if (_logWindow.count() > 0) {
            const auto s = _logWindow.summary(PerfStats::Clock::now());
            const double callsPerSec = s.windowMs > 0.0 ? static_cast<double>(s.count) * 1000.0 / s.windowMs : 0.0;

            logger::info("[Perf] {}: n={} ({:.0f}/s) avg={:.3f}ms p95={:.3f}ms p99={:.3f}ms min={:.3f}ms max={:.3f}ms busy={:.1f}% over {:.1f}s{}",
                _name,
                s.count,
                callsPerSec,
                s.avgMs,
                s.p95Ms,
                s.p99Ms,
                s.minMs,
                s.maxMs,
                s.busyPct,
                s.windowMs / 1000.0,
                s.percentilesTruncated ? " (p95/p99 over the first samples only)" : "");
        }

        _logWindow.reset();
    }
}
