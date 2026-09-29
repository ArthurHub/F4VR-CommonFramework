# `perf/` — Hot-Path Timing

Namespace: `f4cf::perf`

Cheap timing of the code a mod runs every frame, read on demand. A measured place is a **site**:
one macro declares it and times the rest of the block, on any thread. Each site keeps a histogram
of its call durations, how much of that went to the sites it opened (so self time is total minus
children), and the site that opened it, so the sites nest into a tree per thread.

Recording is off until something switches it on with `perf::setEnabled(true)`; while off, a site
costs one relaxed atomic load and takes no timestamp. Two things read it:

- The mod's [devbench tool](../devbench/README.md) switches recording on when it arms: when
  devbench is installed, a Tracy viewer connects, or `perf_reset` asks for it. Its `perf` action
  reads every site as a JSON tree, a flat map or a text table.
- Without devbench, `[Debug] sDumpDataOnceNames` does it through the INI: `perf` writes the text
  table to the mod log, and `perf_reset` does too, then starts a new window; the first
  `perf_reset` switches recording on ([debug-config.md](../../docs/debug-config.md)).

Sites time wall clock on the calling thread. **GPU sites** hold the GPU time of work the mod
issues itself, read back from GPU timestamps ([GPU time](#gpu-time)); the framework's overlay
drawing is timed that way. What they cost is read against the **frame context**, recorded over the
same window while recording is on: the game's frame interval, the frame budget at the headset's
refresh rate, and in VR the compositor's timing of each frame, which is where the whole frame's GPU
time, late starts (the game being CPU-bound) and reprojected frames show. Each site's share of the
budget (`%budget`) is its time per frame over the budget.

For the frame by frame view, build the **`Tracy` configuration**: every site is then also a
[Tracy](https://github.com/wolfpld/tracy) zone, on a timeline per thread (see [Tracy](#tracy)).

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`Perf.h`](Perf.h) / [`Perf.cpp`](Perf.cpp) | The `F4CF_PERF_SCOPE` / `F4CF_PERF_FUNCTION` macros, `Site` (a measured place, its stats and its caller), `Scope` (the RAII timer behind the macros), `dynamicSite()`, and the switch: `setEnabled()`, `reset()`, `windowStart()`, `sites()`. |
| [`Report.h`](Report.h) / [`Report.cpp`](Report.cpp) | `readReport()` — one read of every site, nested by caller and grouped by the thread of each outermost site, the game thread first, with the window and the frame count. `formatReport()` — the same as an indented text table. What the devbench `perf` action and the `perf` debug dump show. |
| [`FrameContext.h`](FrameContext.h) / [`FrameContext.cpp`](FrameContext.cpp) | `FrameContext` — the whole frame over the window: the game's frame interval, the headset's refresh rate (the budget), and the VR compositor's timing of the same frames (GPU time, late starts, reprojected and dropped frames). Plain std; `CompositorFrame` carries the OpenVR fields it keeps. |
| [`FrameSampler.h`](FrameSampler.h) / [`FrameSampler.cpp`](FrameSampler.cpp) | `internal::sampleFrame()` — what `ModBase` calls every frame to fill the frame context: the interval, and every half second the frames since the last read, one `IVRCompositor::GetFrameTiming` each (never `GetFrameTimings`, which overran its array). While a Tracy viewer is connected it reads every frame and plots them. The one place perf calls OpenVR, on the game thread. |
| [`GpuTimer.h`](GpuTimer.h) / [`GpuTimer.cpp`](GpuTimer.cpp) | `GpuTimer` — GPU time of the work a mod issues on the D3D11 immediate context, split into spans by timestamps, read back 1-3 frames later without waiting, into GPU sites (`SiteKind::Gpu`). What the Submit host times every overlay layer with. |
| [`Tracy.h`](Tracy.h) / [`Tracy.cpp`](Tracy.cpp) | The Tracy client's side of perf: `startTracy()` (`ModBase` calls it), `isTracyConnected()`, `tracyFrameMark()`, `tracyThreadName()`, `tracyMessage()`, `tracyPlot()`, and `TRACY_BUILT` for `if constexpr`. Outside the `Tracy` configuration they are inline no-ops. |
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
| `GetControllerState`, `GetControllerStateWithPose` | under the site that polls | the mod's own controller-state polls (`SelfControllerReadScope`) through the suppressor's vtable hooks; `/frame` is how often the mod reads. Everyone else's polls are not timed: every mod's hook sees the same ones, so each mod's table would show the whole game's |
| `render::drawToSubmittedTexture`, each draw callback under it by its registered name | a root on the game thread, after the frame | overlay drawing in the Submit hook, which FO4VR calls on the game thread about a millisecond after the mod's frame (on the loading screen's own thread during a load); CPU time only; absent while nothing draws |
| `UIManager::onFrameUpdate` | where the mod calls it | vrui, while a UI is attached |
| `render::drawToSubmittedTexture`, and under it `setup` and each draw callback by its registered name | the `gpu` tree | the GPU time of the overlay drawing: the shared setup, then each layer (vrui panels, ImGui canvases, debug draw, activation-sphere icons); read back 1-3 frames late |

## GPU time

A **GPU site** (`SiteKind::Gpu`) holds the GPU time of work the mod issues itself on the D3D11
immediate context. `GpuTimer` measures it: a timestamp where the work starts and one after each
span, so N spans take N+1 timestamps, inside a disjoint query that says whether the GPU clock held.
Each whole frame goes into the timer's root site, and each span into its own site under it. A
report lists the GPU sites last, as a tree of their own (`gpu`), since they run on no thread.

```cpp
static perf::Site gpuSite("MyLayer::draw", nullptr, __FILE__, __LINE__, perf::SiteKind::Gpu);
static perf::Site blurSite("MyLayer::draw", "blur", __FILE__, __LINE__, perf::SiteKind::Gpu);
static perf::GpuTimer gpuTimer(gpuSite, "gpu.myLayerMs");

auto gpuFrame = gpuTimer.begin(context);   // on the thread that issues the work
drawBlur(context);
gpuFrame.mark(blurSite, "gpu.myLayer.blurMs");
// the frame ends with gpuFrame's scope
```

- **The Submit host times every layer**, with no code in the layers: the whole draw as
  `render::drawToSubmittedTexture`, and under it the shared `setup` and each draw callback by its
  registered name, so every layer has a GPU time next to its CPU time.
- **Read 1-3 frames late**, without waiting or flushing. Four frames can be in flight; a frame is
  not timed while all four are, and one the GPU clock was disjoint over is dropped, as are the frames
  still in flight at a reset.
- **Only while measured**: nothing is issued unless perf is recording or a Tracy viewer is
  connected.
- **What a span means**: the GPU time between two points in the command stream, with idle time and
  overlap at its edges, so a very short span is mostly its edges. A lightly loaded GPU also clocks
  down, which inflates short times.
- **Only the mod's own work.** What the engine renders because of the mod (a body, a light and its
  shadow map, NIF widgets) is part of the frame context's GPU time and can't be timed from here.

