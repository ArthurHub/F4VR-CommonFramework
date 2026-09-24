# `render/` — Overlay rendering (primitives, text, images)

Namespace: `f4cf::render`

Everything the framework draws **over** the VR view rather than into the scene graph: colored
world-space lines, filled triangles, textured quads and text, projected through the engine's own
per-eye camera so world coordinates land exactly where the game drew the world. One shared
`IVRCompositor::Submit` hook carries every layer, and the world's depth is captured so a layer can
ask to be hidden behind geometry.

> Part of the [F4VR Common Framework](../README.md) source tree.

Mods rarely call this directly — it is the engine under [`vrui/`](../vrui/README.md)'s panels,
[`imgui/`](../imgui/README.md)'s canvases, [`debug/`](../debug/README.md)'s overlay and the
activation-sphere icons. Read it when you are writing a layer of your own, or when you need to know
why something drew where it did.

## Files

| File | What it is |
| ----------------------- | -------------------------------------------------------------------- |
| [`PrimitiveDraw.h`](PrimitiveDraw.h) | The frame a producer fills: `lines` / `triangles` / `images` / `texts`, with `add*` helpers and the budgets. Plus `Color`, the `colors::` constants and the text placement/align/decoration enums. Header-only. |
| [`PrimitiveDrawRenderer.h`](PrimitiveDrawRenderer.h) / [`.cpp`](PrimitiveDrawRenderer.cpp) | One overlay **layer**: takes published frames on the game thread and replays them on the render thread. Owns the game → render handoff and the layer's draw order; goes dormant when it has nothing to draw. |
| [`SubmitHook.h`](SubmitHook.h) / [`.cpp`](SubmitHook.cpp) | The shared `IVRCompositor::Submit` vtable hook and the draw-callback table every layer registers on. Owns the RTV over the submitted eye texture, the per-eye camera constants, the pipeline save/restore and the painter order. |
| [`RenderUtils.h`](RenderUtils.h) / [`.cpp`](RenderUtils.cpp) | The game's D3D11 device/context, the engine's per-eye view-projection + posAdjust (`StereoCameraConstants`), and `ScopedPipelineState` — the RAII snapshot taken around the whole callback list. |
| [`TextFont.h`](TextFont.h) / [`.cpp`](TextFont.cpp) | The framework's font as a signed-distance-field atlas, and the measuring a producer lays text out with (`measureText`, `fitText`, `textDescent`, `measureTextRun`). |
| [`Texture.h`](Texture.h) / [`.cpp`](Texture.cpp) | Images loaded through the **engine's own** texture loader, shared per path, so a path resolves the way a NIF's texture path does — loose files and BA2 archives alike. |
| [`SceneDepthCapture.h`](SceneDepthCapture.h) / [`.cpp`](SceneDepthCapture.cpp) | Takes hold of the engine's scene depth for the frame being submitted, read-only, so a layer can be occluded by the world. |
| [`SceneDepthResample.h`](SceneDepthResample.h) / [`.cpp`](SceneDepthResample.cpp) | The copy made under an upscaler (DLSS/FSR), which draws the world into a sub-viewport of a full-size buffer and clears it before submit. |
| [`SceneDepthDiagnostics.h`](SceneDepthDiagnostics.h) / [`.cpp`](SceneDepthDiagnostics.cpp) | `sSceneDepthStrategy` / `bSceneDepthDiagnostics`, the watch rows, and the shadow A/B that elected the shipped policy. |
| [`fonts/`](fonts/) | The embedded Roboto Medium (and its license), used when a mod ships no font of its own. |

## Drawing a frame

A producer builds a `PrimitiveDraw` on the **game thread**, then publishes it to its own layer. The
layer is a long-lived object — draw callbacks can never be unregistered, so hold it as a static or a
member of something that lives for the process:

```cpp
#include "render/PrimitiveDrawRenderer.h"

render::PrimitiveDrawRenderer& myLayer()
{
    static render::PrimitiveDrawRenderer instance("MyMod", render::DRAW_ORDER_DEFAULT, /*occluded*/ false);
    return instance;
}

void MyMod::onFrameUpdate()
{
    render::PrimitiveDraw frame;
    frame.addLine(from, to, render::colors::Cyan);
    frame.addQuad(a, b, c, d, render::Color::rgba(15, 15, 15, 166));
    frame.addBillboardText("142", hitPos, render::colors::Yellow);
    frame.viewerPosition = headPosition;     // billboards turn toward this

    myLayer().ensureInstalled();             // idempotent; retries until D3D + OpenVR are up
    myLayer().publish(std::move(frame));     // an empty frame goes dormant
}
```

Depth testing is off within a layer, so the lists are painted in a fixed order and whatever draws
last wins the pixel: **lines, then all fills, then all images, then all text**. That is what puts a
panel's background under its image and its image under its labels — but it also means a layer's own
content is not meant to overlap itself. Consecutive content of one color and texture goes out in a
single draw call; each distinct texture costs a draw.

