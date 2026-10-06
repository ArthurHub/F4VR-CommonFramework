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
| [`ImGuiCanvas.h`](ImGuiCanvas.h) / [`.cpp`](ImGuiCanvas.cpp) | `Canvas` — one ImGui window placed in the world by a provider you write. Content, placement, chrome (background, border, rounding, padding), sizing and occlusion, and whether it is dedicated. |
| [`UIImGuiPanel.h`](UIImGuiPanel.h) / [`.cpp`](UIImGuiPanel.cpp) | The vrui adapter: a `UIElement` whose rectangle a `Canvas` fills. Takes `vrui::UIPanelStyle` and the vrui sizing modes, in vrui units. |
| [`ImGuiPointer.h`](ImGuiPointer.h) / [`.cpp`](ImGuiPointer.cpp) | `Pointer`, reached as `imgui::pointer()` — the one pointer of the interactive canvases: a wand's ray as ImGui's mouse, its trigger as the mouse button, and that wand hidden from the game. Its state, which hands point, and its `PointerStyle`: where the ray is on the hand, and how the ray and its mark are drawn. |
| [`ImGuiPointerLogic.h`](ImGuiPointerLogic.h) | The pointer's logic apart from the game, so it is unit tested: where a ray meets a quad and its plane, whether a hand operates the UI and presses it, which hand owns the pointer, and how much a thumbstick scrolls. |
| [`ImGuiSettings.h`](ImGuiSettings.h) / [`.cpp`](ImGuiSettings.cpp) | The two process-wide knobs: `setFontSizePixels` and `setSupersample`. |
| [`ImGuiFonts.h`](ImGuiFonts.h) / [`.cpp`](ImGuiFonts.cpp) | Loads the framework's text font into the ImGui atlas, from the same bytes the primitive renderer draws with. |
| [`ImGuiLayer.h`](ImGuiLayer.h) / [`.cpp`](ImGuiLayer.cpp) | Game-thread pump: one ImGui frame holding every visible canvas, or the dedicated canvas alone, cloned and published with each canvas's quad. |
| [`ImGuiRenderer.h`](ImGuiRenderer.h) / [`.cpp`](ImGuiRenderer.cpp) | Render-thread half: rasterizes the frame into the shared atlas texture, or into the dedicated canvas's own, then composites each canvas's slice as a world-space quad. |

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
- **It is not pressed by a finger.** vrui's finger-collision press handling does not apply — the
  content is pixels, not widgets. It is operated from a distance instead, with a wand's ray: see
  [Interactive canvases](#interactive-canvases). Buttons to press by touch go beside it, in vrui.
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

The canvas's padding and border are the canvas's alone. What the content opens, a combo's list, a
popup, a dialog or a bordered child window, has ImGui's own window padding and border, as in any
ImGui program.

The canvas is a plain game-thread object: construct it, and the framework pumps it every frame;
destroy it and it is gone. `setVisible(false)` costs nothing — a hidden canvas is not packed, not
drawn, and does not run its content callback.

`setPixelSize` is cheap and safe at any time (the atlas is repacked every frame), so a canvas that
grows or shrinks in the world can hold its pixel density instead of being stretched.

## Interactive canvases

A canvas only shows its content until it is made interactive. Then a wand's ray is its pointer: where
the ray meets the canvas is ImGui's mouse position, and the wand's trigger is the left mouse button.
ImGui does the rest, so the content is written as for a screen.

```cpp
panel->setInteractive(true);            // Canvas::setInteractive for a canvas placed by hand
panel->setContent([this] {
    if (ImGui::Button("Reset")) {
        reset();
    }
    ImGui::SliderFloat("FOV", &_fov, 10.0f, 120.0f);
});
```

- **Which hand points.** ImGui has one pointer, so one hand owns it at a time. A hand whose ray is
  alone on an interactive canvas owns it. With both rays on a canvas it is the hand that pressed its
  trigger on a canvas last, and the primary hand before either has. The owner keeps the pointer while
  it holds its trigger down, so a press of the other hand does not take a drag over.
  `imgui::pointer().setHands` limits pointing to one hand.
- **Where the ray is.** It is placed from the wand's UI node (`primaryUIAttachNode`,
  `secondaryUIOffsetNode`), whose +Y is the way the hand aims a weapon: the node a weapon hangs on is
  turned the same, and the wand's own node points 59 degrees above that. By default the ray starts a
  little in front of the wand and points 18 degrees above that aim and 5 degrees inward, which is
  where the hand points. The style's `rayOffset` is that placement: the ray starts at the offset's
  position and runs along its +Y. It is given for the right hand and mirrored for the left.
