# `perf/` — Hot-Path Timing

Namespace: `f4cf::perf`

Cheap CPU timing of the functions a mod runs every frame. A measured site is one static
`PerfMonitor` plus an RAII `scope()` timer, and each monitor feeds two independent consumers:

- **The log.** While the logger is at debug level (`iLogLevel <= 1`) it logs a one-line summary
  every N seconds (default 60, configurable per monitor).
- **On-demand reads.** While `PerfMonitor::setCollecting(true)` is on, every monitor also
  accumulates into `stats()`, which only `resetStats()` clears. `PerfMonitor::all()` lists every
  monitor in the DLL, the framework's own included (`ModBase::onFrameUpdateSafe` is the mod's whole
  frame, `UIManager::onFrameUpdate` its vrui update), so a tool can read every site at once over
  whatever window it chooses.

With both off, `scope()` returns an inert timer: the cost at a site is one log-level check and one
relaxed atomic load. This measures CPU time on the calling thread only; it cannot see GPU time.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`PerfMonitor.h`](PerfMonitor.h) | `PerfMonitor` — a measured site: RAII `scope()` timer, periodic log line, on-demand `stats()`, and the `all()` / `setCollecting()` statics. Game thread only. |
| [`PerfStats.h`](PerfStats.h) | `PerfStats` — the duration accumulator behind it: count, total, avg, min, max, exact p95/p99 up to a sample cap, and busy share of the window. Plain std, so it can be unit tested, and it serves a site `PerfMonitor` can't, such as one off the game thread. |

## Usage

```cpp
#include "perf/PerfMonitor.h"

// Logs e.g. "[Perf] Pipboy::onFrameUpdate: n=897 (90/s) avg=0.412ms p95=0.900ms p99=1.800ms min=0.180ms max=3.900ms busy=3.7% over 60.0s"
void Pipboy::onFrameUpdate()
{
    static perf::PerfMonitor perf("Pipboy::onFrameUpdate");  // default 60s log window
    const auto timer = perf.scope();
    // ... work being measured ...
}

// Read every site on demand, on the game thread. The window is whatever lies between two resets.
perf::PerfMonitor::setCollecting(true);
// ... later, after holding the condition being measured ...
for (auto* monitor : perf::PerfMonitor::all()) {
    const auto s = monitor->stats().summary(perf::PerfStats::Clock::now());
    logger::info("{}: n={} avg={:.3f}ms p99={:.3f}ms busy={:.1f}%", monitor->name(), s.count, s.avgMs, s.p99Ms, s.busyPct);
    monitor->resetStats();
}
```

## Notes

- **Name sites `Class::method`.** On-demand readers list every monitor in the DLL side by side, so
  the name is the only thing telling two sites apart.
- **Game thread only.** Constructing a monitor registers it in `all()`, and a reader walks that list
  and every monitor's `stats()` on the game thread, so a monitor used on another thread races with
  it. Time another thread with a `PerfStats` of its own.
- **Collection is per DLL.** The framework is a static library, so `setCollecting()` and `all()`
  cover the calling mod's monitors only, never another mod's.
- **Memory.** Samples are kept for exact percentiles, up to `PerfStats::MAX_SAMPLES` (60,000, about
  11 minutes at 90 Hz, 480 KB) per window; past that count/avg/min/max stay exact and the summary
  reports `percentilesTruncated`. Vectors keep their capacity across resets, so steady state has no
  per-call allocations.
