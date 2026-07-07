# In-World Debug Draw Overlay (`f4cf::debug`)

Immediate-mode debug drawing for any framework mod: parametric wire primitives (line, point, arrow,
box, sphere, capsule, cone, axes, grid), raw polylines/meshes, screen-space HUD text, world-anchored
labels, and a "name: value" watch table — rendered on top of the VR view. API reference and usage
snippets: [`src/debug/README.md`](../../src/debug/README.md).

This doc records the design decisions, the rendering pipeline, the threading model, and the address
provenance. It is a generalization of ROCK's `DebugBodyOverlay` per the reference library's
`knowledge-base/debug_draw_overlay.md` (line-level citations below reference
`github-repos/gold/ROCK/src/physics-interaction/debug/DebugBodyOverlay.cpp`).

---

## 1. Rendering approach — why D3D11 + `Submit` hook, not scene-graph NIFs

| | **D3D11 + `Submit` hook** (chosen) | Scene-graph NIF (`f4cf::vrui` style) |
|---|---|---|
| Arbitrary topology (any polyline/mesh) | ✅ push verts to a buffer | ❌ needs procedural `BSGeometry` per shape |
| Screen-space HUD text + value watch | ✅ trivial (2D overlay + bitmap font) | ❌ Scaleform / font NIF, awkward |
| Depth occlusion (hidden behind walls) | ❌ draws on top only | ✅ free |
| Threading | render thread → needs handoff | game thread |
| Version-specific offsets | one (VR camera globals) | none |

For a general debug helper whose headline features are "any shape anywhere" + HUD text/watch, D3D
wins on the rows that matter and only concedes depth occlusion — which for debug drawing is usually
*wanted* (x-ray visibility of colliders/zones through geometry).

## 2. Architecture

```
game thread (ModBase::onFrameUpdateSafe)                render thread (OpenVR compositor)
─────────────────────────────────────────               ─────────────────────────────────
DebugDraw::onFrameStart(config...)                      vrSubmitHook(eye, texture, ...)
  ├─ absorb INI changes (master/channels/hotkey)          ├─ s_enabled atomic false → chain original
  ├─ clear last frame's command list                      └─ eye == Left:
  └─ re-emit unexpired sec>0 shapes                            ├─ snapshot frame (mutex)
                                                               ├─ beginFrame: save FULL D3D state
mod onFrameUpdate()                                            ├─ RTV over submitted texture (cached)
  └─ dd().sphere/cone/line/watch/... appends                   ├─ upload engine eye matrices (CB b0)
     (tessellated to line segments at call site)               ├─ draw color-runs of segments ×2 instanced
                                                               ├─ draw text quads (5×7 bitmap font)
DebugDraw::onFrameEnd()                                        ├─ endFrame: restore FULL D3D state
  ├─ lay out watch table as text rows                          └─ chain original Submit
  ├─ lazy renderer::ensureInstalled()
  └─ renderer::publish(move(frame))  ── mutex+atomic ──►
```

- **Producer** ([`src/debug/DebugDraw.cpp`](../../src/debug/DebugDraw.cpp)): every primitive is a
  thin tessellator emitting world-space line segments into this frame's list; text/labels/watch
  append text entries. `sec > 0` shapes are additionally retained (pre-tessellated) in a store and
  re-emitted each frame until expiry (timestamp compare — frame-rate independent). `point`/`sphere`/
  `box` accept a `distanceScaled` flag: the size (radius / half-extents) is multiplied by
  `distance-to-camera / reference` at tessellation time (`_cameraPos` captured from `HmdNode` in
  `onFrameStart`) so a marker holds a constant apparent size — the same constant-on-screen idea as
  billboard labels, but done on the game thread by adjusting the size, so it needs no renderer
  support. `capsule`/`cone` are excluded (a segment / a directional volume has no single scale
  anchor).
- **Consumer** ([`src/debug/DebugDrawRenderer.cpp`](../../src/debug/DebugDrawRenderer.cpp)): dumb —
  no lifetime logic; draws whatever frame was last published.