### Budgets

`MAX_LINE_VERTICES` (64k), `TEXT_VERTEX_CAPACITY` (128k), `MAX_FILL_TRIANGLES` (8k) and
`MAX_IMAGE_QUADS` (4k) per frame. The `add*` helpers return `false` when full, so a runaway producer
degrades instead of ballooning GPU buffers. The lists are public, for a producer with its own
ordering or accounting rules (the debug overlay sorts by color to batch runs).

## Text

Text is the framework's font — the embedded **Roboto Medium**, or a mod's own TTF/OTF at
`Data\Interface\<ModName>\<ModName>.ttf` (`CUSTOM_TEXT_FONT_PATH`) — drawn from a distance-field
atlas, so it stays sharp at any size, distance or angle. The file is read once, the first time any
text needs the font, so replacing it takes a restart. The same bytes feed the ImGui canvases, so a
`vrui::UITextPanel` and an `imgui::UIImGuiPanel` always share a typeface.

Building the atlas takes about a tenth of a second, and it happens on the first draw of anything
(lines and fills sample it too), stalling that frame. A mod that draws sets
`ModBase::Settings::preloadRendering`: the atlas is then built on a background thread from plugin
load (`preloadTextFont()`), and the shared pipeline and Submit hook host at game loaded
(`PrimitiveDrawRenderer::preload()`), both before the player is in the world.

Sizes are always the **height of a capital letter**; `measureText(text, textHeight)` gives a run's
width in the same unit and `textDescent` the room a row needs below its baseline. Four placements:

| `TextPlacement` | Where it lives | What `size` means |
| --------------- | -------------- | ----------------- |
| `Screen` | 2D on the headset view, in target pixels, duplicated into both eye halves | pixels of capital height in steps of `TEXT_PIXELS_PER_SIZE` (7), so size 2 draws 14px capitals |
| `WorldAnchored` | 2D, but at a world anchor's projected spot — screen-upright while tracking a world object | the same pixel steps |
| `Billboard` | a world-space quad welded to the anchor, turned to face the viewer | an apparent scale (the label sizes itself by viewer distance) |
| `Oriented` | a world-space quad in a plane the **caller** gives (`right`/`up`), at a fixed world size | the world height of a capital |

`TextAlign` places a row about its anchor, and `TextDecoration::Underline` goes out with the run's
glyphs, in the same color and the same draw, spanning the run's ink.

> **Screen-space is cropped in VR.** `Screen` text draws into the eye target's raw pixels and the
> corners are eaten by the lens, so corner HUDs are not visible in-headset. Anchor anything that has
> to be read — this is why the debug watch table is world-anchored.

## Images

`render::Texture` loads through the engine's loader, so a path resolves the way a NIF's texture path
does — loose files, BA2 archives, mod overrides — in any DDS format the game itself can use:

```cpp
auto icon = render::Texture::load("Data\\Textures\\MyMod\\icon.dds");   // GAME thread
frame.addImage(icon->view(), icon->isSRGB(), topLeft, topRight, bottomRight, bottomLeft, tint);
```

- **Shared per path.** `load()` returns the same `Texture` while any caller still holds it, on top of
  the engine's own per-path cache, so a path costs one loader call however many images show it. It is
  released with its last holder — a menu that closes gives its images back.
