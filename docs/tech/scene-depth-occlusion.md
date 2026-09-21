# Scene depth occlusion — the side-by-side harness

`f4cf::render::sceneDepth` captures the engine's depth-stencil at a `CommitGraphicsState` call site
so framework overlays can be hidden behind the world. It works on every setup tested. This document
covers the part that is **not** the capture: the instrumentation around it, what it measures, and
what it would take to change the answer it gives.

Background and the full plan this implements:
`Modding-Reference/F4VR/knowledge-base/scene_depth_capture_hardening.md` (work item A).

Files: [`SceneDepthCapture.cpp`](../../src/render/SceneDepthCapture.cpp) (the capture and the
election), [`SceneDepthDiagnostics.h`](../../src/render/SceneDepthDiagnostics.h) (everything below),
[`SceneDepthResample.cpp`](../../src/render/SceneDepthResample.cpp) (the upscaler copy).

## Why there is a harness at all

The subsystem answers two questions, and they fail differently.

**"Is this the code I think it is"** — the hook addresses. Checkable, and checked: a 19-byte prologue
`memcmp`, an opcode test, and a decoded target that must be either the expected function or
executable memory outside the game image (another plugin's hook of the same seam, chained onto).
A wrong address declines to install, the overlay draws unoccluded, and the log names the guard.

**"Which of these passes is the world"** — the viewport election. *Not* checkable. Every way it can
go wrong produces a correctly-dimensioned, entirely valid-looking depth buffer with the wrong
contents, and nothing downstream can tell:

- a renderer mod adds passes, so the copy lands before the world finished drawing;
- an upscaler with a different pass structure, so the most-used viewport is a post-process one;
- pass counts jittering by more than the two-frame minimum absorbs;
- a frame short of the planned copy pass, so the previous frame's depth is silently reused;
- more than four distinct viewports in a frame, so one is dropped from the election.

The harness turns each of those from silent into counted.

## The readout

Everything the render thread learns is published as relaxed atomics and read back on the game thread
by `internal::onGameFrameEnd()`, registered as a frame-end callback when the Submit hook installs.
The rows are tagged with the `SCENE-DEPTH` channel and appear **only when the debug-draw overlay is
already in use** — `watch()` is what switches that overlay on, and drawing an occluded panel should
not be what starts a mod paying for it.

| Row              | Says                                                                                     |
| ---------------- | ---------------------------------------------------------------------------------------- |
| `DEPTH ACQUIRE`  | the last submit's outcome by name, plus running ok / resampled / refused counts           |
| `DEPTH BUFFER`   | the captured buffer's extent, format and sample count                                     |
| `DEPTH WORLD`    | where the world was drawn in it, as pixels and as a percentage — `direct` or `upscaled`   |
| `DEPTH TALLY`    | the winner's pass count against the runner-up, group count, **`CAP HIT`**, winner `flips` |
| `DEPTH COPY`     | the planned copy pass against the frame's actual pass count, copies, **short frames**     |
| `DEPTH SHADOW`   | per decision, `divergences/comparisons`                                                   |
| `DEPTH REJECTED` | every capture stage that ever refused, with its count                                     |

Three of those are the ones to watch. `CAP HIT` means a viewport was dropped and could not win.
Short frames mean last frame's depth was reused. A winner one pass ahead of the runner-up is a coin
toss the next frame may flip — that one also logs itself, sampled.

The same stage counters are dumped inline with every capture failure, so a `scene depth capture
unavailable: …` line carries how often the other stages were reached.

### …and the same thing in the log

The HUD is for whoever is wearing the headset. Everything above also goes to the log, so a session
played without the overlay still says what the capture did:

- **Events, as they happen** — the first eight divergences of each kind with their values; a close
  election, a group-cap hit or a short frame, each sampled to once per 10s; the world viewport
  whenever it changes; the buffer shape when a new one is seen; every capture failure with the stage
  counters beside it.
- **Running totals, on change only** — the two `scene depth [strategy]:` / `scene depth tally:` lines
  carry the whole watch table verbatim, written when a *decision* changes and never merely because
  time passed. There is no heartbeat.

### Nothing here fires on a timer

Every line above is change- or fault-driven, which is what makes it affordable to leave on. The
summary is keyed on what the capture **decided** — strategy, buffer shape, world viewport, outcome,
group count, and whether each pathology has ever occurred — and the rule behind that key took three
sessions to get right, so it is worth stating plainly:

> **Nothing that counts up, and nothing that jitters, may be in the key.**

Both kinds got in and both produced a 30-second heartbeat that said nothing. The counters first — the
acquire and copy totals move every frame, the pass counts jitter ±1, the divergence counts climb
because the copy-pass shadow disagreed by design on half of all frames, and short frames accrue
forever on a profile that drops one in four hundred. Counts now enter the key only as *whether it has
ever happened*. Then the viewport, in disguise: the elected width alternates between 4731 and 4728 on
a fixed upscaler setting — a difference the election's own 8-pixel tolerance ignores and a raw key did
not. It is quantised to a whole percent of the buffer, which is stable across that jitter and still
moves on every quality step (50 / 58 / 67 / 77 / 100).

So a session where the capture simply works says so once and then goes quiet, and **a pair appearing
in a log means something changed there, at that timestamp.**

All of it is `info`, so the default `iLogLevel = 2` collects a session with nothing to configure —
which is the point of leaving it on rather than behind a switch. The overlay ships off, so the log is
the only channel, and a diagnostic that has to be enabled first is one that is missing from every bug
report. The parts that cost something to run are the ones behind a switch: see `bSceneDepthDiagnostics`.

## What runs in the shadow — and what it found

**Nothing here replaces anything.** The shipped election decides; an alternative computes its answer
beside it and a comparator counts the disagreements.

| Decision         | Active                                        | Shadow                                                |
| ---------------- | --------------------------------------------- | ----------------------------------------------------- |
| `WorldRegion`    | election over **every** pass into the buffer   | election over **depth-testing** passes only            |
| `CopyPass`       | the **two-frame minimum** of world-pass counts | the **previous frame's count alone**                   |
| `DepthSelection` | the buffer matched **by size**                 | *(work item C — resolve by logical scene-depth role)*  |

The depth-testing filter rejects passes whose depth state is disabled, or whose `DepthFunc` is not
one of LESS / LESS_EQUAL / GREATER / GREATER_EQUAL: neither is a pass drawing against the world's
depth, so neither should have a say in the election. It was made a shadow rather than a filter on the
shipped path precisely because adopting it would quietly change which viewport wins.

The copy-pass shadow measures the jitter the two-frame minimum exists to absorb. A divergence means
the previous frame drew fewer world passes than this one — the frame where the minimum is doing work.

### Both answered, so both are dormant

Two sessions, two different mod sets, ~37,000 frames, an upscaler cycled through five quality levels
in one of them:

| | Full modlist + Fallout4Upscaler | ROCK + RPS UI, PALM wheel open |
| --- | --- | --- |
| `WorldRegion` divergence | **0 / 29,299** | **0 / 8,076** |
| `CopyPass` divergence | 14,645 / 29,298 (50.0%) | 4,031 / 8,075 (49.9%) |
| viewport groups (cap 4) | 1–2 | 2 |
| elected vs runner-up | 12 vs 1 | 11 vs 1 |
| capture rejections | none | none |

**`WorldRegion`: the filter never changes the answer.** Zero divergence across both mod sets. It is
safe, and it is pointless — it costs an `OMGetDepthStencilState` per committed graphics state to
arrive where the shipped election already is.

**`CopyPass`: the shipped policy wins, decisively.** Half of all frames diverge, and every logged
instance has the same shape — *"the two-frame minimum says pass 14, this frame count alone says 15
(the previous frame drew 14)"*. World-pass counts alternate by one, which is exactly what the minimum
exists to absorb; the alternative would plan the copy at a pass half the frames never reach, make no
copy, and reuse the previous frame's depth **every other frame**.

So the shadows are **off by default** (`[Debug]` / `bSceneDepthDiagnostics`). The code stays rather than
being deleted: re-running the experiment against a setup where occlusion misbehaves is worth more
than the lines it costs, and far more than re-deriving it from the plan document later. Turn the key
on and both comparisons resume, with the first eight divergences of each kind logged **with the
values behind them** and the rest counted — a divergence count with no context is not actionable, and
formatting one per frame on the render thread is not affordable.

## The two keys

Both live in `[Debug]` and hot-reload like every other INI value.

`bSceneDepthDiagnostics` (default **false**) is for whoever came to look at the capture: it draws the
watch rows above, and re-runs the side-by-side comparison — see *Both answered, so both are dormant*.
Neither is on for everyone who opens the debug overlay, and the shipped policies decide either way.

`sSceneDepthStrategy` picks which policy decides.

| Value       | Effect                                                                                   |
| ----------- | ---------------------------------------------------------------------------------------- |
| `auto`      | the shipped election (default)                                                            |
| `depthpass` | promotes the depth-testing shadow to the deciding tally                                   |
| `direct`    | captures, but never resamples — right without an upscaler, wrong with one                 |
| `off`       | captures nothing, hands out nothing; overlays draw on top, as before occlusion existed    |

`direct` and `off` are support switches: between them they say whether a misdrawn overlay is the
capture, the copy, or neither. The readout keeps working under all four — `direct` still counts
passes, it only never acts on them. `depthpass` implies the filtered tally is fed whether or not
`bSceneDepthDiagnostics` is on, since it is the one deciding.

## Validation on the acquire path

Ported from RPS UI Framework (GPL-3.0-or-later, reused with attribution), whose point is that every
rejection names the rule that rejected it rather than handing back a silent null view:
`no-capture`, `stale-capture`, `submitted-layout-mismatch`, `different-device`, `no-world-copy`,
`strategy-off`, and the two successes `ok` / `ok-resampled`.

Three of those are new rules, not just new names:

- **Layout** — the mip the depth view addresses must be the submitted texture's extent, and both must
  carry the same MSAA count and quality. A pair whose extents differ will not bind; a pair differing
  only in sample count reads silently wrong.
- **Device identity** — compared through each texture's `IUnknown`, not its `ID3D11Device` pointer.
  COM only guarantees identity through `IUnknown`, so two interface pointers to one device may
  legitimately differ, and comparing those raw would refuse a perfectly good capture.
- **Frame epoch by destructor** — `sceneDepth::FrameScope` closes the capture frame however the
  submit path leaves, early return or throw included. Before, an early return left the counter where
  it was and every commit of the next frame was counted into the last one.

## Short frames, and how stale a copy may be

A frame that draws fewer world passes than the plan expects reaches no copy at all, and the previous
frame's is used instead. `WORLD_COPY_MAX_AGE_FRAMES` says how far back that may reach; it is **2**.

One was the original choice and measurement rejected it. Short frames turned out to be neither rare
nor only an upscaler artefact — **hiding any drawn object removes a pass**, so a mod's own meshes
cause them. In the ROCK+RPS session they ran at roughly one frame in four hundred with the upscaler
sitting still at 77%, and three times in 95 seconds two landed close enough together to exhaust a
one-frame window, which the log shows as:

```
10:37:18.795  FlashlightMesh: hidden
10:37:18.809  occluded overlay: scene depth NOT AVAILABLE, drawing on top
10:37:18.820  occluded overlay: scene depth bound, testing against the world
```

An overlay popping in front of the world for a frame. Two frames of lag on a resampled copy go unseen
where that does not, so the window is two.

**Confirmed on the same profile:** with the window at two, 17 short frames over ~28,000 produced
**zero** drops. The one `NOT AVAILABLE` left in the session is a different case — going from a world
drawn over the whole buffer back to an upscaled one, where the direct path had already released the
copy and the next frame has to make a new one. One frame, only at an upscaler quality change, and not
something a wider window can fix.

> This is the one measured argument for work item B, and only for **half** of it. B's spatial half —
> reading the world's viewport off the engine's render context — would replace an election that has
> now been right 37,375 times out of 37,375, and is not worth building. B's temporal half — a semantic
> event (`SetViewportFromCamera`, or `SelectDepthStencil` away from DS logical 1) in place of an
> ordinal pass number — is what actually addresses a frame drawing a different number of passes than
> the last one. Revisit it only if the two-frame window proves insufficient.

## Exit criterion

Side-by-side is a transition, not a destination. A shadow that nobody ever promotes is a second
implementation maintained forever.

**Adopt a shadow, and delete what it replaces, when it has run with zero divergences across all
three of:**

1. a stock setup, no upscaler;
2. an upscaled setup (DLSS/FSR driving the engine's dynamic resolution);
3. a setup with at least one other overlay mod installed that hooks the same seam.

**Abandon a shadow** when it diverges and the logged context shows the shipped answer was the right
one.

**Both shadows reached the second outcome, and neither reached the first**, so both were stood down
rather than promoted — `WorldRegion` because it never disagreed and therefore never earned the D3D
query it costs, `CopyPass` because it disagreed on half of all frames and was wrong every time. The
one departure from the plan is that the loser was **made dormant rather than deleted**: the code is
inert behind `bSceneDepthDiagnostics`, so the experiment can be re-run against a setup where occlusion
misbehaves instead of being rebuilt from the plan document.

⚠️ That concession only holds while the dormant path still compiles and still means what it says.
A shadow left in the tree is a path nobody exercises, and the plan's own warning applies: shadow code
rots. If it ever stops being cheap to keep honest, delete it.

The rest of the harness is not part of that bargain and stays always-on: `CAP HIT`, short frames, a
close election and every capture rejection are reported whether or not a shadow runs. They are what
found the short-frame behaviour above, which no shadow would have.

## What is deliberately not here

- **`fFlowFlagN` as the development switch.** The plan called for one; `sSceneDepthStrategy` does the
  same job better — named values, hot-reloaded, and something a user with a broken setup can be asked
  to set. The flow flags remain free for ad-hoc work.
- **An expensive double-copy comparator.** Two policies cannot both copy into one buffer, so only the
  *decision* is shadowed, never the action. Taking a second copy into a scratch buffer to diff the
  pixels is worth building when a divergence is actually observed, and not before.
- **Work items B and C.** B reads the world's viewport off the engine's own render context
  (`ctx + 0x1ee0 + 0x90`, one offset from a base `RenderUtils.cpp` already walks every frame);
  C resolves the depth buffer by its logical scene-depth role instead of by size. Both are pure
  reads, both slot into `Comparison::WorldRegion` / `Comparison::DepthSelection` as further shadows.
  See the hardening plan, §5 and §6.
