# `imgui/` — Dear ImGui panels in the VR world

Namespace: `f4cf::imgui`

[Dear ImGui](https://github.com/ocornut/imgui) on world-space quads: sliders, plots, trees, tables and
text, placed in the world. Two ways in: [`UIImGuiPanel`](UIImGuiPanel.h), a [`vrui`](../vrui/README.md)
element laid out like any other, and a raw [`Canvas`](ImGuiCanvas.h) you place yourself.

It draws through [`render/`](../render/README.md), so it shares its draw order, occlusion and threading
rules, and is drawn under the game's menus. The font is the framework's, so an ImGui panel and a
`vrui::UITextPanel` side by side read as one UI.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | What it is |
| ---- | ---------- |
| [`UIImGuiPanel.h`](UIImGuiPanel.h) | The vrui element a `Canvas` fills. Takes `vrui::UIPanelStyle` and the vrui sizing modes, in vrui units. |
| [`ImGuiCanvas.h`](ImGuiCanvas.h) | `Canvas`: one ImGui window, placed by a provider you write. Content, placement, curve, chrome, sizing, occlusion, dedicated. |
| [`ImGuiPointer.h`](ImGuiPointer.h) | `imgui::pointer()`: a wand's ray as ImGui's mouse, its state and its `PointerStyle`. |
| [`ImGuiPointerLogic.h`](ImGuiPointerLogic.h) | The pointer's logic, unit tested: ray and quad, the curved quad, which hand owns it, scrolling. |
| [`ImGuiKeyboard.h`](ImGuiKeyboard.h) | SteamVR's keyboard while a text field is edited, and the game window's key messages to ImGui ([VR keyboard](../../docs/tech/vr-keyboard.md)). |
| [`ImGuiKeyboardLogic.h`](ImGuiKeyboardLogic.h) | The keyboard's logic, unit tested: the one edit between a field's text and the keyboard's, on whole UTF-8 characters. |
| [`ImGuiImage.h`](ImGuiImage.h) | `imgui::image` and `imgui::addImage`: a `render::Texture` in a canvas, kept alive until drawn. |
| [`ImGuiSettings.h`](ImGuiSettings.h) | `setFontSizePixels` and `setSupersample`, process-wide. |
| [`ImGuiFonts.h`](ImGuiFonts.h) | The framework's font in ImGui, with its bold and italic faces: `imgui::canvasFont`. |
| [`ImGuiLayer.h`](ImGuiLayer.h) | Game thread: one ImGui frame for every visible canvas, cloned and published with their quads. |
| [`ImGuiRenderer.h`](ImGuiRenderer.h) | Render thread: rasterizes the frame into the atlas, then draws each canvas's slice as a world quad. |
| [`ImGuiTextures.h`](ImGuiTextures.h) | ImGui's font texture, created on the game thread, its new glyphs copied for the render thread. |

## A panel in a vrui layout

```cpp
#include "imgui/UIImGuiPanel.h"

auto readout = std::make_shared<imgui::UIImGuiPanel>("BeamReadout", 8.0f); // 8 vrui units wide
readout->setStyle(vrui::F4VR_PANEL_STYLE);
readout->setContent([this] {
    ImGui::Text("FOV: %.1f", beamFov());
    ImGui::ProgressBar(beamFade());
});
row->addElement(readout);
```

- **Sizing:** the constructors and `setMaxWidth` of a `vrui::UITextPanel`
  ([panel sizing](../vrui/README.md#panel-sizing)). Content measured by drawing it reaches the layout
  one frame late.
- **Resolution:** 48 pixels per unit (`CANVAS_PIXELS_PER_UNIT`) of the element's own size. The content
  is laid out at scale 1 and the quad is stretched with the layout, so in a container scaled 1.6 it is
  1.6x softer. `setSupersample` buys that back, for memory.
- **Not pressed by a finger:** its content is pixels. It is worked with a wand's ray, see
  [Interactive canvases](#interactive-canvases); buttons to touch go beside it, in vrui.
- **Not a `vrui::UIPanel`:** it takes the same style and sizing, but ImGui draws its chrome.
- **Hidden by the depth test** (`setOccluded`), not by the scene's draw order.

It lives here, not in `vrui/`, so vrui doesn't depend on ImGui and it goes away with
`F4CF_WITH_IMGUI_UI`. A mod that creates no panel doesn't link it.

## A canvas placed by hand

For a quad that isn't a vrui element: on a node, a billboard, any transform.

```cpp
#include "imgui/ImGuiCanvas.h"

_canvas = std::make_unique<imgui::Canvas>("BeamTuning", 512, 320); // pixels in the shared atlas
_canvas->setContent([this] { ImGui::SliderFloat("FOV", &_fov, 10.0f, 120.0f); });
_canvas->setPlacement([this](imgui::CanvasPlacement& out) {
    out.transform = _node->world; // +X right, +Z up, faces along +Y
    out.worldWidth = 16.0f;
    out.worldHeight = 10.0f;
    return isOpen();              // false skips it this frame
});
```

- **Both callbacks run on the game thread.** The placement may read nodes and game state (never read
  `node->world` render side). The content runs inside the framework's `Begin`/`End` for the canvas, so
  it calls widgets only: no `Begin`/`End` of its own, no D3D.
- **Lifetime:** construct it and the framework pumps it; destroy it and it's gone. A hidden canvas
  (`setVisible(false)`) costs nothing, and its content doesn't run.
- **`setPixelSize`** is cheap at any time, as the atlas is repacked every frame, so a canvas that grows
  can keep its pixel density.
- **Padding and border** are the canvas's. A combo, popup or child window the content opens has ImGui's
  own.

## Interactive canvases

`setInteractive(true)` makes a wand's ray the pointer: where it meets the canvas is ImGui's mouse, the
trigger its left button, the thumbstick its wheel. The content is written as for a screen.

```cpp
panel->setInteractive(true); // Canvas::setInteractive for a canvas placed by hand
panel->setContent([this] {
    if (ImGui::Button("Reset")) {
        reset();
    }
    ImGui::SliderFloat("FOV", &_fov, 10.0f, 120.0f);
});
```

- **Which hand:** one owns the pointer. A hand whose ray is alone on a canvas owns it. With both on, the
  hand that pressed a canvas last, the primary before either has. The owner keeps it while its trigger
  is held. `imgui::pointer().setHands` limits it to one hand.
- **Where the ray is:** from the wand's UI node (`primaryUIAttachNode`, `secondaryUIOffsetNode`), whose
  +Y is how the hand aims a weapon. By default it starts a little in front of the wand and points 18°
  above that aim and 5° inward, where the hand points. `rayOffset` is that placement, for the right
  hand, mirrored for the left.
- **What it meets:** a canvas from the front only, the nearest interactive one. It passes through the
  rest, and through the world.
- **The game loses the wand** while its ray is on an interactive canvas: every button and axis, for each
  hand by its own ray (owner `ImGuiPointer`). A trigger pull fires no weapon. The mod's own
  `VRControllers` reads still see it.
- **A press is held** until the trigger is released. It stays on its canvas while the ray slides off,
  following the ray on the canvas's plane (or [cylinder](#a-curved-panel)) up to a canvas's size past
  its edges, so a slider drags to its end. The wand stays hidden until the release, even when the
  canvas hides mid-press. The press ends when the trigger is back near rest.
- **A pull from outside is the game's:** a trigger already down when the ray arrives presses nothing,
  and the wand isn't hidden until it is released.
- **The thumbstick scrolls** what the pointer is on, a list, a child window or an open combo, faster the
  further it's pushed, up to `scrollSpeed` lines a second. A push more to the side scrolls nothing, so
  the content can take it. The player doesn't turn, as the wand is hidden.
- **What is drawn,** while the pointer is on a canvas: the owner's ray, up to a length, fading at both
  ends, and a disc where it meets the canvas, the same size at any distance. The other hand's ray, if on
  a canvas, is drawn at 0.3 opacity with no disc. Over the panels (`DRAW_ORDER_POINTERS`), never hidden
  by the world. A drawn ray means its wand is the UI's.
- **Game menus take over:** while one takes the controllers' input (`f4vr::isGameMenuTakingInput`), the
  pointer is on no canvas. The canvases stay shown, under the menu.
- **State:** `imgui::pointer().state()` has the canvas, the position in its pixels, the hand, whether it
  presses, the ray, and whether ImGui uses the mouse (`io.WantCaptureMouse`).
  `UIImGuiPanel::isPointedAt()` is whether it's on that panel. It is updated when the ImGui frame is
  built, after the mod's frame: the content reads this frame's, `onFrameUpdate` the last one's.
- **Cost:** none for a canvas that isn't interactive. With no interactive canvas shown and no press
  held, nothing is read from the game.

### How the pointer looks

[`PointerStyle`](ImGuiPointer.h); lengths in world units.

| Field | Default | What it is |
| ----- | ------- | ---------- |
| `rayOffset` | -2,3,-1; -18,0,-5 | The ray's place on the wand's UI node: position, then degrees around x, y, z |
| `drawn` | `true` | `false` draws nothing, for a mod that draws its own from `state()` |
| `rayColor` | 190,255,190,200 | The ray's color and opacity |
| `rayWidth` | 0.2 | |
| `rayMaxLength` | 25 | The ray ends there, or at the disc when the canvas is nearer |
| `rayFade` | 1 | The length it fades over at each end; 0 for none |
| `markColor` | 215,255,215,180 | The disc's color and opacity |
| `markSize` | 0.45 | The disc's radius, border included |
| `markBorderColor` | 215,255,215,100 | |
| `markBorderWidth` | 0.08 | The outer part of the radius; 0 for none |
| `scrollSpeed` | 30 | Lines a second at a full push; 0 for no scrolling |

```cpp
auto style = imgui::pointer().style();
style.rayColor = render::Color::rgba(10, 250, 120, 200);
imgui::pointer().setStyle(style);
```

The framework has no config keys for it. A mod that lets the player set them reads them itself
(`getColorValue`, `getTransformValue`) and calls `setStyle`.

## Images

A [`render::Texture`](../render/README.md#images) in a canvas, loaded through the engine or made from
pixels:

```cpp
#include "imgui/ImGuiImage.h"

_icon = render::Texture::load("Data\\Textures\\MyMod\\icon.dds");
_picture = render::Texture::fromPixels("MyMod picture", width, height, rgba);

panel->setContent([this] {
    imgui::image(*_icon, ImVec2(64.0f, 64.0f));                       // as ImGui::Image
    imgui::addImage(ImGui::GetWindowDrawList(), *_picture, min, max); // as ImDrawList::AddImage
});
```

- **Use these, not `ImGui::Image`,** which takes a bare pointer nothing keeps alive for the render thread.
  The published frame holds the texture until drawn, so it can be dropped at any time.
- **Not ready yet, or missing:** draws nothing, and keeps its room.
- **Colors are the file's:** an sRGB view is encoded back, as the primitive renderer does.
- **Cost:** a draw per image; an sRGB one adds two pipeline state changes.

## A dedicated panel

The canvases share one 1024x1024 atlas, which is also ImGui's display: fine for several small panels.
A mod whose UI is one large panel makes it dedicated:

```cpp
panel->setDedicated(true); // Canvas::setDedicated for a canvas placed by hand
```

- **Its own texture,** of its size: 48 pixels per unit up to 4096 a side (~85 units,
  `MAX_DEDICATED_CANVAS_PIXEL_SIZE`), where an atlas panel is scaled down past ~21 units.
- **It is ImGui's display,** so ImGui places what the content opens against it: a combo near its bottom
  opens upward, a modal (`ImGui::BeginPopupModal`) is centered on it. In the atlas they're placed
  against the atlas, and what falls outside the canvas isn't shown.
- **The only ImGui panel:** while it's shown, the mod's other canvases aren't drawn and their content
  doesn't run. Of two shown, the first created wins. vrui elements and other mods aren't affected.
- **Cost:** its pixels times the supersample, on each side: 38x30 units at 1.5 is 2736x2160, ~24 MB. It
  is made again when the size changes, so one sized to its content is remade as the content resizes.
  No atlas is made for a mod with only a dedicated panel.

## A curved panel

A wide flat panel's sides are further away and turned from the player. A curved one is bent toward
them:

```cpp
panel->setCurveRadius(20.0f); // CanvasPlacement::curveRadius for a canvas placed by hand
```

- **The curve:** around a cylinder of that radius, its axis along the panel's height, in front of its
  middle. A panel as far from the player as its radius faces them everywhere. The radius is in vrui
  units and scales with the panel: one 120 units away at scale 6 takes 20.
- **What stays:** its middle, and its width along the curve, so the same pixels. Its sides come forward.
- **Range:** 0 is flat. Under the width over pi it is taken as that: at most half a cylinder.
- **The pointer** meets it where it's drawn, and a press that slides off follows the cylinder.
- **Drawn** as flat strips, one per 2° (29 for a panel as wide as its radius); laid out and rasterized
  flat.
- **Layout:** vrui lays it out flat, so it suits a panel that is a root alone; in a container its sides
  leave the row.
- **Tuning:** the [dev layout](../vrui/README.md#dev-layout)'s `Curve`.

## A panel the player moves

A panel in the world (`attachPresetToWorldAtHMD`) can be moved with the ray that works it. vrui moves
it; the mod sets the binding:

```cpp
panel->setInteractive(true);
g_uiManager->setWorldMoveBinding({ .hand = vrcf::Hand::Any, .type = vrcf::ActivationType::Press, .button = vr::k_EButton_Grip });
g_uiManager->attachPresetToWorldAtHMD(panel, 350.0f);
```

- **How:** point at it and work the binding on the hand that owns the pointer. It follows the hand
  around the point the ray met, level, until the button is released. The rest is in
  [vrui](../vrui/README.md#moving-a-world-ui).
- **The panel's part:** an interactive panel tells vrui each frame the pointer is on it, and where
  (`markPointedAt`). Only the owner's ray counts.
- **What moves:** the panel's root, with everything in it. A root attached to a hand or the HMD isn't
  moved.

## Sizes, pixels and legibility

| Knob | Scope | What it changes |
| ---- | ----- | --------------- |
| pixel size (`Canvas`) / vrui size (`UIImGuiPanel`) | per canvas | its pixels and its size in the world: how legible it is |
| `setFontSizePixels` (24, 16 to 128) | process | how big text looks, not the layout |
| `setSupersample` (1.5, 1 to 4) | process | how finely it is rasterized; layout is always 1x |

- **Set the process-wide two before the first canvas draws:** they are read then, and later calls do
  nothing.
- **Other text sizes:** `ImGui::PushFont(nullptr, size)`. ImGui rasterizes each size at the supersample,
  so every size is sharp.
- **Memory:** the atlas is 1024 times the supersample on each side, ~9 MB at 1.5, 16 MB at 2, made when
  the first canvas draws. Past the headset's resolution a larger factor shows nothing.

## Font

The framework's text font: the mod's `Data\Interface\<ModName>\<ModName>.ttf` when it ships a usable
one, Roboto Medium otherwise, from the same bytes `render::TextFont` draws. ImGui's own font is the last
resort, with a warning.

`imgui::canvasFont(FontStyle)` gives the bold and italic faces, to push or draw with at any size:

```cpp
ImGui::PushFont(imgui::canvasFont(imgui::FontStyle::Bold), 0.0f); // 0 keeps the current size
ImGui::TextUnformatted("Warning");
ImGui::PopFont();
```

A mod's faces are `<ModName>-Bold.ttf` and `<ModName>-Italic.ttf` beside its font
([`TextFont.h`](../render/TextFont.h)); one it doesn't ship is Roboto Black or Roboto Medium Italic.
Faces share a baseline only when their vertical metrics match, as one family's do.

## How it is drawn

```
game thread (frame end)                         render thread (Submit hook)
──────────────────────────────────              ─────────────────────────────────
imgui::internal::onFrameEnd()                   draw callback (DRAW_ORDER_PANELS)
  ├─ resolve placements → world quads             ├─ write the glyphs ImGui added
  ├─ the pointer: a wand's ray on the quads       ├─ rasterize the frame into the atlas
  │    of the interactive canvases → ImGui's      ├─ group quads by occluded / on-top
  │    mouse, its ray and mark → their own        └─ composite each canvas's atlas slice
  │    layer over the panels, and the wand             as a world quad, back to front
  │    hidden from the game
  ├─ one ImGui frame, every visible canvas
  │    as its own window packed into the atlas
  ├─ measure each canvas's content
  ├─ ImGui's texture requests: the font
  │    texture created, new glyphs copied
  └─ clone the draw data + publish  ────────────►
```

- **One frame, one atlas:** each canvas is a window in its own sub-rect, drawn as one quad. The quads are
  sorted by occlusion group, so all canvases take two draws at most. A dedicated canvas is a frame of
  its own, into its own texture.
- **Rasterized flat first,** because ImGui clips with 2D scissor rectangles (scrolling, child windows,
  tables), which have no meaning on a turned 3D quad.
- **The draw data is cloned** (`ImDrawList::CloneOutput`): the next `NewFrame` reuses ImGui's buffers,
  so reading them render side would be a use-after-free. A few KB a frame.
- **The font texture is made on the game thread** (`ImGuiTextures.cpp`). ImGui's DX11 backend would
  write it while drawing, on the render thread, while ImGui writes the same pixels for the next frame.
- **No depth writes:** canvases don't hide each other and are drawn back to front. They are hidden by
  the world by default ([occlusion](../render/README.md#occlusion-by-the-world)), except where the
  engine's depth can't be captured, so it is a preference, not a promise.
- **A snapshot** of the texture, for devbench's [`snapshot`](../devbench/README.md) action (`ImGuiSnapshot.cpp`):
  the render thread copies it after it draws a frame into it, and reads the copy once the GPU has made it,
  without waiting for the GPU. The caller waits on its own thread, and writes the PNG through WIC.
- **Nothing is registered until the first canvas exists,** so a mod with none pays nothing.

## Building without it

`F4CF_WITH_IMGUI_UI` (on by default) builds the layer; the vcpkg manifest pulls `imgui` 1.92.9 with the
`dx11-binding` and `win32-binding` features, which a mod's own `vcpkg.json` needs too, as vcpkg reads
the top-level manifest. With it off, `src/imgui/` isn't built and its headers aren't exposed, so code
using `f4cf::imgui` fails to compile rather than drawing nothing. The rest, the text font included,
still builds: `stb_truetype` is vendored for that reason.

The design follows the reference library's `knowledge-base/imgui_ui_overlay_architecture.md`.
