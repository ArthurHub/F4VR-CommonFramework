# `imgui/` — Dear ImGui panels in the VR world

Namespace: `f4cf::imgui`

[Dear ImGui](https://github.com/ocornut/imgui) drawn as world-space quads over the VR view: sliders,
plots, trees, tables, text — the whole immediate-mode widget set — placed in the world instead of on
a 2D screen. Two entry points: a raw [`Canvas`](ImGuiCanvas.h) you place yourself, and
[`UIImGuiPanel`](UIImGuiPanel.h), a [`vrui`](../vrui/README.md) element that takes part in vrui
layout like any other.

> Part of the [F4VR Common Framework](../README.md) source tree.

It draws through [`render/`](../render/README.md)'s shared Submit hook, so it shares the overlay's
draw order, its occlusion and its threading rules. The font is the framework's own, so an ImGui panel
and a `vrui::UITextPanel` standing side by side read as one UI.

## Files

| File | What it is |
| ----------------------- | -------------------------------------------------------------------- |
| [`ImGuiCanvas.h`](ImGuiCanvas.h) / [`.cpp`](ImGuiCanvas.cpp) | `Canvas` — one ImGui window placed in the world by a provider you write. Content, placement, chrome (background, border, rounding, padding), sizing and occlusion. |
| [`UIImGuiPanel.h`](UIImGuiPanel.h) / [`.cpp`](UIImGuiPanel.cpp) | The vrui adapter: a `UIElement` whose rectangle a `Canvas` fills. Takes `vrui::UIPanelStyle` and the vrui sizing modes, in vrui units. |
| [`ImGuiSettings.h`](ImGuiSettings.h) / [`.cpp`](ImGuiSettings.cpp) | The two process-wide knobs: `setFontSizePixels` and `setSupersample`. |
| [`ImGuiFonts.h`](ImGuiFonts.h) / [`.cpp`](ImGuiFonts.cpp) | Loads the framework's text font into the ImGui atlas, from the same bytes the primitive renderer draws with. |
| [`ImGuiLayer.h`](ImGuiLayer.h) / [`.cpp`](ImGuiLayer.cpp) | Game-thread pump: one ImGui frame holding every visible canvas, cloned and published with each canvas's quad. |
| [`ImGuiRenderer.h`](ImGuiRenderer.h) / [`.cpp`](ImGuiRenderer.cpp) | Render-thread half: rasterizes the frame into the shared atlas texture, then composites each canvas's slice as a world-space quad. |

## A panel in a vrui layout

`UIImGuiPanel` is the common case: put it in a `UIContainer` next to buttons and it gets a slot in
the row or column, at the same scale, in the same plane, facing the same way.

```cpp
#include "imgui/UIImGuiPanel.h"

auto readout = std::make_shared<imgui::UIImGuiPanel>("BeamReadout", 8.0f);  // 8 vrui units wide
readout->setStyle(vrui::F4VR_PANEL_STYLE);
readout->setContent([this] {
    ImGui::Text("FOV: %.1f", beamFov());
    ImGui::ProgressBar(beamFade());
});
row->addElement(readout);
```

Sizing follows the same constructors as a [`vrui::UITextPanel`](../vrui/UITextPanel.h): no size fits
the content, a width grows to the height the content needs at that width, both stays as given. See
[panel sizing](../vrui/README.md#panel-sizing) — `UIImGuiPanel` is listed there beside the vrui
panels because it takes the same modes and the same `setMaxWidth`.

Its pixel resolution follows from `CANVAS_PIXELS_PER_UNIT` (48) and the element's own size, never its
containers' scale: content is laid out and rendered at scale 1 and the quad stretches it with the
rest of the layout, so text, widgets and chrome scale together like any other vrui element. The price
is density — in a container scaled 1.6 the panel has 1.6x fewer atlas pixels per world unit and reads
correspondingly softer, which `setSupersample` buys back at a memory cost.

Three things set it apart from its neighbours:

- It draws through the overlay path rather than the scene graph, so what hides it is the depth test
  (`setOccluded`), not the scene's own draw order.
- **It is not interactive.** vrui's finger-collision press handling does not apply — the content is
  pixels, not widgets. Put the buttons beside it, in vrui.
- It is **not** a `vrui::UIPanel`, whatever the name suggests: it takes the same `vrui::UIPanelStyle`
  and sizing modes, but ImGui draws its chrome and lays out its content.

It lives here rather than in `vrui/` because it is the adapter *between* the two: building it in vrui
would make every vrui consumer depend on Dear ImGui, and it has to disappear with the rest of the
layer when `F4CF_WITH_IMGUI_UI` is off. Neither subsystem depends on the other; only this file
depends on both, and a mod that never creates a panel never links it.

## A canvas placed by hand

Use `Canvas` directly when the quad is not a vrui element — welded to a node, a billboard, anywhere
you can produce a transform:

```cpp
#include "imgui/ImGuiCanvas.h"

_canvas = std::make_unique<imgui::Canvas>("BeamTuning", 512, 320);   // pixels in the shared atlas
_canvas->setContent([this] { ImGui::SliderFloat("FOV", &_fov, 10.0f, 120.0f); });
_canvas->setPlacement([this](imgui::CanvasPlacement& out) {
    out.transform  = _node->world;   // local +X is right, +Z is up, +Y the normal it faces along
    out.worldWidth = 16.0f;
    out.worldHeight = 10.0f;
    return isOpen();                 // false skips the canvas this frame
});
```

Both callbacks run on the **game thread**: the placement provider may read nodes and game state
freely (that is the whole point — `node->world` must never be read render-side), and the content
callback runs inside the framework's `NewFrame`/`Render` pair and inside a `Begin`/`End` for this
canvas, so it calls ImGui widget functions only — no `Begin`/`End` of its own, no D3D.

The canvas is a plain game-thread object: construct it, and the framework pumps it every frame;
destroy it and it is gone. `setVisible(false)` costs nothing — a hidden canvas is not packed, not
drawn, and does not run its content callback.

`setPixelSize` is cheap and safe at any time (the atlas is repacked every frame), so a canvas that
grows or shrinks in the world can hold its pixel density instead of being stretched.

## Sizes, pixels and legibility

Three separate things, deliberately:

| Knob | Scope | What it changes |
| ---- | ----- | --------------- |
| pixel size (`Canvas`) / vrui size (`UIImGuiPanel`) | per canvas | how many atlas pixels the canvas has, and how big it is in the world — together, how legible it is |
| `setFontSizePixels` (default 24, clamped 16..128) | process-wide | how big the text *looks*, without touching layout |
| `setSupersample` (default 1.5, clamped 1..4) | process-wide | how finely everything is rasterized — detail only; layout is always at 1x |

Both process-wide settings are read when the **first canvas draws**, since the font raster and the
atlas texture are built from them then — set them before that; later calls do nothing. A single
canvas that wants larger text can push `ImGui::SetWindowFontScale` from inside its content callback,
which resamples the raster rather than rebuilding it.

The atlas texture is `MAX_CANVAS_PIXEL_SIZE` (1024) times the supersample factor on each side, so
memory grows with the square — about 9MB at 1.5, 16MB at 2 — and past the headset's own resolution a
larger factor adds nothing visible. Nothing is allocated until the first canvas draws.

## Font

The canvases load the framework's text font — a mod's own TTF/OTF at
`Data\Interface\<ModName>\<ModName>.ttf` when it ships a usable one, the embedded Roboto Medium
otherwise — from the same bytes `render::TextFont` draws with, so a `UIImGuiPanel` and a
`UITextPanel` always share a typeface. ImGui's own embedded bitmap font is the last resort, logged as
a warning.

## How it is drawn

```
game thread (frame end)                         render thread (Submit hook)
──────────────────────────────────              ─────────────────────────────────
imgui::internal::onFrameEnd()                   draw callback (DRAW_ORDER_PANELS)
  ├─ one ImGui frame, every visible canvas        ├─ rasterize the frame into the atlas
  │    as its own window packed into the atlas    ├─ group quads by occluded / on-top
  ├─ measure each canvas's content                └─ composite each canvas's atlas slice
  ├─ resolve placements → world quads                  as a world quad, back to front
  └─ clone the draw data + publish  ────────────►
```

- **One ImGui frame and one atlas for every canvas.** Each canvas is an ImGui window packed into its
  own sub-rect and composited as one textured quad. The quads are sorted so each occlusion group is
  contiguous, so compositing is one draw for the occluded canvases and one for the rest — two at
  most, however many canvases there are.
- **Why rasterize flat first?** ImGui emits per-command *scissor rectangles* in 2D screen space —
  that is how scrolling regions, child windows and tables clip — and there is no scissor for an
  arbitrarily oriented 3D quad. So the draw data is rasterized into a texture and only then placed in
  the world.
- **The draw data is cloned** across the thread boundary (`ImDrawList::CloneOutput`): ImGui's own
  buffers are recycled by the next `NewFrame`, so a render thread still reading last frame's lists
  would be a use-after-free. A few KB of memcpy per frame for canvas-sized UI.
- **Canvases never write depth**, so they do not hide each other: overlapping ones are drawn back to
  front by distance to the viewer. Against the *world* they are occluded by default (`setOccluded`) —
  see [render/](../render/README.md#occlusion-by-the-world); where the engine's depth cannot be
  captured, every canvas draws on top regardless, so it is a preference rather than a guarantee.
- **Measuring costs a frame.** ImGui content can only be measured by drawing it, which happens after
  vrui has laid the frame out — so a canvas sized to its content is laid out at the size the content
  took up the frame *before*, and its first frame is measured without being shown. A dimension that
  follows the content is laid out in all the room it may grow to (the max width, or the whole atlas —
  about 21 vrui units), so wrapped text wraps there and a full-width item stretches the panel out to
  it.
- **Nothing is registered until the first canvas is constructed** (`registerFrameEndCallback`), so a
  mod that never creates one pays neither the frame cost nor the code size.

## Building without it

`F4CF_WITH_IMGUI_UI` (default `ON`) compiles the layer into the archive; the vcpkg manifest pulls
`imgui` with the `dx11-binding` feature. Configure with `-DF4CF_WITH_IMGUI_UI=OFF` to cut the compile
time or drop the port entirely — then `src/imgui/` is not built and its headers are not exposed, so
code using `f4cf::imgui` fails to **compile** rather than linking clean and mysteriously drawing
nothing. Everything else, the text font included, still builds: `stb_truetype` is vendored rather
than taken from the imgui port for exactly that reason.

## Provenance

Architecture per the reference library's `knowledge-base/imgui_ui_overlay_architecture.md` — the
atlas-then-quad composition, the cloned draw data, and the Submit-hook hygiene rules that now live in
[`render/`](../render/README.md).