- **What it meets.** The ray meets a canvas only from its front, and the nearest interactive canvas
  takes it. It passes through the canvases that are not interactive, and through the world.
- **What the game gets.** A hand operates the UI while its ray is on an interactive canvas, and its
  whole wand is then hidden from the game and from other mods: every button and axis
  (`VRControllersSuppress`, under the owner `ImGuiPointer`). A pull of the trigger on a canvas fires
  no weapon, and the game has the wand back when the ray leaves. This goes for each hand by its own
  ray, whether or not it owns the pointer. The mod's own reads through `VRControllers` still see the
  wand.
- **A press is held.** A press that began on a canvas is the UI's until the trigger is released. It
  stays on that canvas while the ray slides off it: the pointer follows the ray on the canvas's
  plane, up to a canvas's size past its edges, so a slider is dragged to its end and a press is
  given up by releasing beside the canvas. The wand stays hidden from the game until the release,
  also when the canvas is hidden in the middle of the press, so the game never gets a trigger that
  is already down. The press ends when the trigger is back near its rest, a little after the end of
  its click.
- **A pull from outside is the game's.** A trigger that is already down when the ray comes to a
  canvas presses nothing, and its wand is not hidden. The hand has no pointer until the trigger is
  released.
- **The thumbstick scrolls.** The thumbstick of the hand that owns the pointer is ImGui's mouse
  wheel: pushed up or down it scrolls what the pointer is on, a list, a child window or an open
  combo, as the wheel does on a screen. The speed follows how far it is pushed, from nothing near
  its center up to the style's `scrollSpeed` at a full push, in lines of text a second. The player
  does not move or turn, since the wand is hidden from the game.
- **What is drawn.** While the pointer is on a canvas: the owner's ray, and a mark on the canvas
  where the ray meets it. The ray is no longer than a set length, so it ends before a canvas that
  is further away and at the mark of a nearer one, and it fades in and out at its two ends. The
  mark is a disc with a border. Its size is set in the world, so it keeps its size on the canvas as
  the player comes nearer or steps back. While the other hand's ray is on a canvas too, that ray is
  drawn at 0.3 of the opacity and with no mark, so the player sees that the hand points and that the
  pointer is not its own. All of it is drawn over the panels (`DRAW_ORDER_POINTERS`) and is not
  hidden by the world. Nothing is drawn while the pointer is on no canvas, so a ray that is drawn
  says its wand is the UI's.
- **What the pointer does.** `imgui::pointer().state()` has the canvas the pointer is on, where on it
  in the canvas's pixels, the hand, whether it presses the canvas, the ray in the world, and whether
  ImGui uses the pointer (`io.WantCaptureMouse`). `UIImGuiPanel::isPointedAt()` says whether it is on
  that panel. The state is updated when the ImGui frame is built, after the mod's frame update: a
  content callback reads this frame's, and `onFrameUpdate` the one before.
- **What it costs.** A canvas that is not interactive costs nothing more. While no interactive canvas
  is shown and no press is held, nothing is read from the game for the pointer.

### How the pointer looks

[`PointerStyle`](ImGuiPointer.h) holds where the ray is on the hand, the whole look and the speed of
scrolling, and its own defaults are the default pointer, so the values are in one place. Lengths are
in world units.