## Tracy

The `Tracy` build configuration is a Release build with the Tracy profiler client v0.14.1 compiled
in. Every site is then also a Tracy zone, so the Tracy viewer shows what the tables above add up:
which frame was slow, what ran in it, and on which thread. The mod needs no code of its own for
it, and the other configurations never compile Tracy in.

1. Build it with the mod's `tracy` build preset (`cmake --build --preset tracy`) or
   `cmake --build build --config Tracy`. The DLL is copied to the mod folder like any other build's,
   over the Release one; a later Release build copies back only when it relinks.
2. Start the game, then the Tracy viewer (`tracy-profiler.exe`) of the same release, v0.14.1, and
   connect to the mod in its list of local clients. `tracy-capture.exe -a 127.0.0.1 -o <file>.tracy`
   records one to a file instead.

The client only listens on localhost, and costs next to nothing until a viewer connects: nothing is
buffered before. A capture holds:

- **Zones**: every site, named by its label, with the function, file and line.
- **Frames**: one per call of `ModBase::onFrameUpdateSafe`. The first frame of a capture spans from
  the plugin loading to the connection; skip it.
- **Threads**: the game thread shows as *Main thread*, since Tracy names the thread that started it
  so, whatever it is called later. The Submit hook's drawing is on it too during play, after the
  mod's frame; during a loading screen it comes from the loading screen's own thread.
- **Plots**: `frame.intervalMs` every frame, and one point per compositor frame for `vr.gpuMs`,
  `vr.gameGpuMs`, `vr.lateStartMs`, `vr.headroomMs`, `vr.reprojectedCpu`, `vr.reprojectedGpu` and
  `vr.dropped` (the frame context's numbers), whether perf recording is on or not. Each GPU site
  timed is a plot too, 1-3 frames after the frame it times: `gpu.drawMs`, `gpu.draw.setupMs` and
  `gpu.draw.<callback>Ms` for the overlay drawing.
- **Messages**: every [devbench event](../devbench/README.md#events), with its topic and payload,
  even without devbench. A viewer connecting arms the devbench tool, so events that come from the
  mod's state capture (a flag changing) show from then on too.

Code that wants more includes [`Tracy.h`](Tracy.h): `tracyPlot()`, `tracyMessage()` and
`isTracyConnected()` (to skip building what only a viewer would see) compile to nothing in the other
configurations.

- **One client per mod.** The framework is a static library, so each mod built this way runs its own
  client (port 8086, then 8087, ...) and makes its own capture; the viewer lists them by mod name. A
  saved capture is named after the game's executable whatever the mod.
- **Left out on purpose:** call stacks (Tracy would call `SymInitialize` on the game, which crash
  loggers rely on), sampling and system tracing (both need the game elevated), frame images, and
  Tracy's crash handler, so a crash still goes to the game's crash logger.
- **Started by `ModBase`** when the plugin loads (`startTracy`), not from a static initializer inside
  the loader lock. A site that runs before that is not a zone.
- **In the mod's `CMakeLists.txt`**, the `Tracy` configuration gets what Release gets: write
  `$<CONFIG:Release,Tracy>` where a setting is for Release only, and repeat the `/Ob2` to `/Ob3` fix
  for `CMAKE_CXX_FLAGS_TRACY`, as the mod template does. The framework adds the configuration itself,
  with Release's flags and the Release build of imported libraries.
- **Configuring downloads Tracy's source** once, pinned by hash. `-DF4CF_WITH_TRACY=OFF` drops the
  configuration and the download.

## Notes

- **Name the code, not the path.** A label only has to be unique within its function (`"arms"`,
  not `"Skeleton::arms"`): the function tells blocks apart, and the caller links supply the
  nesting. `F4CF_PERF_FUNCTION()` names the site after the function it is in, so it can't drift
  from the code.
- **Nesting is per site, not per call path.** A site records the first site it ran inside as its
  caller. A site called from several places keeps one set of stats and one caller, the first seen,
  and `hasMultipleCallers()` says so. Self time is still right, since each call charges its own
  caller. Each thread has its own roots, so a site on another thread never nests under a game-thread one.
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
