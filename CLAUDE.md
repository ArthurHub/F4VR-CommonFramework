# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

F4VR Common Framework is a **static library** (v0.2.0) for building Fallout 4 VR F4SE plugins. It wraps [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR) and provides plugin lifecycle management, VR controller input, VR UI widgets, config hot-reload, and game-state utilities.

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
- `f4cf::f4vr` — Fallout 4 VR game utilities (node/skeleton manipulation, animations, debug dumps)
- `F4SEVR` (in `src/f4sevr/`) — ported F4SE VR SDK: Papyrus VM interop + native-function registration (note: this folder is `namespace F4SEVR`, not `f4cf::f4sevr`)
- `f4cf::vrcf` — VR controller framework (OpenVR button/trigger input)
- `f4cf::vrui` — VR UI system (widget/button/container hierarchy)
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
fDebugFlowFlag1/2/3 = 0   # Runtime feature toggles, read via g_config.debugFlowFlag1
sDebugFlowText1/2 =        # Debug text fields
sDebugDumpDataOnceNames =  # Comma-separated: ui_tree, skelly, fp_skelly, geometry, world, all_nodes
```

### Logging (`src/Logger.h`)
spdlog-based, in the `f4cf::logger` namespace (PCH does `using namespace f4cf;`, so call them unqualified as `logger::info(...)`). Levels: `logger::trace` / `debug` / `info` / `warn` / `error` / `critical`; plus `logger::sample(ms, fmt, ...)` for rate-limited logs and `logger::infoRaw` for unformatted lines. Guard expensive work with `logger::isDebugEnabled()` etc. Logger is initialized during `onF4SEPluginLoad`.

### VR UI (`src/vrui/`)
Widget hierarchy: `UIElement` → `UIWidget` → `UIButton`/`UIToggleButton`/`UIMultiStateToggleButton` (all three extend `UIWidget` directly); plus `UIContainer`/`UIToggleGroupContainer`; `UIManager` is the scene-graph singleton (global `g_uiManager`). Implement a `UIModAdapter`, attach elements via `g_uiManager`, and call `g_uiManager->onFrameUpdate(adapter)` each frame to drive input and rendering. Full hierarchy, assets, and example: [`src/vrui/README.md`](src/vrui/README.md).

### VR Controller Framework (`src/vrcf/`)
Wraps OpenVR (vendored headers in `external/openvr/` as fallback when not found via vcpkg). Three globals, all driven each frame by `ModBase`: `VRControllers` (`VRControllersManager` — debounced button/trigger/thumbstick reads, heading), `VRControllersSuppress` (`VRControllersSuppressor` — owner-keyed input suppression that hides buttons/axes from the game while our own DLL still reads raw input), and `VRHaptics` (`VRControllersHaptic` — haptic feedback: sustained buzzes via per-frame re-pulse, named `HapticPattern` library, custom keyframed sequences). Full API, button map, and example: [`src/vrcf/README.md`](src/vrcf/README.md).

Suppression constraints that cause bugs if missed:
- Call `suppress`/`release`/`reset` from the **main thread** only — the vtable hook (slots 34/35) runs on the OpenVR thread and reads just an atomic mask.
- **Never** call F4SE/CommonLibF4VR from the hook.
- Owner-keyed: each call takes a `std::string_view key`; a key only undoes its own suppression, and the effective mask is the union of all owners.

Design deep-dive: `knowledge-base/commonframework_vr_input_suppression.md` in the reference library.

### F4VR Utilities (`src/f4vr/`)
Game-state helpers: node search/visibility/transform updates, player/weapon/menu state, `getPlayerNodes()` (43 VR reference nodes at `PlayerCharacter + 0x6E0`), `SkellyBones` (100+ bone names + finger poses), Scaleform/HUD (`ScaleformUtils`), `GameMenusHandler`, `F4VRThumbstickControls`, and `F4VROffsets` (all RVAs). Full file list and snippets: [`src/f4vr/README.md`](src/f4vr/README.md). RVA authority + full PlayerNodes layout: `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` in the reference library.

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

### 7. VR UI assets

Pre-built `.nif` mesh files and `.DDS` textures live in `data/vrui/`. Button meshes come in grid sizes `ui_btn_NxM.nif` (up to 5×5); message meshes `ui_msg_NxM.nif` (up to 6×2). To customize:
- Replace or edit textures in `data/vrui/Textures/MyMod/` (DDS format)
- Adjust UV offsets in NifSkope (edit `BDEffectShaderProperty`) to pick a different cell from the grid
- Modify vertex positions for non-standard aspect ratios

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
| `src/vrui/` | VR widget system |
| `CMakePresets.json` | VS2022/VS2026 preset definitions |
| `vcpkg.json` | Dependency manifest (spdlog, xbyak, nlohmann-json, simpleini, filewatch, cpptrace) |
