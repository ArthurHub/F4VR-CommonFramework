# Changelog

The notable changes in each version of F4VR-CommonFramework. Only the big things are listed:
new modules and capabilities, and changes a consuming mod has to act on. Small fixes, refactors
and tooling tweaks are left to the git history.

Versions follow `set(VERSION ...)` in the root [`CMakeLists.txt`](../CMakeLists.txt). While the
major version is `0`, a minor bump may break the API; the **Upgrading** notes say what to change.

## 0.5.0 — unreleased

- **vrui rotation**: an element takes a rotation relative to its parent (`UIElement::setRotation`),
  and everything under it turns with it. On a root it is relative to the attach node, so a UI no
  longer has to face the way its node faces. The dev layout tunes it live as `Rot`.
- **vrui on the offhand**: `UIManager::attachPresetToOffhandWandTop` and
  `attachPresetToOffhandWandRight` attach a UI to the offhand controller, as the primary wand's top
  and left presets do for the primary hand. `attachPresetToOffhandWrist` lays a UI on the inner
  wrist and keeps it on the forearm as the arm moves, when an arm is drawn at the controller (FRIK).
- **vrui in the world**: `UIManager::attachPresetToWorldAtHMD` puts a UI where the HMD is and leaves
  it there, so it stays in the world while the player walks and turns. `recenterWorldElement` puts
  it where the HMD is now, back in front of the player. Given a distance, the preset does that by
  itself when the player walks further than that from the UI.
- **vrui finds the finger**: the fingertip that presses a button is read from the player's skeleton,
  and either hand can press: a UI on a hand is pressed by the other hand, any other UI by the nearer
  finger, and the hand that presses points and gets the haptic. Without a hand drawn at
  the controller, the controller presses the button and a small gold sphere marks where.
  `[Debug] bVRUIShowFingerTip` shows the sphere on the fingertip too. `f4vr::Skelly` maps the
  skeleton's bones by itself, so its fingertip and bone reads no longer need `initBoneTreeMap`
  called first, and its fingertip is 2.0 units past the last finger bone, up from 1.8.
- **vrui points the hand**: the hand whose finger is near a button is pointed through FRIK's API by
  the framework, under the tag `<mod name>_UI`, so a mod with a vrui UI no longer writes an adapter
  for it. With FRIK's second API table (FRIK 0.79) the pose is set a little above the default
  priority, to win over a pose the mod itself holds on that hand.
- **vrui presses from the front**: a finger interacts with a button only when it comes to it from
  in front of the button's face while the player looks at the button. A hand behind a UI, or near
  a UI the player does not look at, no longer points and cannot push a button, and a hand that
  comes out from behind a UI points only once its finger turns back toward a button. A hand that
  points keeps pointing while its finger is near a button, so it holds the pose between presses.
  A button is pushed only by a finger that crossed its face from the front, so a finger coming up
  from behind a button no longer moves it.
- **vrui updates itself**: `ModBase` runs the UI manager's frame update after the mod's
  `onFrameUpdate()`, while the player is loaded, so a mod no longer calls it. A press handler then
  runs after the mod's frame. A mod that needs the UI updated at a certain point of its frame calls
  `g_uiManager->onFrameUpdate()` there, and the framework skips its own call in that frame.
- **vrui dev layout by a flag**: the dev layout is turned on with `[Debug] bVRUIDevLayout`, with
  nothing for the mod to call, and keeps its lines in `<mod>_DevLayout.ini` beside the mod's INI, a
  section for each attached root. The lines of a detached root stay, so what was tuned on a screen
  is still there after going to another screen and back.
- **imgui pointer**: an ImGui canvas can be operated. `Canvas::setInteractive` and
  `UIImGuiPanel::setInteractive` make a wand's ray the pointer of the canvas and its trigger the
  mouse button, so buttons, checkboxes, sliders and lists work as ImGui does them. One hand owns the
  pointer at a time: the one that pressed last, and the primary hand before either has.
  `imgui::pointer()` is that pointer: `state()` says what it does, and `setHands` limits pointing to
  one hand. While the pointer is on a canvas its ray is drawn, up to a set length and with a fade at
  both ends, and a mark with a border on the canvas, over the panels and not hidden by the world.
  The ray of the other hand is drawn fainter, and with no mark, while it is on a canvas too.
  `setStyle` takes a `PointerStyle`: where the ray is on the hand and the whole look, or the look
  turned off for a mod that draws its own.
- **imgui pointer hides the wand**: while a hand's ray is on an interactive canvas, its whole wand is
  hidden from the game and from other mods, so a pull of the trigger on a canvas fires no weapon. A
  press that began on a canvas stays on that canvas and stays hidden from the game until the trigger
  is released, also when the ray slides off the canvas or the canvas is hidden. A trigger that is
  already down when the ray comes to a canvas stays the game's until it is released.
