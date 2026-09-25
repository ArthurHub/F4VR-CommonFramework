# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

F4VR Common Framework is a **static library** (v0.4.0; see [`docs/CHANGELOG.md`](docs/CHANGELOG.md)) for building Fallout 4 VR F4SE plugins. It wraps [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR) and provides plugin lifecycle management, VR controller input, VR UI widgets, an overlay renderer (primitives, text, images, Dear ImGui), config hot-reload, and game-state utilities.

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
- `COMMON_LIB_F4VR_PATH` — override path to CommonLibF4VR (default: `external/CommonLibF4VR`)

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
- `f4cf::debug` — immediate-mode in-world debug draw overlay (one layer on the shared overlay renderer)
- `f4cf::f4vr` — Fallout 4 VR game utilities (node/skeleton manipulation, animations, debug dumps)
- `F4SEVR` (in `src/f4sevr/`) — ported F4SE VR SDK: Papyrus VM interop + native-function registration (note: this folder is `namespace F4SEVR`, not `f4cf::f4sevr`)
- `f4cf::vrcf` — VR controller framework (OpenVR button/trigger input)
- `f4cf::vrui` — VR UI system (widget/button/container hierarchy, NIF widgets + primitive-drawn panels)
- `f4cf::render` — overlay rendering: lines/fills/images/text over the VR view, the shared `IVRCompositor::Submit` hook, scene-depth occlusion
- `f4cf::imgui` — Dear ImGui panels as world-space quads (compiled out by `F4CF_WITH_IMGUI_UI=OFF`)
- `f4cf::common` — math (quaternions, matrices) and shared utilities

### Plugin Lifecycle (`src/ModBase.h`)
`ModBase` is the base class every mod derives from. Override these hooks:
1. `onF4SEQuery` — called at F4SE query phase
2. `onF4SELoad` — called at F4SE load phase; register Papyrus functions here
3. `onGameLoaded` — called once after the game finishes loading
4. `onGameSessionLoaded` — called each time a save is loaded
5. `onFrameUpdate` — called every game frame (injected via main loop hook at offset `0xd8405e`)

A global singleton `f4cf::g_mod` holds the active mod instance.

### Config System (`src/ConfigBase.h`)
`ConfigBase` wraps simpleini with file watching for hot-reload. Derive from it, override `loadIniConfigInternal()`, and call `setupConfig(path)`.

