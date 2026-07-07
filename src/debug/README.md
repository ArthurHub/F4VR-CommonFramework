# `debug/` — In-World Debug Draw Overlay

Immediate-mode debug drawing anywhere in the VR world: boxes, spheres, lines, arrows, axes, cones,
capsules, grids, arbitrary polylines/meshes, screen-space HUD text, world-anchored labels, and a
"name: value" watch table. Namespace `f4cf::debug`.

Rendering is a D3D11 wire/text renderer injected at the OpenVR `IVRCompositor::Submit` vtable hook,
reusing the engine's own per-eye camera matrices, so shapes land exactly where the game renders that
frame. Shapes draw **on top** of everything (no depth occlusion — usually wanted for debugging).

Design doc: [`docs/tech/debug-draw-overlay.md`](../../docs/tech/debug-draw-overlay.md).

## Usage

```cpp
#include "debug/DebugDraw.h"
using f4cf::debug::dd;
namespace colors = f4cf::debug::colors;

void MyMod::onFrameUpdate()
{
    // call EVERY FRAME you want it visible — stop calling and it's gone (immediate mode)
    dd().nodeAxes(f4vr::getPlayerNodes()->primaryWandNode);          // RGB orientation tripod
    dd().sphere(zoneCenter, zoneRadius, /*distanceScaled*/false, colors::Cyan);   // true radius
    dd().sphere(markerPos, 12.0f, /*distanceScaled*/true, colors::Red);           // constant on-screen size
    dd().cone(lightPos, beamDir, range, fovDegrees, colors::Yellow);
    dd().line(from, to, colors::Green);

    // one-shot event flash: fire once, stays visible for 2 seconds
    if (justHit) {
        dd().point(hitPos, 4.0f, /*distanceScaled*/true, colors::Red, 2.0f);
    }

    // HUD: watch table (auto-laid-out) + world-anchored label at a point
    dd().watch("grip angle", angleDeg);          // any std::format-able value
    dd().label("event", eventPos, colors::White);
    // (optional) re-home the watch table from the default head HUD to the offhand controller:
    // dd().watchAnchorNode(f4vr::getOffhandWandNode());
}
```

> **VR HUD note:** by default the watch table floats a short distance in front of the HMD
> (horizontally centred, a little below the look axis) so it reads in-headset. It is **world-anchored,
> not screen-space** — screen corners are cropped by the VR lens, so `text()` at a corner is usually
> not visible. Re-home the table with `watchAnchorNode(node)` / `watchAnchor(pos)` (e.g. the offhand
> controller, for a wrist display). `label()` is a **world-space billboard** welded to its point (it
> tilts with the world instead of staying screen-upright, so it doesn't appear to rotate as you move
> your head, and scales with distance for a depth cue). The watch table's first row is an auto
> `channels: …` line naming the channels drawn this frame (with `(off)` on any the config is muting).

**Zero cost until used:** no hook is installed and the per-frame driver is a single atomic read
until the first draw/watch call of the session. The Submit hook + D3D resources install lazily on
first use and stay inert (one atomic read per Submit) whenever there is nothing to draw.

## Contract

- **Immediate mode.** The command list auto-clears every frame; shape state lives in your update
  loop, not the overlay. No handles, no `remove()`.
- **`sec > 0` persists.** The shape is retained and re-emitted until the time passes — for one-shot
  events ("flash where the hit landed").
- **Game thread only.** Issue draws from `onFrameUpdate` (ModBase drives the frame boundary).
  Rendering happens on the render thread from a double-buffered snapshot.
- **Game-world coordinates.** Physics callers convert first via `DebugDraw::havokToGame(p)`.
- **Distance-scaled markers.** `point`/`sphere`/`box` take a `distanceScaled` flag: `false` draws the
  true world size; `true` treats the size (radius / half-extents) as an *apparent* size and scales it
  by distance to the viewer so the marker keeps a roughly constant on-screen size at any range (like
  the billboard `label()`) — for markers you always want to see. Only these point-located primitives
  have it; a `capsule` (two fixed endpoints) or `cone` (real reach/spread) has no single anchor to
  scale about, so they stay true-size.
- **Budgets degrade gracefully.** Lines beyond 64k vertices per frame are dropped (rate-limited log),
  never overflowing GPU buffers.

## Channels

Tag draws so independent systems can be toggled separately:

```cpp
{
    const auto scope = dd().channelScope("npc-detection");   // RAII; restores previous tag
    dd().cone(...);                                          // tagged "npc-detection"
}
dd().setChannelEnabled("npc-detection", false);              // runtime kill-switch
```

The channels tagged this frame show up automatically as the watch table's first row —
`channels: npc-detection physics(off)` — so you can see at a glance what is drawing and what a
config toggle is muting.

## `[Debug]` INI keys (all mods get these via `ConfigBase`, hot-reloadable)

| Key                          | Default | Meaning                                                                    |
| ---------------------------- | ------- | -------------------------------------------------------------------------- |
| `bDebugDrawEnabled`          | `true`  | Master switch (runtime `setEnabled()` / hotkey override it until changed). |
| `sDebugDrawDisabledChannels` | empty   | Comma-separated channel names whose draws are skipped.                     |
| `sDebugDrawToggleBinding`    | empty   | Controller binding to flip the overlay in-headset (`vrcf::parseInputBinding` grammar, e.g. `offhand longpress grip`). |
| `sDebugDrawHudPlacement`     | `center`| Where the default watch-table HUD sits: `center` / `center-left` / `center-right` / `center-top` / `center-top-left` / `center-top-right` (bottom row a little below centre, top row higher above it; side variants are edge-aligned that way). |

## Files

| File                    | What it is                                                                                   |
| ----------------------- | -------------------------------------------------------------------------------------------- |
| `DebugDraw.h/.cpp`      | Public API + game-thread producer: primitives → line segments, watch table, channels, timed-shape store, frame publish. |
| `DebugDrawRenderer.h/.cpp` | Internal render-thread consumer: `Submit` vtable hook, stereo-instancing shader, D3D state save/restore, 5×7 bitmap font. |

## Provenance

Generalized from ROCK's `DebugBodyOverlay` per the reference library's
`knowledge-base/debug_draw_overlay.md` (physics-body extraction stripped, public parametric API
added). The one version-specific address (VR camera globals, `0x6235AC8` for VR 1.2.72) lives in
[`f4vr/F4VROffsets.h`](../f4vr/F4VROffsets.h) (`vrRenderCameraGlobals`) — if overlay geometry reads
as garbage after a runtime change, that offset is the first suspect.