- **Partial paths resolve** like a vrui NIF path: as given, then under `Data\Textures\`, then under
  the mod's own `Data\Textures\<ModName>\`. Only loose files are probed, so a BA2-packed texture
  needs its full path.
- **`view()` is game-thread only** and is re-read every frame, because the engine may swap it while
  streaming mips. The `TextureView` a frame carries keeps the GPU texture alive across the thread
  boundary.
- **A missing path is not an error to the engine** — it hands back a 1x1 placeholder, which is taken
  to mean "missing": logged once, drawn as nothing.
- **sRGB is the cache's decision**, not the caller's: whoever loaded a path first settles it.
  `isSRGB()` reports what actually came back and the renderer re-encodes, since the overlay writes
  into a target that is not sRGB-encoded. (`Color::rgba(r, g, b, a)` takes the 0-255 bytes a color
  picker reports; a byte in is that byte out.)

## Layers and draw order

Each `PrimitiveDrawRenderer` is one independent layer with its own draw callback. Overlays are not
depth-tested against each other, so painter order is **declared** rather than inherited from
registration order — registration is lazy, which would otherwise make the layering depend on which
overlay the player happened to trigger first in a session:

| Constant | Value | Who uses it |
| -------- | ----- | ----------- |
| `DRAW_ORDER_HINTS` | 50 | world-anchored hints (the activation-sphere icons) — under the panels, so an open panel is never painted over by a hint behind it |
| `DRAW_ORDER_PANELS` | 100 | `vrui::UIPanel` and the ImGui canvases |
| `DRAW_ORDER_DEFAULT` | 500 | anything with no opinion |
| `DRAW_ORDER_DEBUG` | 900 | deliberately last, i.e. on top: diagnostics must never end up hidden behind a mod's UI |

Lower draws first, i.e. ends up underneath.

## Occlusion by the world

A layer constructed with `occluded = true` is hidden where scene geometry is in front of it; one
constructed `false` always draws on top (what diagnostics want — a debug shape behind a wall is
exactly the one you need to see). It degrades safely: with no depth captured, an occluded layer
simply draws on top as before.

The difficulty is that by the time a frame reaches `Submit` the engine has unbound its depth buffer,
so there is nothing left to test against. The capture takes it from the one place it is legitimately
bound — a call site inside the engine's graphics-state commit, hooked so the detour runs after the
state is committed and simply reads what is bound. No struct offsets, no guessing. What a callback
is handed is **read-only**: the buffer belongs to the engine and is still referenced by it.

Two things decide whether that depth lines up with the overlay:

- **Size.** Only a depth buffer the same size as the submitted texture is kept — D3D requires a depth
  view to match the render target it binds beside, so anything else is unusable by definition.
- **Viewport.** An upscaler driving the engine's dynamic resolution (DLSS, FSR) keeps the buffer at
  full size but draws the world into a smaller viewport of it, upscales only the color, and clears
  the buffer before the frame is submitted. The capture counts the viewports the frame's passes draw
  with, and when the elected one does not cover the buffer, the world's depth is copied during the
  frame and resampled to the submitted size ([`SceneDepthResample.h`](SceneDepthResample.h)).

Test with `SubmitFrame::sceneDepthComparison`, never a guess — it is the comparison the engine drew
the world with, and the opposite sense hides exactly what should be visible.

`[Debug]` keys: `sSceneDepthStrategy` (`auto` | `depthpass` | `direct` | `off`) and
`bSceneDepthDiagnostics`; both default to the right thing — see
[debug-config.md](../../docs/debug-config.md#scene-depth-occlusion). Design, measurements and the
failure modes they address:
[`docs/tech/scene-depth-occlusion.md`](../../docs/tech/scene-depth-occlusion.md).

## The render-thread contract

A draw callback runs on the **render thread**, inside the Submit hook, and the rules are not
negotiable:

- **Never touch game-thread state** — nodes, forms, config. Snapshot it game-side and hand the
  snapshot over; reading `node->world` here races the skeleton update.
- **Never call into OpenVR.** Two overlays both querying the runtime every frame double-drive
  vrclient; the engine's own matrices in `SubmitFrame::camera` are there so nobody has to.
- **Restore what you bind** beyond the shared save/restore. The host snapshots and restores the
  standard surface (`ScopedPipelineState`) around the whole callback list, deliberately wider than
  what any one callback binds — we draw in the middle of the game's pipeline and, once other mods
  chain onto the same hook, in the middle of theirs, and a field left clobbered presents as *their*
  bug.

## Zero cost until used, and never removed

- Nothing is installed until a layer actually has something to draw. `ensureInstalled()` compiles the
  shaders, builds the pipeline objects off the game's own device and patches the compositor vtable;
  each unavailable dependency (VR runtime, D3D device, compositor) just logs once and retries next
  frame.
- With no callback active the hook is **one relaxed atomic read** and a tail call to the original
  `Submit`. Publishing an empty frame is what puts a layer dormant.
- The vtable patch is **never removed** (process lifetime, like the input-suppression hook) and the
  original `Submit` is always chained. Two mods can be installing on the same frame and the slot swap
  is a read-modify-write, so it is serialized process-wide by a named mutex — losing that race would
  orphan whoever wrote first, silently and permanently. `ensureInstalled()` also drives the
  orphan-detection heartbeat, so an active consumer should keep calling it every frame.
- **VR only.** On flat Fallout 4 there is no compositor to hook; the overlay logs once and stays
  inert.

## Provenance

The Submit hook and the stereo projection were extracted from the debug-draw overlay (a port of
ROCK's `DebugBodyOverlay`, reference library `knowledge-base/debug_draw_overlay.md`) and hardened per
`knowledge-base/imgui_ui_overlay_architecture.md` §4-5, so the hygiene rules live here once instead
of in each subsystem. Its one version-specific game address is the VR render camera globals block
(`0x6235AC8` on VR 1.2.72), kept in [`f4vr/F4VROffsets.h`](../f4vr/F4VROffsets.h) as
`vrRenderCameraGlobals` — if overlay geometry reads as garbage after a runtime change, suspect it
first. The scene-depth capture adds two of its own, which live in
[`SceneDepthCapture.cpp`](SceneDepthCapture.cpp); a bad one there disables occlusion rather than
misplacing anything, since the addresses are checked for the instructions expected at them.

The scene-depth technique and both of its addresses were studied from PrismaUI's FO4VR port
(reference library, `framework-F4-Conversion`). That project's license permits study but not
redistribution of derived code, so this is an independent implementation; the addresses and byte
patterns are facts about `Fallout4VR.exe` 1.2.72 and were re-verified against it.