- **INI base path:** `%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\{ModName}\`
- **Override file:** `{ModName}_Custom.ini` is merged on top of the main INI automatically
- **Default INI** is embedded in the DLL as RCDATA resource ID 101 and extracted on first run
- **Config version migration:** use `[Version] iVersion` key; compare in `loadIniConfigInternal()` to handle upgrades
- File watcher triggers `loadIniConfigInternal()` automatically on disk change — no restart needed

Standard `[Debug]` INI keys provided by the base class:
```ini
iLogLevel = 2              # 0=trace, 1=debug, 2=info
sLogPattern = %H:%M:%S.%e %l: %v
fFlowFlag1/2/3 = 0   # Runtime feature toggles, read via g_config.debug.flowFlag1
sFlowText1/2 =        # Debug text fields
sDumpDataOnceNames =  # Comma-separated: ui_tree, skelly, fp_skelly, geometry, world, all_nodes
sAddItemsOnceNames =  # Bulk-add items once: first token = operation (get=obtainable/get-all=everything/print=dry-run of get/print-all=dry-run of get-all), then "category[:filter]" tokens (weapons/throwables/ammo/armor/aid/misc); filter is '|'-sep key=value (name=/keyword= any; armor slot=/class=light|heavy|none; weapon class=melee|gun|unarmed)
```

### Logging (`src/Logger.h`)
spdlog-based, in the `f4cf::logger` namespace (PCH does `using namespace f4cf;`, so call them unqualified as `logger::info(...)`). Levels: `logger::trace` / `debug` / `info` / `warn` / `error` / `critical`; plus `logger::sample(ms, fmt, ...)` for rate-limited logs and `logger::infoRaw` for unformatted lines. Guard expensive work with `logger::isDebugEnabled()` etc. Logger is initialized during `onF4SEPluginLoad`.

### VR UI (`src/vrui/`)
Two families of element share one scene graph, one layout and one press model:
- **NIF widgets** — `UIElement` → `UIWidget` → `UIButton`/`UIToggleButton`/`UIMultiStateToggleButton` (all three extend `UIWidget` directly): a `.nif` mesh per button, built with `nif-tools/vrui_atlas.py`.
- **Panels** — `UIElement` → `UIPanel` → `UITextPanel`/`UIImagePanel`/`UIButtonPanel` → `UIToggleButtonPanel`/`UIMultiStateToggleButtonPanel<State>`: no mesh, composed at runtime from text and textures and drawn by `f4cf::render`. The look comes from a `UIPanelStyle` (`F4VR_PANEL_STYLE` / `F4VR_BUTTON_STYLE`), and they can size themselves to their content (`UIPanelSizing`).

The two mix freely: `UIPressable` carries the disabled state both kinds share and `UIToggleable` the on/off state, so a `UIToggleGroupContainer` can hold a NIF toggle and a panel toggle in the same group. `UIContainer`/`UIToggleGroupContainer` lay children out; `UIManager` is the scene-graph singleton (global `g_uiManager`). Implement a `UIModAdapter`, attach elements via `g_uiManager`, and call `g_uiManager->onFrameUpdate(adapter)` each frame to drive input and rendering. `BindingPrompt` turns an `InputBinding` into a text span carrying the controller's own icon, so a prompt says what is actually bound. Full hierarchy, sizing, assets, and examples: [`src/vrui/README.md`](src/vrui/README.md).

### VR Controller Framework (`src/vrcf/`)
Wraps OpenVR (vendored headers in `external/openvr/` as fallback when not found via vcpkg). Three globals, all driven each frame by `ModBase`: `VRControllers` (`VRControllersManager` — debounced button/trigger/thumbstick reads, heading), `VRControllersSuppress` (`VRControllersSuppressor` — owner-keyed input suppression that hides buttons/axes from the game while our own DLL still reads raw input), and `VRHaptics` (`VRControllersHaptic` — haptic feedback: sustained buzzes via per-frame re-pulse, named `HapticPattern` library, custom keyframed sequences). Full API, button map, and example: [`src/vrcf/README.md`](src/vrcf/README.md).

Suppression constraints that cause bugs if missed:
- Call `suppress`/`release`/`reset` from the **main thread** only — the vtable hook (slots 34/35) runs on the OpenVR thread and reads just an atomic mask.
- **Never** call F4SE/CommonLibF4VR from the hook.
- Owner-keyed: each call takes a `std::string_view key`; a key only undoes its own suppression, and the effective mask is the union of all owners.

Design deep-dive: `knowledge-base/commonframework_vr_input_suppression.md` in the reference library.

### F4VR Utilities (`src/f4vr/`)
Game-state helpers: node search/visibility/transform updates, player/weapon/menu state, `getVRPlayerNodes()` (CommonLibF4's `RE::VRPlayerNodes`, the VR reference nodes at `PlayerCharacter + 0x6E0`; the old `PlayerNodes` / `getPlayerNodes()` are deprecated, removed in v0.5.0), `SkellyBones` (100+ bone names + finger poses), Scaleform/HUD (`ScaleformUtils`), `GameMenusHandler`, `F4VRThumbstickControls`, `PlayerRotation` (snap/smooth turning that obeys the player's VR comfort settings and works where vanilla turning is taken away, rotating the VR room transform rather than the actor — design: [`docs/tech/vr-player-rotation.md`](docs/tech/vr-player-rotation.md)), and `F4VROffsets` (all RVAs). Full file list and snippets: [`src/f4vr/README.md`](src/f4vr/README.md). RVA authority + full PlayerNodes layout: `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` in the reference library.

### Debug Draw Overlay (`src/debug/`)
`f4cf::debug::DebugDraw` (facade `debug::dd()`) — immediate-mode in-world debug drawing: wire
primitives (line/point/arrow/box/sphere/capsule/cone/axes/grid/polyline/mesh), HUD text,
world-anchored labels, and a `watch()` value table, drawn on top of the VR view. It is a producer:
it fills a `render::PrimitiveDraw` and publishes it to its own `render::PrimitiveDrawRenderer`
layer at `DRAW_ORDER_DEBUG` — on top of everything and deliberately not occluded, since a debug
shape behind a wall is exactly the one you need to see. Call each frame to keep a shape visible
(auto-clears per frame); `sec > 0` persists a one-shot for that long. Zero cost and no hooks until
the first draw call (lazy install, driven by `ModBase` around `onFrameUpdate`).
`[Debug]` INI keys: `bDebugDrawEnabled`, `sDebugDrawDisabledChannels`, `sDebugDrawToggleBinding`.
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
- A draw callback runs on the **render thread**: never touch nodes/forms/config there, never call
  into OpenVR, and restore anything you bind beyond the shared `ScopedPipelineState`.
- Draw order is **declared** (`DRAW_ORDER_HINTS` 50 / `PANELS` 100 / `DEFAULT` 500 / `DEBUG` 900),
  not inherited from registration order — registration is lazy.
- Callbacks are **never unregistered** and the vtable patch is never removed, so a layer must outlive
  the process. Hold it as a static.
- `Texture::load()` / `Texture::view()` are **game thread only**; the `TextureView` in a published
  frame is what keeps the GPU texture alive across the boundary.

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
- **Not interactive** — vrui's finger-collision press handling does not reach ImGui widgets. Put the
  buttons beside the panel, in vrui.
- Content can only be measured by drawing it, which happens after vrui lays the frame out, so a panel
  sized to its content is laid out at **last frame's** size.
- `setFontSizePixels` / `setSupersample` are process-wide and read when the first canvas draws.
- Compiled out by `F4CF_WITH_IMGUI_UI=OFF`; nothing else in the framework depends on it.

Full API and the draw path: [`src/imgui/README.md`](src/imgui/README.md).

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
| `src/Config.h` / `src/Config.cpp` | Update class name, INI section name |
| `data/config/MyMod.ini` | Rename file; update `[MyMod]` section header |
| `cmake/Version.h.in` | (no change needed — driven by CMakeLists.txt) |
| `README.md` | Update links, description |

Also rename `CMakeUserPresets.json.template` → `CMakeUserPresets.json` (git-ignored) and fill in:
- `POST_BUILD_COPY_PLUGIN`: `true` to auto-copy DLL/PDB after build
- `COPY_PLUGIN_BASE_PATH`: path(s) to MO2 mod folder or `Fallout4VR\Data` (semicolon-separated)
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

Opens a VS solution in `build/`. Debug and Release configurations are both available. Release builds automatically stage everything (DLL, PDB, `data/mod/` contents) and produce a versioned `.7z` at `build/package/`.

### 4. Source file responsibilities

| File | Purpose |
|------|---------|
| `src/MyMod.h` | Mod class (extends `ModBase`); declares lifecycle overrides; holds global `g_myMod` singleton |
| `src/MyMod.cpp` | `F4SEPlugin_Query` / `F4SEPlugin_Load` entry points; lifecycle method bodies |
| `src/Config.h` | `Config` class (extends `ConfigBase`); declares INI-backed member variables |
| `src/Config.cpp` | `loadIniConfigInternal()` — reads each INI key via `simpleini`; holds `g_config` singleton |
| `src/Resources.h` | Resource IDs (e.g., `IDR_CONFIG_INI = 101`) for files embedded in the DLL |
| `src/PCH.h` | Precompiled header — includes F4SE, RE/Fallout.h, REL/Relocation.h, Logger.h, Version.h |
| `cmake/Version.h.in` | Template → auto-generated `Version.h` with `Version::PROJECT`, `Version::NAME`, semver consts |
| `cmake/version.rc.in` | Template → DLL metadata resource (file version, product name) |
| `cmake/resources.rc.in` | Template → embeds `MyMod.ini` as binary resource ID 101 inside the DLL |
| `cmake/package.cmake` | Post-build Release script: stages files → zips to versioned `.7z` |
| `data/config/MyMod.ini` | Shipped INI (also embedded in DLL as default). Sections: `[MyMod]` for settings, `[Debug]` for log level/pattern/debug flags |

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
2. Read it in `Config.cpp` → `loadIniConfigInternal()`:
   ```cpp
   myValue = static_cast<float>(ini.GetDoubleValue(DEFAULT_SECTION, "fMyValue", 0.0));
   ```
3. Add the key to `data/config/MyMod.ini` under `[MyMod]`.

The file watcher in `ConfigBase` calls `loadIniConfigInternal()` automatically when the INI changes on disk — no restart needed.

### 7. Framework assets (`f4cf\`)

`data/mod/` is the mod's Data folder. Everything the framework ships and loads by path sits in an `f4cf` folder inside the mod's own folders, so what came from the framework stays apart from what the mod added:

- `Meshes\MyMod\f4cf\activation-sphere.nif`, `Textures\MyMod\f4cf\activation-sphere.dds` / `debug-sphere.dds` — the sphere visuals (`f4vr::SphereStyle`)
- `Textures\MyMod\f4cf\bindings\` — the controller icons binding prompts draw (`vrui::BINDING_ICONS_DIR`)
- `Textures\MyMod\f4cf\vrui\` — shared VR UI icons (save, reset, exit, config, wiki, debug spheres) for `UIButtonPanel::setImage("f4cf\\vrui\\save.DDS")`

Ship `f4cf\` as the template has it and refresh it from the template when updating the framework. The mod's own assets go beside it, e.g. its own icons in `Textures\MyMod\vrui\`.

Text is not part of `f4cf\`: it is drawn in the framework's embedded font unless the mod ships its
own TrueType/OpenType file at `Data\Interface\<ModName>\<ModName>.ttf` — one file serves both the
vrui panels and the ImGui canvases, and it is read once at first use, so replacing it takes a restart.

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
| `src/ConfigBase.h/.cpp` | INI config with hot-reload |
| `src/f4vr/` | Game node/skeleton/animation utilities |
| `src/f4sevr/` | Papyrus native function registration helpers |
| `src/vrcf/VRControllersManager.h` | Controller button/trigger state |
| `src/vrui/` | VR widget system (NIF widgets + primitive-drawn panels) |
| `src/render/` | Shared OpenVR Submit hook, primitive/text/image renderers, and the scene-depth capture that occludes overlays behind the world (design + the `sSceneDepthStrategy` key: [`docs/tech/scene-depth-occlusion.md`](docs/tech/scene-depth-occlusion.md)) |
| `src/imgui/` | Dear ImGui canvases + `UIImGuiPanel`; compiled out by `F4CF_WITH_IMGUI_UI=OFF` |
| `CMakePresets.json` | VS2022/VS2026 preset definitions |
| `vcpkg.json` | Dependency manifest (spdlog, xbyak, nlohmann-json, simpleini, filewatch, cpptrace, imgui with `dx11-binding`) |