- **imgui pointer scrolls**: the thumbstick of the hand that owns the pointer is ImGui's mouse wheel,
  so a list or a child window that is longer than its room scrolls while the ray is on it, and the
  player does not move. `PointerStyle::scrollSpeed` is the speed at a full push, in lines of text a
  second.
- **imgui content has ImGui's padding**: the zero padding and border of the canvas window no longer
  reach what the content opens. A combo's list, a popup, a dialog and a bordered child window have
  ImGui's own window padding and border, where they had none, so the content of a bordered child
  moves in by that padding.
- **imgui dedicated panel**: `UIImGuiPanel::setDedicated` and `Canvas::setDedicated` are for the one
  large panel that is a mod's whole UI. It has a texture of its own size in place of a part of the
  shared atlas, so it keeps 48 pixels per vrui unit up to 4096 pixels a side, where a panel in the
  atlas is scaled down past 1024. While it is shown it is ImGui's display, so a combo's list, a
  popup and a dialog that its content opens stay inside the panel, and a dialog is centered on it.
  ImGui has one display, so while a dedicated panel is shown it is the only canvas the mod draws.
  The atlas texture is now created when a canvas is first drawn into it, so a mod with only a
  dedicated panel does not have it.
- **imgui curved panel**: `UIImGuiPanel::setCurveRadius` bends a panel toward the player, around a
  cylinder of that radius whose axis runs along the panel's height in front of its middle
  (`CanvasPlacement::curveRadius` for a canvas placed by hand). With the radius as the distance the
  panel stands at, its sides are as far from the player as its middle and face them, so a wide panel
  reads the same all over. The pointer meets the panel where it is drawn, and the dev layout tunes
  the radius live as `Curve`.
- **vrui in the world moved by the player**: `UIManager::setWorldMoveButton` lets the player move a
  UI that stays in the world. They point at it and hold that button on the hand that points, and the
  UI moves and turns with the hand, kept level, until the button is released. It follows the hand
  smoothly, which steadies the hand's shake: `setWorldMoveSmoothing` is how smoothly. It stays there
  until `recenterWorldElement` or the recenter distance puts it in front of the player again. A UI
  is moved by a ray on it, which an interactive `UIImGuiPanel` tells vrui of by itself.
- **config color**: `ConfigBase::getColorValue` reads a color from the INI, written as `r,g,b` or
  `r,g,b,a` in 0 to 255, as `getTransformValue` reads a transform.
- **render gradients**: a fill takes a color for each corner and blends between them
  (`PrimitiveDraw::addTriangle` and `addQuad` with a color per corner), so a gradient or a fade is
  one shape. Every vertex carries its color, so a change of color no longer starts a new draw call:
  the lines of a layer go out in one draw, its screen text in one, and its fills and world text in
  one for each texture.

**Upgrading**

- `vrui::UIModAdapter` is removed: delete the mod's adapter and its
  `g_uiManager->onFrameUpdate(&adapter)` call, which the framework now makes. A mod that keeps the
  call, to have the UI updated at a certain point of its frame, passes no argument.
  `vrui/UIModAdapter.h` is now `vrui/UIFrameUpdateContext.h`. A mod that poses the hands itself
  gives `g_uiManager->setHandPointingHandler(...)` a function that points a hand and releases it,
  in place of the adapter.
- A mod's own pressable element finds its finger with `UIElement::updateFinger`, which also reports
  it for the hand to point, in place of `getInteractionFingerTip` and
  `UIFrameUpdateContext::markAnyPressableCloseToInteraction`.
- `UIUtils::triggerInteractionHeptic` takes the hand to buzz: pass `true` for the primary hand.
- `vrui::UIDebugWidget` is removed. To mark a point in the world use the debug draw overlay
  (`debug::dd().sphere(...)`).
- `UIManager::enableDevLayoutViaConfig` is removed: delete the call, and add `bVRUIDevLayout = true`
  to the INI's `[Debug]` section while tuning. `ConfigBase::debugVRUIProperties` is removed with it,
  and a `[VRUI_DevLayout]` section left in an INI is no longer read.
- `render::FillTriangle` has a color for each corner, `colorA`, `colorB` and `colorC`, in place of
  `color`. `addTriangle` and `addQuad` with one color are unchanged.

- `f4vr::PlayerNodes` and `getPlayerNodes()`, deprecated in 0.4.0, are removed. Use
  `getVRPlayerNodes()`, which returns CommonLibF4's `RE::VRPlayerNodes`: the same table with
  camelCase member names, some of them corrected (e.g. `HmdNode` → `hmdNode`,
  `SecondaryWandNode` → `secondaryWandNode`, `unk750` → `equippedWeaponNode`).

