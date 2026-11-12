# `render/` — Overlay rendering (primitives, text, images)

Namespace: `f4cf::render`

What the framework draws **over** the VR view rather than into the scene graph: world-space lines,
filled triangles, textured quads and text, projected through the engine's own per-eye camera so they
land where the game drew the world. One host carries every layer: the game's player-space pass draws
them under its menus, and a shared `IVRCompositor::Submit` hook draws the diagnostics on top. The
world's depth is captured, so a layer can be hidden behind it.

Mods rarely call it directly: it is the engine under [`vrui/`](../vrui/README.md)'s panels,
[`imgui/`](../imgui/README.md)'s canvases, [`debug/`](../debug/README.md)'s overlay and the
activation-sphere icons. Read it to write a layer of your own, or to know why something drew where it did.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | What it is |
| ---- | ---------- |
| [`Color.h`](Color.h) | `Color`: 0..1 floats, from 0..255 bytes with `rgba`, back with `bytes`, and `colors::`. Plain std, so the config parsers use it. |
| [`PrimitiveDraw.h`](PrimitiveDraw.h) | The frame a producer fills: `lines`, `triangles`, `images`, `texts`, the `add*` helpers, the budgets, the text enums. |
| [`PrimitiveDrawRenderer.h`](PrimitiveDrawRenderer.h) | One **layer**: takes frames on the game thread, replays them on the render thread, in its draw order. Dormant with nothing to draw. |
| [`SubmitHook.h`](SubmitHook.h) | The shared Submit hook, the player-space pass hook ([under the game's menus](#under-the-games-menus)), and the draw-callback table. The eye texture's RTV, the camera constants, the pipeline save and restore, the painter order. |
| [`RenderUtils.h`](RenderUtils.h) | The game's D3D11 device and context, the per-eye `StereoCameraConstants`, and `ScopedPipelineState`. |
| [`TextFont.h`](TextFont.h) | The font as a distance-field atlas, and text measuring (`measureText`, `fitText`, `textDescent`, `measureTextRun`). |
| [`Texture.h`](Texture.h) | Images through the engine's own loader, shared per path (loose and BA2), and images from pixels. |
| [`CallSiteHook.h`](CallSiteHook.h) | Whether a call site the hooks patch still calls its function, or another plugin's hook they chain onto, and whose. |
| [`SceneDepthCapture.h`](SceneDepthCapture.h) | Takes the engine's scene depth for the frame, read-only, for occlusion. |
| [`SceneDepthResample.h`](SceneDepthResample.h) | The copy made under an upscaler (DLSS, FSR). |
| [`SceneDepthShared.h`](SceneDepthShared.h) | The one copy every framework mod in the process shares, and its frame count. |
| [`SceneDepthDiagnostics.h`](SceneDepthDiagnostics.h) | `sSceneDepthStrategy`, `bSceneDepthDiagnostics`, the watch rows, the A/B that chose the policy. |
| [`fonts/`](fonts/) | Roboto 2.137: Medium (the default), and the ImGui canvases' Black (bold, as Bold is too close to Medium) and Medium Italic. |

## Drawing a frame

A producer fills a `PrimitiveDraw` on the **game thread** and publishes it to its layer. Draw callbacks
are never unregistered, so a layer lives for the process, as a static:

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
    frame.viewerPosition = headPosition; // billboards turn toward this

    myLayer().ensureInstalled();         // idempotent; retries until D3D and OpenVR are up
    myLayer().publish(std::move(frame)); // an empty frame goes dormant
}
```

- **Paint order:** a layer has no depth test within it, so it paints **lines, then fills, then images,
  then text**, the last winning. That puts a panel's background under its image under its labels; a
  layer's content isn't meant to overlap itself otherwise.
- **Draw calls:** color is per vertex, so consecutive content of one texture is one draw whatever its
  colors. Each texture costs a draw.
- **Gradients:** a fill takes a color per corner, so a fade is one shape:
  `frame.addQuad(a, b, c, d, color, color, clear, clear)`.
- **Budgets per frame:** `MAX_LINE_VERTICES` (64k), `TEXT_VERTEX_CAPACITY` (128k), `MAX_FILL_TRIANGLES`
  (8k), `MAX_IMAGE_QUADS` (4k). The `add*` helpers return `false` when full, so a runaway producer
  degrades. The lists are public, for a producer with rules of its own.

## Text

The framework's font, Roboto Medium or the mod's `Data\Interface\<ModName>\<ModName>.ttf`
(`CUSTOM_TEXT_FONT_PATH`), drawn from a distance-field atlas, sharp at any size, distance or angle.

- **Read once,** the first time text needs it, so replacing it takes a restart. The ImGui canvases use
  the same bytes, and its bold and italic faces (`<ModName>-Bold.ttf`, `<ModName>-Italic.ttf`).
- **Building the atlas stalls a frame** (~0.1 s), on the first draw of anything, as lines and fills
  sample it too. A mod that draws sets `ModBase::Settings::preloadRendering`: the atlas is then built on
  a background thread from plugin load, and the pipeline at game loaded.
- **Sizes are a capital letter's height.** `measureText(text, textHeight)` gives a width in that unit,
  `textDescent` the room below the baseline.

| `TextPlacement` | Where | What `size` means |
| --------------- | ----- | ----------------- |
| `Screen` | 2D on the headset view, in both eye halves | capital height in steps of `TEXT_PIXELS_PER_SIZE` (7 px) |
| `WorldAnchored` | 2D, at a world point's projected spot, screen-upright | the same steps |
| `Billboard` | a world quad at the anchor, facing the viewer | an apparent scale, sized by distance |
| `Oriented` | a world quad in a plane the caller gives (`right`, `up`) | a capital's world height |

`TextAlign` places a row about its anchor. `TextDecoration::Underline` spans the run's ink, in its
color and draw.

> **Screen text is cropped in VR:** the lens eats the corners, so a corner HUD isn't visible in the
> headset. Anchor what has to be read, as the debug watch table is.

## Images

`render::Texture` loads through the engine, so a path resolves like a NIF's texture path (loose files,
BA2s, overrides), in any DDS format the game uses:

```cpp
auto icon = render::Texture::load("Data\\Textures\\MyMod\\icon.dds"); // game thread
frame.addImage(icon->view(), icon->isSRGB(), topLeft, topRight, bottomRight, bottomLeft, tint);
```

- **Shared per path:** `load()` returns the same `Texture` while anyone holds it, so a path is one
  loader call. It goes with its last holder.
- **Partial paths:** as given, under `Data\Textures\`, then under `Data\Textures\<ModName>\`. Only loose
  files are probed, so a BA2 texture needs its full path.
- **`view()` is game thread only,** read each frame, as the engine may swap it while streaming mips. The
  `TextureView` a frame carries keeps the GPU texture alive across threads.
- **Missing:** the engine returns a 1x1 placeholder, taken as missing: logged once, drawn as nothing.
- **Bad headers** (zero or huge sizes, truncated, block-compressed sizes not a multiple of 4) are checked
  before loading, from at most 148 bytes, and skipped with a log. Archive textures aren't checked.
- **`error()`** says why a texture has no view, for a mod to show its player.
- **sRGB is the cache's:** whoever loaded a path first settles it. `isSRGB()` says what came back, and
  the renderer encodes it back, as the overlay's target isn't sRGB. A color byte in is that byte out.

An image no file holds, one a mod makes itself, is made from pixels:

```cpp
// rows from the top, 4 bytes a pixel: red, green, blue, alpha
auto picture = render::Texture::fromPixels("MyMod picture", width, height, rgba);
```

- **Copied, and drawn as given:** alpha isn't premultiplied. The name is for the log.
- **Made on the GPU by the first `view()`,** so `fromPixels` runs on any thread, before the renderer is
  up.
- **Its own,** one mip, alive with its holders; dropping it while shown is safe.
- **Too few pixels for the size** is logged, and it draws nothing.

## Layers and draw order

Each `PrimitiveDrawRenderer` is a layer with its own callback. Layers aren't depth tested against each
other, so the order is **declared**: registration is lazy, and its order would depend on what the
player happened to trigger first. Lower draws first, underneath.

| Constant | Value | For |
| -------- | ----- | --- |
| `DRAW_ORDER_HINTS` | 50 | world hints (activation-sphere icons), under the panels |
| `DRAW_ORDER_PANELS` | 100 | `vrui::UIPanel` and the ImGui canvases |
| `DRAW_ORDER_POINTERS` | 150 | the ImGui pointer's ray and mark, over the panels |
| `DRAW_ORDER_DEFAULT` | 500 | no opinion |
| `DRAW_ORDER_DEBUG` | 900 | on top of everything, the game's menus too |

## Under the game's menus

FO4VR draws its menus as quads in the world: the pause menu, message boxes, dialogue, notifications,
lockpicking, the workshop, other mods' menus. The player-space pass draws them after the world, with
the Pip-Boy and the wands, testing the world's depth but writing none. A layer drawn at Submit, over
the finished frame, would cover them all.

So the layers below `DRAW_ORDER_DEBUG` draw inside that pass, from a hook of the call that draws its
first scene (`Main::DrawWorld_And_UI` → `0x14284e370`, called at `0x14284e68c`), into its target, with
the depth captured this frame. The game draws its menus over them. Debug draw stays at Submit.

- **Once a frame,** into a target of the eye texture's size. Another target declines, logged once, and
  Submit draws the layers that frame.
- **A loading screen** doesn't run the pass, so the layers aren't drawn: it is drawn from its own thread
  and submitted with no world.
- **Every framework mod draws in the pass,** each from its own copy of the framework hooking the same
  call. A call another plugin hooked first is chained onto, and the log names it. The mod installed
  last draws first, so where two mods' layers overlap, the first installed is on top.
- **No hook, all at Submit:** where the call isn't the expected one (another build) or the trampoline
  has no room, every layer draws at Submit, over the menus, and the log says why.
  `sOverlayDrawPass = "submit"` in `[Debug]` does the same live
  ([debug settings](../../docs/debug-config.md#soverlaydrawpass)).
- **Over the layers anyway:** the Pip-Boy and the wand models, drawn after them by the pass.

## Occlusion by the world

A layer made `occluded = true` is hidden behind scene geometry; `false` always draws on top, as
diagnostics want. With no depth captured, an occluded layer draws on top.

By Submit the engine has unbound its depth buffer. The capture takes it where it is bound, from a call
site in the engine's graphics-state commit, hooked to run after the commit and read what is bound: no
struct offsets. The buffer is the engine's, so it is **read-only**.

- **Size:** only a depth buffer the size of the submitted texture is kept, as D3D requires.
- **Viewport:** an upscaler (DLSS, FSR) draws the world into a smaller viewport of a full-size buffer,
  upscales only the color, and clears the depth before Submit. The capture counts the viewports the
  passes use, and when the chosen one doesn't cover the buffer, the depth is copied during the frame and
  resampled ([`SceneDepthResample.h`](SceneDepthResample.h)).
- **Only while read:** under an upscaler the copy is a full-screen draw and an eye-sized buffer (80 MB at
  6144x3264), so it runs only while an active layer is occluded. Activation icons, pointers or debug
  lines alone capture nothing. A callback of a mod's own reads it while active, unless it says not
  (`setDrawCallbackActive`'s `readsSceneDepth`).
- **One copy for every framework mod:** it lives in a struct the process's mods find by name
  ([`SceneDepthShared.h`](SceneDepthShared.h)); the first to reach the copy pass makes it, the rest use it.
  Each mod's capture, viewport choice and `[Debug]` keys stay its own. The outermost Submit hook ends the
  frame, and a copy unwritten for 8 frames is freed. A mod built before the struct copies for itself.
  The struct is a frozen ABI (`SharedSceneDepthV1`): fields are only appended.
- **Compare with `SubmitFrame::sceneDepthComparison`,** never a guess: the opposite sense hides exactly
  what should show.

The `[Debug]` switches: [debug settings](../../docs/debug-config.md#scene-depth-occlusion). The design
and measurements: [`docs/tech/scene-depth-occlusion.md`](../../docs/tech/scene-depth-occlusion.md).

## The render-thread contract

A draw callback runs inside the Submit hook, on whatever thread the game submits from. **During play
that is the game thread,** about a millisecond after the mods' frame updates. **While a loading screen
is up it is another thread,** the one drawing it. The hook logs each thread it's called on once. The
game → render handoff keeps a layer correct on both.

- **Never touch game state:** nodes, forms, config. Snapshot it game side and hand the snapshot over;
  on another thread, reading `node->world` would race the skeleton update.
- **Never call OpenVR.** Two overlays querying it every frame double-drive vrclient; the engine's
  matrices are in `SubmitFrame::camera`.
- **Restore what you bind** beyond `ScopedPipelineState`, which the host saves and restores around all
  the callbacks. We draw in the middle of the game's pipeline, and of other mods' on the same hook, and
  a state left changed shows up as *their* bug.

## Timing and capture tools

The host times its draw on the CPU (`render::drawToSubmittedTexture`, each callback under it by name)
and on the GPU (the same names in the perf report's `gpu` tree, with the shared `setup`;
[perf](../perf/README.md#gpu-time)). With RenderDoc or PIX attached, the draw is an event named
`<mod> overlays` with one per callback. Neither costs anything unused: GPU queries run only while perf
records or Tracy is connected, events only while a capture tool is attached.

## Zero cost until used, never removed

- **Nothing is installed until a layer has something to draw.** `ensureInstalled()` compiles the
  shaders, builds the pipeline and patches the compositor vtable; a missing dependency logs once and
  retries next frame.
- **With no callback active,** the hook is one atomic read and a tail call to `Submit`.
- **The patch is never removed,** and the original `Submit` is always chained. Two mods may install on
  the same frame, so the slot swap is serialized by a process-wide named mutex: losing that race would
  orphan the first writer for good. `ensureInstalled()` also drives the orphan check, so an active layer
  keeps calling it.
- **VR only:** flat Fallout 4 has no compositor; the overlay logs once and stays inert.

## Addresses

- **The VR render camera globals** (`0x6235AC8` on 1.2.72, `vrRenderCameraGlobals` in
  [`f4vr/F4VROffsets.h`](../f4vr/F4VROffsets.h)): the one address of the overlay itself. Overlay
  geometry that reads as garbage after a runtime change, suspect it first.
- **The scene-depth capture's two,** in [`SceneDepthCapture.cpp`](SceneDepthCapture.cpp), checked for
  the instructions expected there, so a bad one disables occlusion rather than misplacing anything.

The Submit hook and the stereo projection come from the debug-draw overlay (a port of ROCK's
`DebugBodyOverlay`, reference library `knowledge-base/debug_draw_overlay.md`), hardened per
`knowledge-base/imgui_ui_overlay_architecture.md` §4-5. The scene-depth technique was studied from
PrismaUI's FO4VR port, whose license permits study but not redistribution, so this is an independent
implementation; the addresses are facts about `Fallout4VR.exe` 1.2.72, verified against it.