## 3. Zero cost when unused (the lazy-hook contract)

- `DebugDraw::onFrameStart/onFrameEnd` (driven by `ModBase` around the mod's `onFrameUpdate`)
  early-return on one relaxed atomic read until the **first draw/watch call of the session**
  (`s_everUsed`). A mod that never draws pays two atomic reads per frame and installs **no hook**.
- On first actual content, `renderer::ensureInstalled()` lazily compiles the shaders, creates the
  D3D pipeline objects off the game's device, and patches the compositor vtable. Each unavailable
  dependency (device, compositor) just logs once and retries next frame.
- Once installed with nothing to draw, the Submit hook is one relaxed atomic read per call.
- When the overlay is disabled (INI/hotkey/`setEnabled(false)`) appends are skipped at the call
  site behind a single pre-computed bool (`_appendActive`).

## 4. Rendering pipeline details (ported from ROCK)

- **Device/context** straight off `RE::BSGraphics::RendererData::GetSingleton()` (`device` @0x48,
  `context` @0x50) — no swapchain creation (ROCK :1142-1152).
- **Injection**: vtable index **5** (`Submit`) on the live `IVRCompositor`, same vtable-swap
  technique as `vrcf::VRControllersSuppressor`. Draw on `Eye_Left` only — FO4VR submits one
  double-wide texture; the shader's clip/cull split carries both halves (ROCK :2591-2628).
- **Camera**: the engine's own per-eye column-major view-projection + posAdjust are borrowed each
  draw, so game-world input coordinates project exactly where the game drew that frame — no OpenVR
  pose math, HMD pose already baked in (ROCK :981-1002). Address in §6.
- **Stereo instancing**: every draw is instanced ×2; `SV_InstanceID` selects the eye matrix, X is
  packed into the correct half via `eyeOffsetScale` and clipped at the seam via `SV_ClipDistance0`
  (ROCK :239-280).
- **State save/restore is mandatory**: `beginFrame`/`endFrame` snapshot and restore VS/PS (+ class
  instances), input layout, topology, rasterizer, depth-stencil, blend, RTVs, DSV, viewports, and
  vertex/index buffers. We draw in the middle of the game's pipeline; a missed field visibly
  corrupts the frame (ROCK :1187-1231).
- **Line batching**: producer publishes segments sorted by color; the renderer uploads one dynamic
  VB and issues one `DrawInstanced` per same-color run.
- **Text**: self-contained 5×7 bitmap font (7 bit-rows per glyph, one quad per lit pixel, no
  texture/asset; ROCK :2129-2314), in three modes. **Screen HUD** (`text()`): quads in clip space,
  duplicated into both eye halves. **World-anchored screen text** (the watch table): the anchor is
  CPU-projected per eye and the glyph quads laid out in screen space at that spot (`TextAlign`
  left/center/right). **World billboard** (`label()`): the glyphs are emitted as *world-space* quads
  on a viewer-facing plane (world-up right/up basis, from `RenderFrame::cameraPos`) and drawn through
  the **stereo geometry shader** — so a label shares the shapes' exact projection/depth, stays welded
  to its world point, tilts with the world (not screen-upright), and scales with distance. This is why
  a `label()` no longer appears to rotate as you move your head the way flat screen text does.
- **RTV cache**: the RTV over the submitted texture is cached keyed by texture pointer + desc — the
  texture is stable frame-to-frame (ROCK :1167-1185).
- ROCK's second hook (`write_call<5>` at `0xD844BC` on the render path) is intentionally **not**
  ported — in ROCK it only chains the original; the draw lives entirely in the Submit hook, and the
  producer side here is driven from `MainLoopHook` instead.

## 5. What was stripped vs. added relative to ROCK

**Stripped** (physics-specific): `extractBody()` hknp world-walking + offset table, convex-hull
decode + GPU shape cache, all `BodyOverlayRole`/`AxisOverlayRole`/marker/skeleton role enums and
their per-role color tables.

**Added**: the public immediate-mode API + `polyline`/`mesh` raw hatch, the game-thread persistence
store (`sec > 0`), channels + INI toggles + in-headset hotkey, the `watch()` table, and
`havokToGame()` / `nodeAxes()` / `nodeBounds()` conveniences.

## 6. Address provenance (VR 1.2.72)

| Address | What | Where kept | Source |
|---|---|---|---|
| `0x6235AC8` | VR render camera globals block | `f4vr::vrRenderCameraGlobals` ([`F4VROffsets.h`](../../src/f4vr/F4VROffsets.h)) | ROCK `DebugBodyOverlay.cpp:983`, `knowledge-base/debug_draw_overlay.md` §5.3 |
| `+0x25D0` | → camera data block pointer | `VR_RENDER_CAMERA_DATA_OFFSET` | same |
| camera `+0xD0` / `+0x2E0` | eye 0 / eye 1 view-projection 4×4 | `VR_RENDER_CAMERA_EYE{0,1}_VIEW_PROJ_OFFSET` | same |
| `+0x2590` / `+0x25C0` | eye 0 / eye 1 posAdjust float3 | `VR_RENDER_CAMERA_EYE{0,1}_POS_ADJUST_OFFSET` | same |
| vtable idx 5 | `IVRCompositor::Submit` | `DebugDrawRenderer.cpp` | ROCK `DebugBodyOverlay.cpp:2614`, OpenVR ABI |

This is the overlay's **only** version-specific game address. If overlay geometry reads as garbage
after a game/runtime change, suspect it first.

## 7. Gotchas & limitations

1. **On-top only** — the Submit path has no scene depth bound; shapes draw through walls (usually
   desired). True occlusion would need a hook earlier in the render, out of scope.
2. **Game thread only for draws.** The render side never touches game state; the producer side is
   not thread-safe by design.
3. **Budgets**: 64k line vertices / 128k text vertices per frame; over-budget appends are dropped
   with a rate-limited log — a runaway loop degrades, never crashes.
4. **VR only** — on flat Fallout 4 the overlay logs once and stays inert.
5. **Hook lifetime**: the vtable patch is never removed (process-lifetime, like the input-suppression
   hook). The original `Submit` is always chained.
6. **Watch-table font** covers `A-Z 0-9 - + = . , : / ( ) %`; other characters render blank.
7. **Screen-space HUD is cropped in VR.** `text()` draws into the eye render target's raw pixels; the
   corners are cropped by the lens, so corner HUDs are *not visible in-headset*. The **watch table is
   world-anchored** to avoid this: by default a point in front of the HMD (`defaultHudAnchor()` —
   `HUD_FORWARD_DIST` along the look axis, then offset along the HMD's local up/right per the
   `sDebugDrawHudPlacement` config: `center` / `center-{left,right}` / `center-top{,-left,-right}`,
   bottom row `HUD_BOTTOM_FRACTION` below centre, top row `HUD_TOP_FRACTION` above), or an explicit
   `watchAnchor()` / `watchAnchorNode()` (e.g. offhand controller = wrist display). Both give correct
   stereo depth for free. Plain `text()` is really only useful on flat FO4 or a mirror/2D capture. The
   head local axes are `rotate.Transpose() * (0,1,0)` forward / `(0,0,1)` up / `(1,0,0)` right — if the
   default HUD lands off-centre, adjust those in `defaultHudAnchor()`. HUD rows are `TextAlign`-ed
   (left/right for the side placements, else centred) so they keep a clean edge on their side of the
   view. The HUD rides `HmdNode`, so it tracks head yaw/pitch/roll; that's fine for a head-locked
   readout, and world `label()`s use the billboard path instead when a truly world-stuck tag is needed.

## 8. Configuration

`[Debug]` INI keys provided by `ConfigBase` for every mod (hot-reloadable):
`bDebugDrawEnabled` (master, default on), `sDebugDrawDisabledChannels` (comma-separated),
`sDebugDrawToggleBinding` (in-headset toggle, [input-binding grammar](../input-binding.md)).
Runtime equivalents: `setEnabled()`, `setChannelEnabled()`.
