# `perf/` — Hot-Path Timing

Namespace: `f4cf::perf`

Cheap timing of the code a mod runs every frame, read on demand. A measured place is a **site**:
one macro declares it and times the rest of the block, on any thread. Each site keeps a histogram
of its call durations, how much of that went to the sites it opened (so self time is total minus
children), and the site that opened it, so the sites nest into a tree per thread.

Recording is off until something switches it on with `perf::setEnabled(true)`; while off, a site
costs one relaxed atomic load and takes no timestamp. Two things read it:

- The mod's [devbench tool](../devbench/README.md) switches recording on when it is first used,
  and its `perf` action reads every site as a JSON tree, a flat map or a text table.
- Without devbench, `[Debug] sDumpDataOnceNames` does it through the INI: `perf` writes the text
  table to the mod log, and `perf_reset` does too, then starts a new window; the first
  `perf_reset` switches recording on ([debug-config.md](../../docs/debug-config.md)).

Sites time wall clock on the calling thread and cannot see the GPU. What they cost is read
against the **frame context**, recorded over the same window while recording is on: the game's
frame interval, the frame budget at the headset's refresh rate, and in VR the compositor's timing
of each frame, which is where GPU time, late starts (the game being CPU-bound) and reprojected
frames show. Each site's share of the budget (`%budget`) is its time per frame over the budget.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`Perf.h`](Perf.h) / [`Perf.cpp`](Perf.cpp) | The `F4CF_PERF_SCOPE` / `F4CF_PERF_FUNCTION` macros, `Site` (a measured place, its stats and its caller), `Scope` (the RAII timer behind the macros), `dynamicSite()`, and the switch: `setEnabled()`, `reset()`, `windowStart()`, `sites()`. |
| [`Report.h`](Report.h) / [`Report.cpp`](Report.cpp) | `readReport()` — one read of every site, nested by caller and grouped by the thread of each outermost site, the game thread first, with the window and the frame count. `formatReport()` — the same as an indented text table. What the devbench `perf` action and the `perf` debug dump show. |
| [`FrameContext.h`](FrameContext.h) / [`FrameContext.cpp`](FrameContext.cpp) | `FrameContext` — the whole frame over the window: the game's frame interval, the headset's refresh rate (the budget), and the VR compositor's timing of the same frames (GPU time, late starts, reprojected and dropped frames). Plain std; `CompositorFrame` carries the OpenVR fields it keeps. |
| [`FrameSampler.h`](FrameSampler.h) / [`FrameSampler.cpp`](FrameSampler.cpp) | `internal::sampleFrame()` — what `ModBase` calls every frame to fill the frame context: the interval, and every half second the frames since the last read, one `IVRCompositor::GetFrameTiming` each (never `GetFrameTimings`, which overran its array). The one place perf calls OpenVR, on the game thread. |
| [`Histogram.h`](Histogram.h) | `Histogram` — the lock-free duration histogram behind every site: log-linear buckets over nanoseconds, percentiles within ~3%, exact count/sum/min/max, and `Snapshot`s that merge by addition. Plain std, so it can be unit tested. |

## Usage

```cpp
#include "perf/Perf.h"

void Skeleton::onFrameUpdate()
{
    F4CF_PERF_FUNCTION();              // the whole function, labelled "Skeleton::onFrameUpdate"
    {
        F4CF_PERF_SCOPE("arms");       // a block; it nests under Skeleton::onFrameUpdate
        {
            F4CF_PERF_SCOPE("solveArms");
            // ...
        }
    }
}

// A label only known at runtime, such as a callback's tag: look the site up once, keep it, and time with a Scope.
perf::Site& site = perf::dynamicSite(__FUNCTION__, tag);
{
    const perf::Scope scope(site);
    callback();
}

// Read every site. The window is whatever lies between two resets.
perf::setEnabled(true);
// ... later, after holding the condition being measured ...
for (const auto* site : perf::sites()) {
    const auto stats = site->read();
    const auto s = stats.durations.summary();
    logger::info("{}/{}: n={} p95={:.3f}ms self={:.3f}ms", site->shortFunction(), site->label(), s.count, s.p95Ms,
        static_cast<double>(stats.selfNs()) / 1e6);
}
perf::reset();
```

## What the framework times

Every mod gets these sites without code of its own:

| Site | Where it shows | What it says |
|------|----------------|--------------|
| `ModBase::onFrameUpdateSafe` | the game thread's root | the mod's whole frame; its call count is the frame count |
| `internal::sampleFrame`, `Tool::publishState`, `Tool::runQueuedCommands` | under the frame | what measuring and the devbench tool cost the frame, on the frames they do work |
| `VRControllersManager::update`, `VRControllersSuppressor::update`, `VRControllersHaptic::update`, `DebugDraw::onFrameStart` / `onFrameEnd`, `DebugAdjuster::onFrameUpdate`, `frameEndCallbacks` | under the frame | the framework's per-frame work; debug draw only once something has drawn |
| `GetControllerState:own` / `:other`, `GetControllerStateWithPose:own` / `:other` | under the site that polls, or a root | every controller-state poll through the suppressor's vtable hooks, the mod's own apart from everyone else's; `/frame` is how often each reads |
| `render::drawToSubmittedTexture`, each draw callback under it by its registered name | the render thread's root | overlay drawing in the Submit hook, CPU time only; absent while nothing draws |
| `UIManager::onFrameUpdate` | where the mod calls it | vrui, while a UI is attached |

## Notes

- **Name the code, not the path.** A label only has to be unique within its function (`"arms"`,
  not `"Skeleton::arms"`): the function tells blocks apart, and the caller links supply the
  nesting. `F4CF_PERF_FUNCTION()` names the site after the function it is in, so it can't drift
  from the code.
- **Nesting is per site, not per call path.** A site records the first site it ran inside as its
  caller. A site called from several places keeps one set of stats and one caller, the first seen,
  and `hasMultipleCallers()` says so. Self time is still right, since each call charges its own
  caller. Each thread has its own roots, so a render-thread site never nests under a game-thread one.
  A report lists the sites under a caller in the order they run in a frame (`Site::runOrder`), not
  the order they were constructed.
- **Threads and frames.** A site remembers the thread it first ran on, which is how a report groups
  the outermost sites by thread. `ModBase` times its whole frame with the frame site
  (`declareFrameSite`, around `onFrameUpdateSafe`): its thread is the game thread, and its call
  count is the frame count that `callsPerFrame` divides by.
- **Safe on any thread.** Recording is lock-free atomics; reading a site (`read()`) only loads them,
  so a reader on another thread never stalls the frame. A sample recorded during a read or a drain
  can land partly in it.
- **SEH-safe.** A `Scope` keeps the site it replaced by value rather than pointing at the enclosing
  `Scope`, so a fault recovery that skips destructors leaves nothing dangling: the next outer
  `Scope` to close puts the thread back in order.
- **Per DLL.** The framework is a static library, so `setEnabled()`, `reset()` and `sites()` cover
  the calling mod's sites only, never another mod's.
- **Memory.** A site is about 1.4 KB and lives as long as the DLL; dynamic sites are never freed.
  Nothing allocates per call.