## 0.4.1 — 2026-10-01

- **`f4cf::devbench`**: every mod gets its own [devbench](https://github.com/ArthurHub/devbench)
  tool, so an AI agent or a script can drive it in the running game over MCP or REST. `ModBase`
  registers it, named after the mod, with generic actions for health, the mod's state as of its
  last frame, session config overrides and every perf site as a tree per thread, read without
  the game thread; a mod adds its own
  actions with `devbench::addAction`, its state with `devbench::setStateProvider`, and publishes
  events with `devbench::emit`; the framework publishes its own for session loads, config reloads
  and overrides, and input suppression changes. Opt out with `Settings::devbenchTool = false`.
- **`f4cf::perf`**: `PerfMonitor` is replaced by sites. `F4CF_PERF_FUNCTION()` and
  `F4CF_PERF_SCOPE("label")` time a function or a block on any thread into a lock-free histogram
  (p50/p95/p99 within ~3%, no sample cap), and each site records the site it ran inside, so they
  nest into a tree with self time. Every read comes with the frame context of the same window:
  the frame interval, the budget at the headset's refresh rate, and the VR compositor's GPU time,
  late starts and reprojected frames. The framework times its own per-frame work, every
  controller-state poll of its own and overlay drawing in the
  Submit hook. Recording is off until `perf::setEnabled(true)`, which the
  devbench tool does on first use; the log level no longer turns it on. Without devbench, the
  `sDumpDataOnceNames` names `perf_reset` and `perf` start a window and log it as a table.
- **GPU time**: GPU sites and `perf::GpuTimer` time the GPU work a mod issues itself, from
  timestamps read back 1-3 frames later without waiting. The Submit host times every overlay layer
  this way, the shared setup and each draw callback by name, shown as a `gpu` tree in the perf
  report and as plots in a Tracy build. The host also names its drawing for frame capture tools
  (RenderDoc, PIX) while one is attached.
- **Tracy**: a `Tracy` build configuration next to Debug and Release, a Release build with the
  [Tracy](https://github.com/wolfpld/tracy) profiler client v0.14.1 in it. Every perf site is also
  a Tracy zone, each frame is marked, the frame interval and the compositor's timing of each frame
  are plots, and devbench events are messages, so the Tracy viewer shows the timeline, frame by
  frame and thread by thread. The client is on demand and listens on localhost only; the other
  configurations never compile it in. The mod template builds it with its `tracy` build preset.

**Upgrading**

- `common::PerfMonitor` is gone: replace `static PerfMonitor perf("Name");` and
  `const auto timer = perf.scope();` with `F4CF_PERF_FUNCTION();` (or `F4CF_PERF_SCOPE("label")`
  for a block) from `perf/Perf.h`. Debug logging no longer turns timing on and nothing is logged
  any more; read the sites through the mod's devbench tool (`perf` action).
- The `Tracy` configuration gets only what your `CMakeLists.txt` gives it: change the Release-only
  `$<CONFIG:RELEASE>` settings to `$<CONFIG:Release,Tracy>`, and repeat the `/Ob2` to `/Ob3` fix for
  `CMAKE_CXX_FLAGS_TRACY`, as the mod template does. Add a `tracy` build preset
  (`"configuration": "Tracy"`) to build it. Configuring now downloads Tracy's source once;
  `-DF4CF_WITH_TRACY=OFF` drops the configuration and the download.

## 0.4.0 — 2026-09-25

**Rendering in VR, built into the framework.**

- **`f4cf::render`**: one shared OpenVR `Submit` hook that every framework overlay draws through,
  with lines, filled triangles, game-texture images and text in an embedded Roboto
  distance-field font. It can capture the engine's scene depth so overlays are hidden behind world
  geometry instead of always drawing on top ([design](tech/scene-depth-occlusion.md)).
- **`f4cf::debug`**: an in-world debug-draw overlay (shapes, labels, and a watch table in front of
  the HMD) for seeing what a mod is doing live ([design](tech/debug-draw-overlay.md)).
- **vrui panels**: UI elements drawn by the primitive renderer instead of one NIF per button:
  `UITextPanel`, `UIImagePanel`, `UIButtonPanel`, `UIToggleButtonPanel` and
  `UIMultiStateToggleButtonPanel`. They size themselves to their content, wrap text, share one
  panel style, and can show controller prompts generated from the actual input binding.
- **`f4cf::imgui`**: Dear ImGui windows drawn as world-space panels that fit into vrui layouts
  (`UIImGuiPanel`). Controlled by the `F4CF_WITH_IMGUI_UI` build option (on by default).
- **Activation spheres**: `WandActivationSphere` is now driven by a `WandActivationConfig` loaded
  from one INI section: a zone with an optional power-armor variant, two bindings, suppression you
  opt into per binding, configurable haptics, and a visual shown never / always / when inside /
  when available. The visual is a single mesh styled at runtime (preset + per-value overrides),
  and a zone can also be marked by an icon.
- **Player turning**: `f4vr::PlayerRotation` snap/smooth-turns the player the way the game's VR
  comfort settings say, even where vanilla turning is taken away ([design](tech/vr-player-rotation.md)).
- **Game-state helpers**: actor range and projectile queries, Havok collision layers, actor
  perception / combat / light-level access, live light refresh entry points, and
  `getVRPlayerNodes()` typed by CommonLibF4.
- **Input**: `VRControllers.setControllerStateAdjuster()` lets a mod correct the polled controller
  state when another mod remaps it for every reader (e.g. ROCK's trigger remap).

**Upgrading**

- Assets the framework ships now live in an `f4cf` folder inside the mod's own folders (sphere
  visuals, binding prompt icons, shared vrui icons). Re-copy them from `mod-template/`.
- The per-look sphere NIF variants are replaced by one mesh plus an `sSphereStyle` preset.
- A mod using `f4cf::imgui` must declare `imgui` (feature `dx11-binding`) in its own `vcpkg.json`.
  The mod template already does.
- **Deprecated, removed in 0.5.0:** `f4vr::PlayerNodes` and `getPlayerNodes()`. Use
  `getVRPlayerNodes()`, which returns CommonLibF4's `RE::VRPlayerNodes`. That struct is the same
  table, but its member names are camelCase and some are corrected (e.g. `HmdNode` → `hmdNode`,
  `SecondaryWandNode` → `secondaryWandNode`, `unk750` → `equippedWeaponNode`).

## 0.3.0 — 2026-07-02

**Configurable input, haptics, and in-game tuning.**

- **License**: relicensed from MIT to **GPL-3.0-or-later**, and `mod-template/` along with it.
- **CommonLibF4VR types throughout**: the framework no longer uses F4SEVR's `Forms.h`. All
  game types are CommonLibF4VR `RE::` types.
- **Input bindings**: `InputBinding` + `VRControllersManager::check()` evaluate a binding
  described as data (hand, tap / press / long-press / double-press, button or axis, cross-hand
  modifier), parsed from a readable INI string ([guide](input-binding.md)).
- **Input suppression**: `VRControllersSuppressor` hides chosen buttons and axes from the game
  and other mods while a gesture owns them.
- **Haptics**: `VRControllersHaptic` with a library of named haptic patterns.
- **Interaction and state helpers**: `WandActivationSphere` (a proximity zone for gestures) and
  `EquippedWeaponHandler` (tracks equipped-weapon changes).
- **Config**: `NiTransform` and hand-pose INI values, INI change subscriptions, and
  session-only value overrides.
- **Debugging**: `DebugAdjuster` for tuning transforms, hand poses and INI values in-game with the
  controllers, `DebugInventory` for adding items in bulk, `PerfMonitor` for profiling hot paths,
  and a guide to the `[Debug]` settings ([guide](debug-config.md)).
- **vrui**: elements sized from their real texture dimensions, a disabled state, and atlas
  pack/unpack tools in `nif-tools`.
- **Main loop**: early and late frame-update hooks, so a mod that moves the body (FRIK) can run
  before the others.
- **Build**: moved to Visual Studio 2026, with CI build and maintenance workflows.

## 0.2.0 — 2025-11-27

**A reusable library with a mod template.**

- Built on **CommonLibF4VR** instead of CommonLibF4.
- A CMake target that a mod adds with `add_subdirectory(...)`, rather than a DLL.
- **`ModBase`**: the shared plugin lifecycle (`onModLoaded` / `onGameLoaded` /
  `onGameSessionLoaded` / `onFrameUpdate`) on a common main-loop hook, with exception handling.
- **`mod-template/`**: a starting point for a new mod.
- **vrui**: a multi-state toggle button, and layout values read from config so a UI can be tuned
  without rebuilding.

**Upgrading**

- Everything moved under the `f4cf` namespace (`f4cf::f4vr`, `f4cf::vrcf`, `f4cf::vrui`), and
  the controller and UI code moved to `vrcf/` and `vrui/`.

## 0.1.0 — 2025-09-09

- First version: the common code built up across FRIK and other mods (`common`, `f4vr`,
  `f4sevr`, `ui`), collected into one repository.
