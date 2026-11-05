# `debug/` — In-World Debug Draw Overlay

Namespace: `f4cf::debug`

Immediate-mode debug drawing in the VR world: boxes, spheres, lines, arrows, axes, cones, capsules,
grids, polylines and meshes, HUD text, world labels, and a "name: value" watch table.

It fills a `render::PrimitiveDraw` and publishes it to a [`render/`](../render/README.md) layer at
`DRAW_ORDER_DEBUG`, **not** occluded: shapes draw on top of everything, the mod's UI and walls
included, which is what you want to see colliders and zones. Design:
[`docs/tech/debug-draw-overlay.md`](../../docs/tech/debug-draw-overlay.md).

> Part of the [F4VR Common Framework](../README.md) source tree.

## Usage

```cpp
#include "debug/DebugDraw.h"
using f4cf::debug::dd;
namespace colors = f4cf::debug::colors;

void MyMod::onFrameUpdate()
{
    // call every frame you want it shown: stop calling and it's gone
    dd().nodeAxes(f4vr::getVRPlayerNodes()->primaryWandNode);                   // RGB orientation tripod
    dd().sphere(zoneCenter, zoneRadius, /*distanceScaled*/ false, colors::Cyan); // its true radius
    dd().sphere(markerPos, 12.0f, /*distanceScaled*/ true, colors::Red);         // the same size on screen
    dd().cone(lightPos, beamDir, range, fovDegrees, colors::Yellow);
    dd().line(from, to, colors::Green);

    if (justHit) {
        dd().point(hitPos, 4.0f, /*distanceScaled*/ true, colors::Red, 2.0f); // stays 2 seconds
    }

    dd().watch("grip angle", angleDeg);      // any std::format-able value
    dd().watch("grip", "lost", colors::Red); // a color tints the value
    dd().label("event", eventPos, colors::White);
    // dd().watchAnchorNode(f4vr::getOffhandWandNode()); // the table on the offhand, as a wrist display
}
```

- **Immediate mode:** the list clears every frame, so the shapes' state lives in your update. No handles,
  no `remove()`.
- **`sec > 0` persists** a shape that long, for a one-shot event such as where a hit landed.
- **Game thread only,** from `onFrameUpdate`; `ModBase` drives the frame.
- **Game-world coordinates:** convert physics positions first with `DebugDraw::havokToGame(p)`.
- **`distanceScaled`** (`point`, `sphere`, `box`): `false` is the true world size, `true` an apparent size
  that stays the same on screen at any range. A `capsule` or `cone` has no single anchor and stays true.
- **Budget:** lines past 64k vertices a frame are dropped, with a rate-limited log.
- **Zero cost until used:** no hook, and one atomic read a frame, until the first draw of the session.

### The watch table and labels

- **Where:** by default a short way in front of the HMD, a little below the look axis. It is
  world-anchored, as screen corners are cropped by the lens, so `text()` in a corner is rarely seen.
  `watchAnchorNode(node)` or `watchAnchor(pos)` moves it.
- **Layout:** a dim name column right-aligned against the values, grouped under an underlined header per
  [channel](#channels), untagged rows first. A value changing width never shifts the table.
- **`label()`** is a world billboard at its point: it tilts with the world and scales with distance, so
  it doesn't seem to turn with the head.

## Channels

Tag draws so systems can be toggled apart:

```cpp
{
    const auto scope = dd().channelScope("npc-detection"); // RAII; restores the previous tag
    dd().cone(...);
}
dd().setChannelEnabled("npc-detection", false);            // runtime switch
```

Each channel drawn this frame gets a header in the table with its rows under it, so row names need no
prefix. A channel with only shapes gets a bare header, and a muted one a grey `physics (off)`, so you see
what draws and what a config switch mutes. The framework's scene-depth readout is the `SCENE-DEPTH`
channel, off unless asked for ([`bSceneDepthDiagnostics`](../../docs/debug-config.md#scene-depth-occlusion)).

## `[Debug]` config keys

Every mod has them, reloaded live. The defaults are the code's, for a key that is missing.

| Key | Default | What it is |
| --- | ------- | ---------- |
| `bDebugDrawEnabled` | `true` | The master switch. `setEnabled()` and the toggle binding win until it changes. |
| `aDebugDrawDisabledChannels` | `[]` | Channels whose draws are skipped, as `["physics"]`. |
| `kDebugDrawToggleBinding` | none | A [binding](../../docs/input-binding.md#the-binding) that flips the overlay in the headset. |
| `sDebugDrawHudPlacement` | `center` | The watch table: `center`, `center-left`, `center-right`, `center-top`, `center-top-left` or `center-top-right`. |

## Files

`DebugDraw.h/.cpp`: the API and the game-thread producer (primitives into lines, the watch table,
channels, timed shapes, the publish). The drawing itself is [`render/`](../render/README.md)'s.

Generalized from ROCK's `DebugBodyOverlay` per the reference library's
`knowledge-base/debug_draw_overlay.md`. Its one address, the VR camera globals, is
[`render/`](../render/README.md#addresses)'s.