| Field | Default | What it is |
| ----- | ------- | ---------- |
| `rayOffset` | -2,3,-1; -18,0,-5 | where the ray is, from the wand's UI node: position, then degrees around x, y and z |
| `drawn` | `true` | `false` draws nothing, for a mod that draws the pointer itself from `state()` |
| `rayColor` | 190,255,190,200 | the ray's color, with its opacity as the alpha |
| `rayWidth` | 0.2 | the ray's width |
| `rayMaxLength` | 25 | the longest the ray is drawn: it ends there, or at the mark when the canvas is nearer |
| `rayFade` | 1 | the length the ray fades in over at its start and out over at its end; 0 for no fade |
| `markColor` | 215,255,215,180 | the mark's color, with its opacity as the alpha |
| `markSize` | 0.45 | the mark's radius, its border included; the same at any distance |
| `markBorderColor` | 215,255,215,100 | the color of the border around the mark |
| `markBorderWidth` | 0.08 | the border's width, the outer part of the mark's radius; 0 for no border |
| `scrollSpeed` | 30 | how fast the thumbstick scrolls, in lines of text a second at a full push; 0 for no scrolling |

A mod changes a part by starting from the current style:

```cpp
auto style = imgui::pointer().style();
style.rayColor = render::Color::rgba(10, 250, 120, 200);
imgui::pointer().setStyle(style);
```

A mod that wants the player to set these reads the fields from its own INI and calls `setStyle`
with them: `ConfigBase::getColorValue` reads a color written as `r,g,b,a`, and `getTransformValue`
the offset. The framework has no INI keys for it.

## A dedicated panel

The canvases share one atlas of 1024 by 1024 layout pixels, which is also ImGui's display. That suits
several small panels. A mod whose UI is one large panel makes that panel dedicated:

```cpp
panel->setDedicated(true);              // Canvas::setDedicated for a canvas placed by hand
```

- **A texture of its own.** The panel is rasterized into a texture of the panel's size, not into the
  atlas. It keeps 48 pixels per vrui unit up to `MAX_DEDICATED_CANVAS_PIXEL_SIZE` (4096) a side,
  about 85 units, where a panel in the atlas is scaled down past 1024 pixels, about 21 units.
- **It is ImGui's display.** ImGui keeps what the content opens inside its display, and while a
  dedicated panel is shown the display is that panel. A combo near the panel's bottom edge opens
  upward, and a dialog (`ImGui::BeginPopupModal`) is centered on the panel. The content calls
  `ImGui::BeginCombo`, `ImGui::BeginPopup` and `ImGui::BeginPopupModal` as in any ImGui program. In
  the atlas these are placed against the atlas, and the part beside the canvas's own rectangle is
  not shown.
- **It is the only ImGui panel.** ImGui has one display, so while a dedicated panel is shown the
  mod's other canvases are not drawn and their content does not run. They are drawn again when it is
  hidden. Of two dedicated panels that are shown, the one created first is drawn. vrui buttons and
  vrui text panels are not ImGui and are not affected, and neither are other mods: each has its own
  copy of the framework.
- **What it costs.** The texture is the panel's pixels times the supersample factor on each side: a
  panel of 38 by 30 units at 1.5 is 2736 by 2160 pixels, about 24MB. It is created when the panel is
  first drawn, and again when the panel's size changes, so a panel sized to its content gets a new
  one whenever its content changes size. The atlas is not created for a mod that draws only a
  dedicated panel.

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
larger factor adds nothing visible. Nothing is allocated until the first canvas draws. A
[dedicated panel](#a-dedicated-panel) has a texture of its own size in place of the atlas, at the
same factor.

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
  ├─ resolve placements → world quads             ├─ rasterize the frame into the atlas
  ├─ the pointer: a wand's ray on the quads       ├─ group quads by occluded / on-top
  │    of the interactive canvases → ImGui's      └─ composite each canvas's atlas slice
  │    mouse, its ray and mark → their own             as a world quad, back to front
  │    layer over the panels, and the wand
  │    hidden from the game
  ├─ one ImGui frame, every visible canvas
  │    as its own window packed into the atlas
  ├─ measure each canvas's content
  └─ clone the draw data + publish  ────────────►
```

- **One ImGui frame and one atlas for every canvas.** Each canvas is an ImGui window packed into its
  own sub-rect and composited as one textured quad. The quads are sorted so each occlusion group is
  contiguous, so compositing is one draw for the occluded canvases and one for the rest — two at
  most, however many canvases there are.
- **A dedicated canvas is a frame by itself.** While one is shown the frame holds that canvas alone,
  ImGui's display is the canvas, and the frame is rasterized into the canvas's own texture in place
  of the atlas. The rest of the path is the same.
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
