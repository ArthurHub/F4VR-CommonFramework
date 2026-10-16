# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

F4VR Common Framework is a **static library** (v0.5.0; see [`docs/CHANGELOG.md`](docs/CHANGELOG.md)) for building Fallout 4 VR F4SE plugins. It wraps [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR) and provides plugin lifecycle management, VR controller input, VR UI widgets, an overlay renderer (primitives, text, images, Dear ImGui), config hot-reload, and game-state utilities.

## Build

**Prerequisites:**
- `VCPKG_ROOT` environment variable must point to a vcpkg installation
- Visual Studio 2022 (v143) or 2026 (v145), x64 only
- CMake 4.2+
- Initialize submodules: `git submodule update --init --recursive`

**Generate solution:**
```
cmake --preset default
```
This creates a VS solution in `build/`. Open and build there. All project config changes go in `CMakeLists.txt`, not the VS project.

**Options:**
- `F4VR_BUILD_SHARED=ON` — build as DLL instead of static lib (default: OFF)
- `F4CF_WITH_IMGUI_UI=OFF` — drop the Dear ImGui UI layer `f4cf::imgui` (default: ON). Its headers are then not exposed either, so consumer code using it fails to **compile** instead of linking clean and drawing nothing. Everything else, the text font included, still builds.
- `F4CF_WITH_TRACY=OFF` — drop the `Tracy` build configuration (default: ON), and with it the download of Tracy's source at configure time. The configuration is Release plus the Tracy profiler client, so every perf site is also a Tracy zone; the other configurations never compile Tracy in ([perf README](src/perf/README.md#tracy)). The framework adds it to `CMAKE_CONFIGURATION_TYPES` with Release's flags, so a mod's `CMakeLists.txt` writes `$<CONFIG:Release,Tracy>` wherever it means Release.
- `F4CF_BUILD_TESTS` — build the unit tests in `tests/` (default: ON when the framework is the top-level project, OFF under a mod's `add_subdirectory`). A mod turns it on to build and run them with its own; it then needs `catch2` in its own `vcpkg.json`.
- `COMMON_LIB_F4VR_PATH` — override path to CommonLibF4VR (default: `external/CommonLibF4VR`)

**Tests:** `ctest --test-dir build -C Release` after a build. Catch2 unit tests in `tests/` for the game-independent logic, in two executables:
- `tests/*.cpp` test code that is plain std (today `f4cf::perf`, the TOML editor, the config's file, its values, its overrides and its migration, the vrui dev layout file and the imgui pointer logic). The test binary compiles the `.cpp` files it covers directly, without the PCH, F4SE or CommonLibF4. Keep pure logic in such files: it is the first choice, since the logic is then tested apart from everything else.
- `tests/linked/*.cpp` test code that needs the framework and CommonLibF4 to compile and link, and not the game to run (today `ConfigBase`, the config readers, the `[Debug]` section and the offsets files, `MatrixUtils`, `Quaternion` and `CommonUtils`, the input binding parser and the binding prompts, the haptic patterns, the sphere styles, and the vrui container layout and toggle groups). The test binary links the framework as a mod does, and `tests/linked/Setup.cpp` sets CommonLibF4's `REL::NO_GAME`, so every game address resolves into a range that cannot be used. The addresses the framework resolves at startup (`F4VROffsets.h`) are then harmless, and a test that calls the game fails with an access violation. What reads the game or OpenVR is out of their reach: a node, the left-handed setting (so a prompt for the primary or the off hand), a controller's state.

Add a new test file to `tests/CMakeLists.txt`, with the sources it covers for the first kind.

## Code Style

clang-format enforces style (`.clang-format`): LLVM-based, 180-column limit, 4-space indent, CRLF line endings, pointer-left (`T* p`), namespace indentation enabled, braces on new lines for classes/functions/namespaces.

Run formatter: `clang-format -i <file>` or format-on-save in your editor.

After cloning, run `pre-commit install` once to enforce clang-format on every commit (`.pre-commit-config.yaml`); the hook lives in `.git/hooks/` and is not version-controlled.

## Architecture

> Per-subsystem usage docs are canonical in [`src/README.md`](src/README.md) and each subfolder's
> README (code-adjacent, with examples). The sections below are a terse map for orientation — defer
> to those READMEs for the full API surface, and update them (not just this file) when behavior changes.

### Namespaces
- `f4cf::` — framework root (ModBase, Logger, ConfigBase)
- `f4cf::config` — the parts `ConfigBase` is made of (`src/config/`): the TOML file as the mod last loaded or saved it, typed values, session overrides, version migration and the INI import, the special values' text forms and their readers, the offsets JSON and its files, the `[Debug]` section
- `f4cf::debug` — immediate-mode in-world debug draw overlay (one layer on the shared overlay renderer)
- `f4cf::f4vr` — Fallout 4 VR game utilities (node/skeleton manipulation, animations, debug dumps)
- `F4SEVR` (in `src/f4sevr/`) — ported F4SE VR SDK: Papyrus VM interop + native-function registration (note: this folder is `namespace F4SEVR`, not `f4cf::f4sevr`)
- `f4cf::vrcf` — VR controller framework (OpenVR button/trigger input)
- `f4cf::vrui` — VR UI system (widget/button/container hierarchy, NIF widgets + primitive-drawn panels)
- `f4cf::render` — overlay rendering: lines/fills/images/text over the VR view, the shared `IVRCompositor::Submit` hook, scene-depth occlusion
- `f4cf::imgui` — Dear ImGui panels as world-space quads (compiled out by `F4CF_WITH_IMGUI_UI=OFF`)
- `f4cf::common` — math (quaternions, matrices) and shared utilities, among them `TomlEditor`: it sets values in a TOML file's text and keeps every other byte, which toml++'s own writer doesn't (it drops comments and blank lines and sorts keys). toml++ is compiled once into the framework (`common/TomlImpl.cpp`), and `TOML_HEADER_ONLY=0` is a PUBLIC define
- `f4cf::devbench` — each mod's devbench tool (MCP/REST actions an agent or script calls in the running game), registered by `ModBase`
- `f4cf::perf` — hot-path timing: `F4CF_PERF_FUNCTION()` / `F4CF_PERF_SCOPE("label")` sites on any thread, a lock-free histogram each, nested by the site each ran inside (self time = total - children), read against the frame context (frame interval, refresh-rate budget, VR compositor GPU time and reprojection, polled on the game thread); the framework times its own frame work, controller polls and overlay drawing (see the perf README); GPU sites (`perf::GpuTimer`) hold the GPU time of work the mod issues itself, from timestamps read back 1-3 frames late, which the Submit host records for every overlay layer; off until `perf::setEnabled(true)`, which the devbench tool does when it arms (devbench installed, a Tracy viewer connected, or `perf_reset`); in the `Tracy` build configuration every site is also a Tracy zone, with frame marks, the frame context as plots and devbench events as messages (`perf/Tracy.h`)

### Plugin Lifecycle (`src/ModBase.h`)
`ModBase` is the base class every mod derives from. Override these hooks:
1. `onModLoaded` — called at the F4SE load phase; register Papyrus functions and hooks here
2. `onGameLoaded` — called once after the game finishes loading
3. `onGameSessionLoaded` — called each time a save is loaded
4. `onFrameUpdate` — called every game frame (injected via main loop hook at offset `0xd8405e`)

A global singleton `f4cf::g_mod` holds the active mod instance.

### Config System (`src/config/`)
`ConfigBase` loads a TOML file (toml++) with file watching for hot-reload. Derive from it, override `loadConfigInternal(const toml::table&)`, and pass the file's path and the default's resource id to its constructor. A mod reads a value as `config[SECTION]["key"].value_or(default)`, which gives the default for a key that is missing or of another type (toml++ reads an integer as a float and as a bool); the readers of `ConfigReaders` (`getTransformValue`, `getInputBindingValue`, ...) read the values that are text.

- **Config base path:** `%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\{ModName}\{ModName}.toml`
- **Default config** is embedded in the DLL as RCDATA (resource ID 101) and written on first run
- **Saves** (`saveConfigValue(s)`) go through `common::TomlEditor`: only the values change in the file, its comments and layout stay, a value the file already holds is not written or logged, and a file that holds them all is not written. A value given as text is written in the type the file has for the key (`"1.250"` over a float is the float 1.25), and not written when it is not of it. A mod saves several values with one `saveConfigValues` (the keys of one section, or `config::ConfigEntry` values of any sections), in its `save()` override: each save reads and writes the whole file, so a `saveConfigValue` in a loop costs a file write per value. A section and a key are TOML dotted keys (`config::ConfigKey`), so `saveConfigValue("Main", "hand.x", 1.5f)` saves x in `[Main.hand]` or in Main's inline table `hand`; overrides, `getConfigValue`, the readers and devbench take them the same way, and a name with a dot in it is in quotes (`"\"my.key\""`)
- **A file that is not TOML** (a person in the middle of editing it, a string without its quotes) is logged with the line and what is wrong, and never written over: the values loaded before stay, and before any were the embedded default's apply. Its other file-watch events are not parsed again until it changes, and a save refuses it
- **Config version migration:** the `[Debug] iVersion` key; when the shipped config's value is higher than the user's, `ConfigBase` writes the shipped config with the user's values for the keys it still has with a value of the same type (`config::migrateValues` in `src/config/ConfigMigration.h`, plain std and toml++, and unit tested), then calls `updateConfigToLatestVersionCustom` for what changed type or name. The old file is kept as `{name}_backup_v{N}.toml`
- **INI import:** a mod that moved its config from INI to TOML keeps its players' values. On a load with no `{name}.toml` and a `{name}.ini` beside it, the INI is written as `{name}.toml` at version 0, and the version migration above takes it to the default's version, `updateConfigToLatestVersionCustom(0, ...)` being where a mod moves what its TOML config changed from the INI. In the version 0 file a key the default has is of the default's type, read by SimpleIni's getter of that type so it is what the mod read from the INI ("yes" a bool, "0x10" an integer), with names matched with no regard to case as SimpleIni matched them; a transform (a table the default has as `{ pos = [..], rot = [..], scale = .. }`) is the table of the INI's `x,y,z;heading,roll,attitude;scale`; any other value is a string (`config::iniToToml`, and `config::importIniFile` for the files, in `src/config/ConfigMigration.h`, plain std and unit tested). The INI is kept as `{name}_backup_v{N}.ini`, `N` the INI's own version, and the version 0 file as the update's `{name}_backup_v0.toml`
- File watcher triggers `loadConfigInternal()` automatically on disk change — no restart needed
- `switchConfigFile(path)` moves the config to another file at runtime (FRIK's Fallout London VR config): it stops the watch, loads the new file as `load()` does (created, imported from its INI or updated as needed) and watches it. Its values are not merged over the file before, and the session overrides stay
- A load runs on the watcher's thread for a change on disk and on the caller's for a session override (`setConfigOverride`), one at a time: `_loadMutex` is held from the read of the file to the end of `loadConfigInternal()`
- The mod's own save is not loaded back: the watcher loads the file only when its content is not what the mod last loaded or saved (`config::ConfigFile` in `src/config/ConfigFile.h`, plain std and unit tested). A save has to go through `ConfigBase` (`saveConfigValue(s)`) to be taken as the mod's own. A read that loads no values (`getConfigValue`, the DebugAdjuster's field mode) parses that kept content and reads no disk
- Session overrides (`setConfigOverride(s)`, `clearConfigOverride(s)`, several with one reload): a set loads that kept content of the file and reads no disk, so it can be done many times a second for a preview, and a clear reads the file. Only a key that its owner did not override before is logged at info and is a `config.override` event. A set takes each value in the type the file has for its key and keeps it as it applies, which `getConfigValue` and `getConfigOverrides` give back (`no` over a bool is `false`, see `config::appliedText`); a value not of the type, a key that is not a TOML key, or one under a value that is not a table is refused with the rest of its set, and the call's `std::expected` error says why. Overrides are kept by `ConfigKey::canonical()`, so two spellings of one key's path are one override
- Every override has an owner, the first argument of each call, and an owner clears only its own (`config::Overrides` in `src/config/Overrides.h`, which holds them under its own lock and puts them into a loaded config in the type the file has for the key; plain std and toml++, and unit tested). The devbench tool's is `devbench`, and a mod's API passes its caller's name. With two owners on one key the one set last applies, and the other again when it is cleared
- To react to a load, a mod subscribes (`subscribeForConfigChangedEvent`). A load only marks that it ran, and `ModBase` calls the subscribers on the game thread at the start of the next frame, once for all the loads since the frame before: a change on disk, an override, `reload()`, and the load when a game session starts

Standard `[Debug]` config keys provided by the base class:
```toml
iLogLevel = 2                 # 0=trace, 1=debug, 2=info
sLogPattern = "%H:%M:%S.%e %l: %v"
fFlowFlag1 = 0.0              # also 2 and 3: runtime feature toggles, read via g_config.debug.flowFlag1
sFlowText1 = ""               # also 2: debug text fields
aDumpDataOnce = []            # Names of dumps to log once, each removed from the file when it runs: ui_tree, skelly, fp_skelly, geometry, world, all_nodes, perf (log the perf table), perf_reset (log it and start a new window; the first one switches recording on)
aAddItemsOnce = []            # Inventory requests run once, the array cleared before it is read: { op, category, name?, keyword?, slot?, class? } each, see f4vr::DebugInventory::parseRequest; any invalid request and none runs
```
An `a` prefix is an array key, read with `ConfigReaders::getStringListValue` for a list of strings. A one-shot array is cleared by saving its new TOML text (`saveConfigValue(section, key, "[]")`), which `typedLike` reads as the array it is. A `t` key is a transform, `{ pos = [x, y, z], rot = [heading, roll, attitude], scale = s }` (rotation in degrees, a missing key no move, no rotation or a scale of 1), read with `getTransformValue`; a `c` key is a color, `{ rgb = [r, g, b], alpha = a }` in 0..255 (a missing key keeps the default's channels), read with `getColorValue`.

### Logging (`src/Logger.h`)
spdlog-based, in the `f4cf::logger` namespace (PCH does `using namespace f4cf;`, so call them unqualified as `logger::info(...)`). Levels: `logger::trace` / `debug` / `info` / `warn` / `error` / `critical`; plus `logger::sample(ms, fmt, ...)` for rate-limited logs and `logger::infoRaw` for unformatted lines. Guard expensive work with `logger::isDebugEnabled()` etc. Logger is initialized during `onF4SEPluginLoad`.

### VR UI (`src/vrui/`)
Two families of element share one scene graph, one layout and one press model:
- **NIF widgets** — `UIElement` → `UIWidget` → `UIButton`/`UIToggleButton`/`UIMultiStateToggleButton` (all three extend `UIWidget` directly): a `.nif` mesh per button, built with `nif-tools/vrui_atlas.py`.
- **Panels** — `UIElement` → `UIPanel` → `UITextPanel`/`UIImagePanel`/`UIButtonPanel` → `UIToggleButtonPanel`/`UIMultiStateToggleButtonPanel<State>`: no mesh, composed at runtime from text and textures and drawn by `f4cf::render`. The look comes from a `UIPanelStyle` (`F4VR_PANEL_STYLE` / `F4VR_BUTTON_STYLE`), and they can size themselves to their content (`UIPanelSizing`).

The two mix freely: `UIPressable` carries the disabled state both kinds share and `UIToggleable` the on/off state, so a `UIToggleGroupContainer` can hold a NIF toggle and a panel toggle in the same group. `UIContainer`/`UIToggleGroupContainer` lay children out; `UIManager` is the scene-graph singleton (global `g_uiManager`). Attach elements via `g_uiManager`; `ModBase` runs `g_uiManager->onFrameUpdate()` each frame after the mod's `onFrameUpdate()` to drive input and rendering, unless the mod called it itself in that frame to have it at a certain point (FRIK does). The manager finds the finger that presses and points that hand through FRIK's API (`UIHandPointing.cpp`), with nothing for the mod to implement. `BindingPrompt` turns an `InputBinding` into a text span carrying the controller's own icon, so a prompt says what is actually bound. Full hierarchy, sizing, assets, and examples: [`src/vrui/README.md`](src/vrui/README.md).

### VR Controller Framework (`src/vrcf/`)
Wraps OpenVR (vendored headers in `external/openvr/` as fallback when not found via vcpkg). Three globals, all driven each frame by `ModBase`: `VRControllers` (`VRControllersManager` — debounced button/trigger/thumbstick reads, heading), `VRControllersSuppress` (`VRControllersSuppressor` — owner-keyed input suppression that hides buttons/axes from the game while our own DLL still reads raw input), and `VRHaptics` (`VRControllersHaptic` — haptic feedback: sustained buzzes via per-frame re-pulse, named `HapticPattern` library, custom keyframed sequences). Full API, button map, and example: [`src/vrcf/README.md`](src/vrcf/README.md).

Suppression constraints that cause bugs if missed:
- Call `suppress`/`release`/`reset` from the **main thread** only — the vtable hook (slots 34/35) runs on the OpenVR thread and reads just an atomic mask.
- **Never** call F4SE/CommonLibF4VR from the hook.
- Owner-keyed: each call takes a `std::string_view key`; a key only undoes its own suppression, and the effective mask is the union of all owners.

Design deep-dive: `knowledge-base/commonframework_vr_input_suppression.md` in the reference library.

### F4VR Utilities (`src/f4vr/`)
Game-state helpers: node search/visibility/transform updates, player/weapon/menu state, `getVRPlayerNodes()` (CommonLibF4's `RE::VRPlayerNodes`, the VR reference nodes at `PlayerCharacter + 0x6E0`), `SkellyBones` (100+ bone names + finger poses), Scaleform/HUD (`ScaleformUtils`), `GameMenusHandler`, `F4VRThumbstickControls`, `PlayerRotation` (snap/smooth turning that obeys the player's VR comfort settings and works where vanilla turning is taken away, rotating the VR room transform rather than the actor — design: [`docs/tech/vr-player-rotation.md`](docs/tech/vr-player-rotation.md)), and `F4VROffsets` (all RVAs). Full file list and snippets: [`src/f4vr/README.md`](src/f4vr/README.md). RVA authority + full PlayerNodes layout: `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` in the reference library.

### Debug Draw Overlay (`src/debug/`)
`f4cf::debug::DebugDraw` (facade `debug::dd()`) — immediate-mode in-world debug drawing: wire
primitives (line/point/arrow/box/sphere/capsule/cone/axes/grid/polyline/mesh), HUD text,
world-anchored labels, and a `watch()` value table, drawn on top of the VR view. It is a producer:
it fills a `render::PrimitiveDraw` and publishes it to its own `render::PrimitiveDrawRenderer`
layer at `DRAW_ORDER_DEBUG` — on top of everything and deliberately not occluded, since a debug
shape behind a wall is exactly the one you need to see. Call each frame to keep a shape visible
(auto-clears per frame); `sec > 0` persists a one-shot for that long. Zero cost and no hooks until
the first draw call (lazy install, driven by `ModBase` around `onFrameUpdate`).
`[Debug]` config keys: `bDebugDrawEnabled`, `aDebugDrawDisabledChannels`, `sDebugDrawToggleBinding`.
Usage: [`src/debug/README.md`](src/debug/README.md); design: [`docs/tech/debug-draw-overlay.md`](docs/tech/debug-draw-overlay.md).

### Overlay Rendering (`src/render/`)
Everything the framework draws **over** the VR view rather than into the scene graph, so the hard
parts live here once: one shared `IVRCompositor::Submit` vtable hook (index 5) with a painter-ordered
draw-callback table, the engine's own per-eye view-projection matrices (so world coordinates land
where the game drew the world — never query OpenVR from the render thread), the pipeline
save/restore around every callback, a signed-distance-field text font, engine-loaded textures, and
the scene-depth capture that lets a layer be hidden behind world geometry.

A producer fills a `render::PrimitiveDraw` (lines, fill triangles, image quads, text) on the **game
thread** and `publish()`es it to its own `PrimitiveDrawRenderer` layer, which replays it on the
render thread. `vrui::UIPanel`, `f4cf::imgui`, `f4cf::debug` and the activation-sphere icons are all
layers on it.

Rules that cause bugs if missed:
- A draw callback runs on the **render thread**, meaning Submit's thread: never touch nodes/forms/config there, never call
  into OpenVR, and restore anything you bind beyond the shared `ScopedPipelineState`. In FO4VR Submit is called on the game
  thread during play, right after the mods' frames, and from the loading screen's own thread while one is up; the hook
  logs each thread it is called on once. The game → render handoff is what keeps a layer correct on both.
- Draw order is **declared** (`DRAW_ORDER_HINTS` 50 / `PANELS` 100 / `POINTERS` 150 / `DEFAULT` 500 / `DEBUG` 900),
  not inherited from registration order — registration is lazy.
- Callbacks are **never unregistered** and the vtable patch is never removed, so a layer must outlive
  the process. Hold it as a static.
- `Texture::load()` / `Texture::view()` are **game thread only**; the `TextureView` in a published
  frame is what keeps the GPU texture alive across the boundary.
- `Texture::fromPixels()` makes a texture from RGBA pixels in memory, for an image that is no file
  the engine can load. It runs on any thread: the GPU texture is created by the first `view()`.

Full API, budgets, text placements and the occlusion story: [`src/render/README.md`](src/render/README.md).
Scene-depth design + the `sSceneDepthStrategy` / `bSceneDepthDiagnostics` keys:
[`docs/tech/scene-depth-occlusion.md`](docs/tech/scene-depth-occlusion.md).

### Dear ImGui UI (`src/imgui/`)
Dear ImGui content on world-space quads. `imgui::Canvas` is one ImGui window placed by a provider the
mod writes; `imgui::UIImGuiPanel` wraps one as a `vrui::UIElement`, so it takes part in vrui layout,
takes a `vrui::UIPanelStyle` and is sized in vrui units. Every canvas is packed into one shared atlas
and composited as one quad, so N canvases cost one ImGui frame and one draw call.

- Content and placement callbacks run on the **game thread**; the draw data is cloned across to the
  render thread, since ImGui recycles its own buffers on the next `NewFrame`.
- **ImGui's font texture is answered on the game thread** (`ImGuiTextures.cpp`): created there through
  the device when ImGui asks, and the glyphs ImGui adds for a new size copied for the render thread to
  write before it draws. Text of any size is rasterized at that size, at the supersample factor
  (`io.DisplayFramebufferScale`); content pushes a size with `ImGui::PushFont(nullptr, size)`.
- **Display only until `setInteractive(true)`** — then a wand's ray is ImGui's mouse on the canvas and
  its trigger the left button (`imgui::pointer()` in `ImGuiPointer.h`: `state`, `setHands`,
  `setStyle`). One hand owns the pointer at a time, and while it is on a canvas its ray and a mark
  on the canvas are drawn over the panels, with the other hand's ray fainter beside it.
  `PointerStyle` has where the ray is on the hand and how both look, with its defaults as the
  default pointer. vrui's finger-collision press handling does not reach ImGui widgets: buttons to
  press by touch go beside the panel, in vrui.
- **The wand that points is hidden from the game**: every button and axis of a hand whose ray is on
  an interactive canvas, through `VRControllersSuppress` under the owner `ImGuiPointer`. A press
  that began on a canvas keeps its wand hidden until the trigger is released, wherever the ray is
  and whether or not the canvas is still shown. The mod's own `VRControllers` reads still see it.
  The thumbstick of the hand that owns the pointer is ImGui's mouse wheel.
- **One large panel is made dedicated** (`setDedicated(true)`): it has a texture of its own size in
  place of a part of the atlas, so it keeps its pixels past the atlas's 1024, and while it is shown
  it is ImGui's display, so the combos, popups and dialogs of its content open inside it. ImGui has
  one display: while a dedicated panel is shown, the mod's other canvases are not drawn.
- **A wide panel is curved** (`setCurveRadius`): bent toward the player around a cylinder, so its
  sides read as its middle does. It is drawn as flat pieces side by side, and the pointer meets it
  on the cylinder, both from `CurvedQuad` in `ImGuiPointerLogic.h`. vrui still lays it out flat.
- **A panel in the world is moved by the player**: vrui does it, for every root attached with
  `attachPresetToWorldAtHMD`, once the mod sets `g_uiManager->setWorldMoveButton(...)`. While that
  button is held on the hand that owns the pointer, from a press on the panel, the root follows the
  hand, kept level and smoothed against the hand's shake (`setWorldMoveSmoothing`), and that wand
  stays hidden from the game. The panel only reports the ray on it
  (`UIFrameUpdateContext::markPointedAt`). Recentering the root puts it back.
- **An image in the content is `imgui::image` / `imgui::addImage`** (`ImGuiImage.h`), with a
  `render::Texture`, and never `ImGui::Image` with a raw view: the published frame then holds the
  texture's view until it has been drawn, so the mod can drop the texture at any time.
- **Bold and italic text is `imgui::canvasFont(FontStyle)`** (`ImGuiFonts.h`): the faces the canvas
  font is loaded with, to push or to pass to `ImDrawList::AddText`.
- Content can only be measured by drawing it, which happens after vrui lays the frame out, so a panel
  sized to its content is laid out at **last frame's** size.
- `setFontSizePixels` / `setSupersample` are process-wide and read when the first canvas draws.
- Compiled out by `F4CF_WITH_IMGUI_UI=OFF`; nothing else in the framework depends on it.

Full API and the draw path: [`src/imgui/README.md`](src/imgui/README.md).

### devbench Tool (`src/devbench/`)
[devbench](https://github.com/ArthurHub/devbench) is a separate F4SE plugin serving MCP + REST tools
from inside the game. `ModBase` registers one tool per mod after `onGameLoaded`, named after
`Settings::name` in lowercase, with generic actions (`health`, `state`, `config`, `set`, `clear`,
`overrides`, `perf`); a mod adds its own with `devbench::addAction`, its state with
`devbench::setStateProvider`, and opens the description with `devbench::setToolDescription`.

- Devbench calls the tool on its **listener thread**. Actions run on the game thread by default:
  queued and run by `ModBase` right before `onFrameUpdate`, with a 2s timeout. `RunOn::Listener`
  actions (`health`, `state`, `perf`) must not touch game or mod state.
- `state` answers from a snapshot `ModBase` publishes after every `onFrameUpdate` once the tool is
  armed, which happens as soon as devbench is installed, a Tracy viewer connects or `perf_reset` asks
  for perf; the provider's struct must hold **plain values only**, since a snapshot outlives its frame.
- `devbench::emit(topic)` publishes `<tool>.<topic>` into a 256-event ring every mod shares: emit on
  changes, never per frame. The framework emits `sessionLoaded`, `config.reloaded`, `config.override`
  (any source, including other mods) and `input.suppression` (per owner, only on a real mask change).
- Tool names are one flat namespace in devbench, and a duplicate **silently replaces** the earlier
  tool; that is why the name comes from `Settings::name` and never from a generic word.
- Only the devbench C-ABI slots every version has are called; a later slot needs a
  `GetBuildNumber()` check. `DevBenchAPI.h/.cpp` are vendored verbatim: never edit or reformat them.

Full API: [`src/devbench/README.md`](src/devbench/README.md).

### FRIK Inter-Mod Integration
Mods that want a button in FRIK's config menu:
```cpp
// In onGameLoaded():
FRIKApi::registerOpenModSettingButtonToMainConfig(data);

// Listen for F4SE message to open your UI:
// Sender: "F4VRBody", type: 15
```
`FRIKApi::setHandPose(tag, hand, pose)` / `clearHandPose(tag, hand)` control finger positions. Tag is a string priority key — higher strings override lower ones.

### ModBase Settings
The `Settings` struct passed to the `ModBase` constructor controls:
- Trampoline size (default 256)
- `earlyFrameUpdate` / `lateFrameUpdate` flags — late means "run before all others" (used by FRIK for body tracking priority)
- Update frequency (calls per second for `onFrameUpdate`)
- `preloadRendering` (default off) — build the overlay rendering before the first draw instead of on it, which otherwise stalls that frame for ~0.1s (mostly the font atlas): the font on a background thread from plugin load (`render::preloadTextFont()`), the shared D3D pipeline + Submit hook host at game loaded (`render::PrimitiveDrawRenderer::preload()`). Turn it on in a mod that draws vrui panels, activation-sphere icons or other `f4cf::render` overlays; the mod sets it in its constructor (`_settings.preloadRendering = true;`)
- `devbenchTool` (default on) — register the mod's devbench tool when devbench is installed; set it to `false` in the constructor to opt out

### mod-template
The `mod-template/` directory is a complete starting point for new mods. See [Creating a New Mod](#creating-a-new-mod) below for the full process.

## Creating a New Mod

All new mods start from `mod-template/`. The template produces a DLL (F4SE plugin) linked against this framework as a static lib.

### 1. Copy and rename

Copy the entire `mod-template/` directory into the new mod's repo. Then replace every occurrence of `MyMod` (case-sensitive) and `My Mod` (friendly name) throughout all files:

| File | What to change |
|------|----------------|
| `CMakeLists.txt` | `NAME`, `FRIENDLY_NAME`, `VERSION` at the top |
| `src/MyMod.h` / `src/MyMod.cpp` | Rename files; update class name and `#include` |
| `src/Config.h` / `src/Config.cpp` | Update class name, config section name |
| `data/config/MyMod.toml` | Rename file; update `[MyMod]` section header |
| `cmake/Version.h.in` | (no change needed — driven by CMakeLists.txt) |
| `README.md` | Update links, description |
| `CLAUDE.md` | The mod's own Claude Code guidance; rename the tokens (`MyMod`, `My Mod`, `my_mod`, `g_myMod`) and fill in its TODOs |

The template also carries the repo automation, used as it came: `.pre-commit-config.yaml` (clang-format over `src/` plus basic checks; `data/` and `external/` excluded), `.github/workflows/maintenance.yml` (pre-commit on GitHub with auto-fix commits) and `build.yml` (Release build), and `.vscode/settings.json` (no hook bypass from the commit UI). Run `pre-commit install` once in the new repo.

Also rename `CMakeUserPresets.json.template` → `CMakeUserPresets.json` (git-ignored) and fill in:
- `POST_BUILD_COPY_PLUGIN`: `true` to auto-copy DLL/PDB after build
- `COPY_PLUGIN_BASE_PATH`: path(s) to MO2 mod folder or `Fallout4VR\Data` (semicolon-separated)
- `COPY_PLUGIN_CONFIGURATIONS`: build configurations to copy on — `all` (default) or a `;` list like `Release;Tracy` / `Debug;RelWithDebInfo`
- `F4VR_COMMON_FRAMEWORK_PATH`: only if not using a submodule (overrides the default `external/F4VR-CommonFramework`)

Rename `src/PCH.h.template` → `src/PCH.h`.

### 2. Add the framework as a submodule

```
mkdir external
git submodule add https://github.com/ArthurHub/F4VR-CommonFramework.git external/F4VR-CommonFramework
git submodule update --init --recursive
```

Or point `F4VR_COMMON_FRAMEWORK_PATH` in CMakeUserPresets.json at an existing checkout.

### 3. Generate and build

```
cmake --preset default        # or --preset vs2026
```

Opens a VS solution in `build/`. Debug, Release and Tracy (Release plus the Tracy profiler client, the template's `tracy` build preset) configurations are available. Building the `package_mod` target (`cmake --build build --config Release --target package_mod`) stages everything (DLL, PDB, `data/mod/` contents) and produces a versioned `.7z` at `build/package/`; a normal build does not package.

### 4. Source file responsibilities

| File | Purpose |
|------|---------|
| `src/MyMod.h` | Mod class (extends `ModBase`); declares lifecycle overrides; holds global `g_myMod` singleton |
| `src/MyMod.cpp` | `F4SEPlugin_Query` / `F4SEPlugin_Load` entry points; lifecycle method bodies |
| `src/Config.h` | `Config` class (extends `ConfigBase`); declares config-backed member variables |
| `src/Config.cpp` | `loadConfigInternal()` — reads each key from the `toml::table`; holds `g_config` singleton |
| `src/Resources.h` | Resource IDs (e.g., `IDR_CONFIG = 101`) for files embedded in the DLL |
| `src/PCH.h` | Precompiled header — includes F4SE, RE/Fallout.h, REL/Relocation.h, Logger.h, Version.h |
| `cmake/Version.h.in` | Template → auto-generated `Version.h` with `Version::PROJECT`, `Version::NAME`, semver consts |
| `cmake/version.rc.in` | Template → DLL metadata resource (file version, product name) |
| `cmake/resources.rc.in` | Template → embeds `MyMod.toml` as binary resource ID 101 inside the DLL |
| `cmake/package.cmake` | Run by the `package_mod` target: stages files → zips to versioned `.7z` |
| `data/config/MyMod.toml` | Shipped config (also embedded in DLL as default). Sections: `[MyMod]` for settings, `[Debug]` for log level/pattern/debug flags |

### 5. Lifecycle hooks (override in MyMod.cpp)

```cpp
void onModLoaded()          // F4SE load phase — register Papyrus functions, hooks
void onGameLoaded()         // fires once when the game world finishes loading
void onGameSessionLoaded()  // fires on new game + each save load
void onFrameUpdate()        // fires every frame while PlayerCharacter is initialized
```

`onFrameUpdate` template already guards on `RE::PlayerCharacter::GetSingleton()` and its loaded data flag.

### 6. Adding config values

1. Add a member to `Config.h`: `float myValue = 0.0f;`
2. Read it in `Config.cpp` → `loadConfigInternal()`:
   ```cpp
   myValue = config[DEFAULT_SECTION]["fMyValue"].value_or(0.0f);
   ```
3. Add the key to `data/config/MyMod.toml` under `[MyMod]`: `fMyValue = 1.0` (a float with its decimal point, a string in quotes, a Windows path in single quotes).

The file watcher in `ConfigBase` calls `loadConfigInternal()` automatically when the file changes on disk — no restart needed.

### 7. Framework assets (`f4cf\`)

`data/mod/` is the mod's Data folder. Everything the framework ships and loads by path sits in an `f4cf` folder inside the mod's own folders, so what came from the framework stays apart from what the mod added:

- `Meshes\MyMod\f4cf\activation-sphere.nif`, `Textures\MyMod\f4cf\activation-sphere.dds` / `debug-sphere.dds` — the sphere visuals (`f4vr::SphereStyle`)
- `Textures\MyMod\f4cf\bindings\` — the controller icons binding prompts draw (`vrui::BINDING_ICONS_DIR`)
- `Textures\MyMod\f4cf\vrui\` — shared VR UI icons (save, reset, exit, config, wiki, debug spheres) for `UIButtonPanel::setImage("f4cf\\vrui\\save.DDS")`

Ship `f4cf\` as the template has it and refresh it from the template when updating the framework. The mod's own assets go beside it, e.g. its own icons in `Textures\MyMod\vrui\`.

Text is not part of `f4cf\`: it is drawn in the framework's embedded font unless the mod ships its
own TrueType/OpenType file at `Data\Interface\<ModName>\<ModName>.ttf` — one file serves both the
vrui panels and the ImGui canvases, and it is read once at first use, so replacing it takes a restart.
The canvases' bold and italic faces are `<ModName>-Bold.ttf` and `<ModName>-Italic.ttf` beside it, or
else the embedded Roboto ones.

## Modding Reference Library

A curated reference collection lives at `C:\Stuff\GitHub\Mine\Modding-Reference\F4VR\`. Consult it when writing mod code, looking up RVAs/struct offsets, or researching implementation techniques.

```
F4VR/
├── github-repos/{gold,silver,bronze}/   # 57 cloned repos
├── manual-repos/{gold,silver}/          # 7 closed-source repos (f4sevr SDK, mith077 tools)
├── Analysis/{gold,silver,bronze}/       # 133 docs: paired MOD_OVERVIEW + RE_REFERENCE per mod
└── knowledge-base/                      # 5 deep-dive technical guides
```

**Key files to consult first:**

| File | Use for |
|------|---------|
| `Analysis/gold/CommonLibF4VR_API_REFERENCE.md` | Primary API lookup |
| `Analysis/gold/f4sevr_0_6_21_RE_REFERENCE.md` | Authoritative VR 1.2.72 RVAs and struct offsets |
| `Analysis/gold/FRIK_RE_REFERENCE.md` | PlayerNodes layout, skeleton bone offsets |
| `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` | This framework's own RVAs |
| `knowledge-base/scope_zoom_techniques.md` | 4 scope zoom approaches with full code |
| `knowledge-base/item_in_hand_techniques.md` | Attaching items to hand bones |
| `knowledge-base/commonlibf4vr_f4sevr_gap_analysis.md` | Known CommonLibF4VR bugs and missing APIs |

**RVA authority:** f4sevr 0.6.21 headers → CommonLibF4VR AddressLib → individual `_RE_REFERENCE.md` files.

**Maintaining the reference library** — see `C:\Stuff\GitHub\Mine\Modding-Reference\F4VR\CLAUDE.md` for the full conventions. Short version: every analyzed mod needs two files in `Analysis/{tier}/`:
- `{ModName}_MOD_OVERVIEW.md` — what it does, Papyrus API, all config settings, dependencies
- `{ModName}_RE_REFERENCE.md` — every `REL::ID`/`REL::Offset`/`RelocAddr`, struct layouts with byte offsets, hook targets, vtable indices, FormIDs

**FRIK's repo name** in the reference library is `Fallout-4-VR-Body` (github-repos/gold/), analysis files are `FRIK_MOD_OVERVIEW.md` / `FRIK_RE_REFERENCE.md`.

## Key Files

| File | Purpose |
|------|---------|
| `src/PCH.h` | Precompiled header — included implicitly in all TUs |
| `src/ModBase.h/.cpp` | Plugin base class and F4SE registration |
| `src/Logger.h` | Logging macros |
| `src/config/ConfigBase.h/.cpp` | TOML config with hot-reload |
| `src/config/ConfigFile.*`, `ConfigValue.*`, `Overrides.*`, `ReloadSubscribers.*`, `ConfigMigration.*`, `ValueParsers.*`, `OffsetsJson.h` | The parts of the config that are apart from the game, so unit tested: the TOML file as the mod last loaded or saved it, a typed value and the type a value given as text takes, the session overrides with their owners, the subscribers to a reload, the move of a user's values into the config of a newer version, the forms of a transform (a TOML table, and the INI's text of it for a migration), a hand pose, a color and a list of names, and the offsets JSON |
| `src/config/ConfigReaders.*`, `OffsetsFiles.*` | The readers of a transform, a hand pose, a binding, a color and an activation sphere's section from a loaded config (`getTransformValue`, `loadWandActivationConfig`, ...), and the loading and saving of the offsets JSON files (`loadOffsetsFromFilesystem`, `saveOffsetsToJsonFile`, ...). `ConfigBase` derives from both, so a mod's config calls them by name |
| `src/config/DebugSection.*` | The values of the `[Debug]` config section, which a mod reads as `config->debug`, and `DebugAdjustTarget` |
| `src/f4vr/` | Game node/skeleton/animation utilities |
| `src/f4sevr/` | Papyrus native function registration helpers |
| `src/vrcf/VRControllersManager.h` | Controller button/trigger state |
| `src/vrui/` | VR widget system (NIF widgets + primitive-drawn panels) |
| `src/render/` | Shared OpenVR Submit hook, primitive/text/image renderers, and the scene-depth capture that occludes overlays behind the world (design + the `sSceneDepthStrategy` key: [`docs/tech/scene-depth-occlusion.md`](docs/tech/scene-depth-occlusion.md)) |
| `src/imgui/` | Dear ImGui canvases + `UIImGuiPanel`; compiled out by `F4CF_WITH_IMGUI_UI=OFF` |
| `CMakePresets.json` | VS2022/VS2026 preset definitions |
| `vcpkg.json` | Dependency manifest (spdlog, xbyak, nlohmann-json, simpleini, tomlplusplus, filewatch, cpptrace, catch2 for the tests, imgui with `dx11-binding`, at 1.92.9 from its `builtin-baseline`, which a mod's manifest repeats) |
