# `devbench/` — devbench Integration

[devbench](https://github.com/ArthurHub/devbench) is a separate F4SE plugin that runs a local MCP +
REST server inside the game, so an AI agent or a script can inspect and drive it. Other plugins add
their own tools to it through a small C ABI. This folder is the framework's side of that ABI.

> Part of the [F4VR Common Framework](../README.md) source tree.

## One tool per mod

`ModBase` registers one devbench tool for the mod once the game has loaded, named after
`Settings::name` in lowercase (`FRIK` → `frik`). A mod does nothing to get it; set
`Settings::devbenchTool = false` to opt out. When devbench isn't installed the mod logs one line
and carries on.

Everything the tool does is an **action**, chosen by its required `action` argument. There is no
default, so a call that names none is an error and never arms the tool. Every framework mod has the
same generic actions:

| Action | Arguments | What it does |
|--------|-----------|--------------|
| `health` | — | Which mod and framework version this is, the tool's `contract` number, the devbench build, whether the tool is armed, whether a state snapshot exists and its `liveness`, whether this is a [Tracy](../perf/README.md#tracy) build and a Tracy viewer is connected (`tracy`: `built`, `connected`), and the actions. Answered without the game thread, so it replies while the game is stalled. |
| `state` | — | What the mod is doing as of its last frame: `liveness` plus the mod's own state (see below). Answered from a snapshot without the game thread; the call that arms the tool waits for the first one. |
| `config` | `key`, `section` | One INI value as the mod sees it: the session override if one is set, otherwise the file's. |
| `set` | `key`, `value`, `section` | Override one INI value for the session (`ConfigBase::setConfigOverride`); the file is not written. |
| `clear` | `key`, `section`, or `all` | Drop one session override, or all of them. |
| `overrides` | — | The session overrides in effect. |
| `perf` | `reset`, `format` | Time spent in every [perf site](../perf/README.md) in the mod since the last reset, as a tree per thread (see below). `reset: true` clears every site after reading it. `format`: `tree` (the default), `flat` (one map keyed by function and label) or `text` (the tree as an indented table in `text`, the quickest to read). Answered without the game thread, so reading never stalls a frame. |

`section` defaults to the mod's name, the section the mod template uses; a mod whose main section
is named differently says so with `devbench::setDefaultConfigSection`.

`perf` nests each site under the site it runs inside, and groups the outermost ones by thread,
the game thread (the one `ModBase::onFrameUpdateSafe` runs on) first:

```json
{ "windowMs": 30012.4, "frames": 2701, "reset": false,
  "frame": { "hz": 90, "budgetMs": 11.11, "fps": 90.0, "intervalMs": { "avg": 11.11, "p50": 11.11, "p95": 11.52, "p99": 13.87, "max": 48.21 },
    "compositorFrames": 2690, "gpuMs": { "p95": 10.41 }, "gameGpuMs": { "p95": 9.12 }, "compositorGpuMs": { "p95": 0.84 },
    "lateStartMs": { "p95": 0.12 }, "headroomMs": { "p50": 1.64 }, "reprojected": { "cpu": 41, "gpu": 12 }, "dropped": 3, "misPresented": 5 },
  "threads": [
  { "thread": "8412", "gameThread": true, "sites": [
    { "label": "ModBase::onFrameUpdateSafe", "key": "ModBase::onFrameUpdateSafe", "n": 2701, "p95Ms": 0.61, "selfAvgMs": 0.04, "callsPerFrame": 1,
      "children": [ { "label": "FRIK::onFrameUpdate", "children": [ { "label": "FRIK::onFrameUpdateInner", "children": [ { "label": "Skeleton::onFrameUpdate",
        "children": [ { "label": "arms", "key": "Skeleton::onFrameUpdate/arms", "children": [ { "label": "solveArms" } ] } ] } ] } ] } ] },
    { "label": "SmoothMovementVR::onFrameUpdate" } ] } ] }
```

- Every site has `n`, `totalMs`, `avgMs`, `p50Ms`, `p95Ms`, `p99Ms`, `minMs`, `maxMs`,
  `selfTotalMs`, `selfAvgMs` (its time minus the sites under it), `busyPct` (of the window),
  `callsPerFrame` (per call of the frame site; `frames` counts those) and `budgetPct` (its time
  per frame as a share of the frame budget, once the refresh rate is known). `multipleCallers:
  true` means it also runs under other sites and is shown under the first one seen.
- `frame` is the whole frame over the same window, which the sites read against:
  - `hz` and `budgetMs` (one frame at the headset's refresh rate), `fps`, and `intervalMs`, the
    time between game frames as `ModBase` sees them.
  - In VR, the compositor's timing of the same frames, read on the game thread about every half
    second: `gpuMs` (the whole GPU frame), `gameGpuMs` (the game's scene), `compositorGpuMs`,
    `lateStartMs` (how late the game asked for poses: the CPU-bound signal), `headroomMs` (time
    the compositor idled that the game could have used), `reprojected` frames by the reason the
    runtime gives, `cpu` or `gpu`, `dropped` and `misPresented`. `compositorFrames` counts them.
  - The sites time the CPU; this is where the whole frame's GPU time shows.
- `gpu`, once the mod has timed GPU work (the framework's overlay drawing does while something
  draws): the GPU sites, nested like a thread's sites, read back from timestamps 1-3 frames late
  ([perf README](../perf/README.md#gpu-time)). The text table lists them last, under `gpu`, and
  `flat` keys them with a `gpu:` prefix, since a GPU site can share its function and label with a
  CPU one.
- `key` is the site's name in the `flat` view: its function, plus the label for a block.
- A site that recorded nothing since the reset is left out, unless a site under it recorded.
- `windowMs` counts from the last reset; the call that arms the tool is that reset.

`format: "text"` puts the same tree in `text` as a table, which is also what the `perf` debug dump
(`sDumpDataOnceNames`) writes to the log. An excerpt from FRIK, sites under a caller in the order
they run:

```
perf: 20.0s window, 1793 frames (89.6 fps), times in ms
frame: 90 Hz, budget 11.11 | interval p50 11.27 p95 11.80 p99 17.30 max 68.68
gpu:   p50 5.37 p95 5.64 p99 5.90 max 6.34 | game p95 5.37 | compositor p95 0.02
vr:    1761 frames | reprojected cpu 0, gpu 0 | dropped 0 | mispresented 8 | late start p95 1.34 | headroom p50 8.65
site                                                  n      avg      p50      p95      p99      max     self   /frame  %budget
game thread 46428
  ModBase::onFrameUpdateSafe                       1793    0.463    0.451    0.573    0.705    0.805    0.002     1.00      4.2
    VRControllersManager::update                   1793    0.016    0.005    0.084    0.088    0.107    0.002     1.00      0.1
      GetControllerStateWithPose                   3586    0.007    0.002    0.076    0.080    0.100    0.007     2.00      0.1
    FRIK::onFrameUpdate                            1793    0.443    0.434    0.541    0.672    0.765    0.001     1.00      4.0
      FRIK::onFrameUpdateInner                     1793    0.423    0.401    0.516    0.672    0.746    0.002     1.00      3.8
        Skeleton::onFrameUpdate                    1793    0.305    0.287    0.369    0.541    0.621    0.001     1.00      2.7
          arms                                     1793    0.241    0.225    0.303    0.483    0.556    0.001     1.00      2.2
            phaseAfterArmSolve                     1793    0.211    0.193    0.270    0.451    0.527    0.000     1.00      1.9
              AfterArmSolve:ROCK                   1793    0.211    0.193    0.270    0.451    0.527    0.207     1.00      1.9
        AfterWorldFinal:ROCK                       1793    0.072    0.072    0.080    0.104    0.199    0.072     1.00      0.6
```

```powershell
(irm http://127.0.0.1:8931/api/tool/frik -Method Post -ContentType application/json -Body '{"action":"perf","format":"text"}').text
```

A mod adds its own actions and the opening line of the tool's description:

```cpp
#include "devbench/DevBench.h"

void MyMod::onModLoaded(const F4SE::LoadInterface*)
{
    // an agent picks the mod's tool by this line, so name the features someone would ask about
    devbench::setToolDescription("MyMod: swimming comfort for VR - reduced bob, auto-surface, stamina");
    devbench::addAction({
        .name = "surface",
        .description = "float the player to the surface now",
        .arguments = { { "speed", { { "type", "number" }, { "description", "surface: units per second, default 50" } } } },
        .handler = [](const nlohmann::json& args) -> nlohmann::json {
            startSurfacing(args.value("speed", 50.0f));
            return { { "surfacing", true } };
        },
    });
}
```

- **Threads.** Devbench calls the tool on its own listener thread. An action runs on the game
  thread by default: it is queued, run at the start of the next frame right before the mod's
  `onFrameUpdate`, and the caller waits up to 2 seconds for it. `RunOn::Listener` runs it at once
  instead, for answers that must work while the game is stalled, as `health`, `state` and `perf` do; such a
  handler may only read atomics and data that no longer changes. A mod without `setupMainGameLoop` has no frame to run
  the queue in, so its actions go through F4SE's task interface.
- **Answers.** A handler returns a JSON object and gets `"ok": true` added; throwing turns the
  call into `{ "ok": false, "error": "<message>" }`, which carries no other keys.
- **Arming.** The first call of any action but `health` arms the tool (`devbench::isArmed()`) for
  the rest of the session, and anything that costs per-frame work waits for it: arming is what
  switches on perf site recording, so a perf window starts at the first use, and the per-frame
  state snapshot. `health` never arms, so probing every mod is free.
- **Arguments.** All actions share one flat input schema, so start each argument's description
  with the actions that read it (`"surface: ..."`). An argument several actions share is declared
  once; the first declaration wins.
- **Adding late.** Actions and the description added before `onGameLoaded` returns go out in
  the first registration; a later change re-registers the tool.

### State

While the tool is armed, `ModBase` publishes a snapshot after every `onFrameUpdate`, including
the frames the mod's update returned early from, and `state` answers from the latest one without
waiting for the game thread. That is what keeps it answering while the game is stalled, and all
of its values come from the same frame.

Every snapshot carries the framework's `liveness`, which a reader checks first:

| Key | Meaning |
|-----|---------|
| `frame` | Snapshots published since the tool was armed. This tool's counter, not the engine's frame. |
| `ageMs` | How old the snapshot is. A growing `ageMs` means the game thread stopped publishing. |
| `publishedAtMs` | When it was published, on `steady_clock`, which every DLL in the game process shares, so snapshots of different mods line up. |

The mod adds its own state with a provider: a plain-values struct, filled on the game thread and
turned into JSON on devbench's thread.

```cpp
struct SwimState
{
    bool underwater = false;
    float depth = 0;
    std::uint32_t sessionGeneration = 0;
};

devbench::setStateProvider<SwimState>(
    "swim: underwater and depth in game units",
    [](SwimState& state) { state.underwater = isUnderwater(); state.depth = currentDepth(); state.sessionGeneration = g_generation; },
    [](const SwimState& state) -> nlohmann::json {
        return { { "swim", { { "underwater", state.underwater }, { "depth", state.depth } } },
            { "liveness", { { "sessionGeneration", state.sessionGeneration } } } };
    });
```

- The keys the JSON has become the top level of the `state` answer, beside `liveness`. The keys
  of its own `liveness` object are added to the framework's block, for whatever tells whether the
  state is current, such as a generation counter. They never replace the framework's keys.
- The struct must hold **plain values only**. A snapshot outlives its frame, and a node or form a
  pointer pointed to may be gone by the time a reader formats it.
- The description goes into the `state` action's description, so an agent knows what the keys mean.
- Capturing costs one allocation and a copy per frame, and only while the tool is armed.

### Events

`devbench::emit(topic, makePayload)` publishes an event through devbench: MCP clients get it as a
notification, REST clients poll `GET /api/events?since=N`, and devbench stamps each one with the
engine frame. The topic is prefixed with the tool's name, so `emit("skeleton.ready")` in FRIK is
published as `frik.skeleton.ready`.

```cpp
devbench::emit("skeleton.ready", [&] { return nlohmann::json{ { "generation", _skeletonGeneration } }; });
devbench::emit("modeEntered");   // no payload
```

The payload is a function, called only when the event can go somewhere: while devbench is absent,
and before the game has loaded, an emit is one atomic load and builds nothing.

- The framework publishes these for every mod:

  | Event | When | Payload |
  |-------|------|---------|
  | `sessionLoaded` | After each save load and new game, once the mod's own `onGameSessionLoaded` has run | — |
  | `config.reloaded` | The file watcher applied an INI change from disk, or `ConfigBase::reload()` ran | `file`, `trigger` (`file` or `reload`) |
  | `config.override` | A session override was set or cleared, by anyone: a devbench call, the mod, or another mod through the mod's API | `section`, `key`, `value` (`null` when cleared), or `all: true` |
  | `input.suppression` | An owner started or stopped suppressing controller input | `owner`, its `left`/`right` `buttons`/`axes` (empty once released), the `effective` union, `owners` |

  Names are the input binding grammar's (`grip`, `trigger`, `menu`, `a`, `thumbstick`), and
  hands are physical. A suppression that is re-applied every frame publishes nothing; only a
  change does.
- Events of every mod share one ring of 256 in devbench, so emit them when something changes
  (a skeleton rebuilt, a mode entered), never every frame.
- Any thread. Before the tool is registered, and when devbench is absent, it does nothing.
- In a [Tracy build](../perf/README.md#tracy), while a Tracy viewer is connected, every event is also
  a message on its timeline where it happened, as the topic without the tool's prefix and the
  payload, with or without devbench.

### Several mods at once

Each mod DLL links its own copy of the framework, so each has its own tool, queue and config, and
they can't see each other. What they share is devbench's list of tool names, where registering a
name that exists **silently replaces** the earlier tool. Deriving the name from `Settings::name`,
which is also the DLL's name, keeps them unique; the framework logs a warning when its
registration replaced something. The generic actions are identical in every mod, and `health`'s
`framework` and `contract` fields say which version of them a mod has.

Only `GetBuildNumber`, `RegisterTool` and `EmitEvent` are used, slots every devbench has, so a mod
works with any devbench version. A later slot would have to be gated on `GetBuildNumber()` (see below).

## Vendored client API

| File | Contents |
|------|----------|
| [`DevBenchAPI.h`](DevBenchAPI.h) | The cross-plugin interface: `IDevBenchInterface001` (register tools, emit events), the handler types, and `GetDevBenchInterface001()`. |
| [`DevBenchAPI.cpp`](DevBenchAPI.cpp) | `GetDevBenchInterface001()`: the F4SE messaging handshake, with a `GetProcAddress` fallback on `devbench.dll`. |
| [`DevBenchAPI.LICENSE.txt`](DevBenchAPI.LICENSE.txt) | MIT license for these two files only; devbench itself is GPL-3.0. |

They are devbench's `include/` files, copied byte for byte from devbench **1.22.0**
([ArthurHub/devbench](https://github.com/ArthurHub/devbench) `cdf903e`). They stay unmodified
so that an update is a plain copy that can be diffed against upstream:

- Don't edit or reformat them. `.pre-commit-config.yaml` excludes them from every hook for that reason.
- To update, copy all three files from devbench's `include/` over these and note the new version
  and commit here.

The ABI is append-only: a vtable slot added in a later devbench exists only on hosts at least that
new, so check `GetBuildNumber()` before calling one. Every slot says which build introduced it.
