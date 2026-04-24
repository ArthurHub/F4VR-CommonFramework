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

## Architecture

### Namespaces
- `f4cf::` — framework root (ModBase, Logger, ConfigBase)
- `f4cf::f4vr` — Fallout 4 VR game utilities (node/skeleton manipulation, animations, debug dumps)
- `f4cf::f4sevr` — F4SE interop (Papyrus native functions, form lookups, game events)
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
spdlog-based. Use macros: `log_trace`, `log_debug`, `log_info`, `log_warn`, `log_error`. The framework checks log level before formatting to avoid string construction overhead. Logger is initialized in `ModBase::onF4SELoad`.

### VR UI (`src/vrui/`)
Full widget hierarchy:

| Class | Description |
|-------|-------------|
| `UIElement` | Base renderable (position/rotation/scale, visibility) |
| `UIWidget` | 2D panel with collision detection and event callbacks |
| `UIButton` | Clickable with hover/pressed states |
| `UIToggleButton` | On/off toggle |
| `UIMultiStateToggleButton` | N-way cycle toggle |
| `UIContainer` | Parent for multiple elements |
| `UIToggleGroupContainer` | Radio button group (mutually exclusive) |
| `UIManager` | Singleton: scene graph, input dispatch, render state |

Call `UIManager.update()` each frame in `onFrameUpdate()` to drive input and rendering.

### VR Controller Framework (`src/vrcf/`)
Wraps OpenVR. `VRControllersManager` tracks left/right controller state (buttons, triggers, grip). OpenVR is optional — falls back to vendored headers in `external/openvr/` if not found via vcpkg.

Key calls: `VRControllers.getThumbstickValue(Hand::Primary/Offhand)` returns `NiPoint2 {x, y}`. `getControllerRelativeHeading(hand)` returns yaw for wand-directional movement.

### F4VR Utilities (`src/f4vr/`)
| Class/Utility | Description |
|---------------|-------------|
| `F4VRSkelly` | Complete skeleton bone enumeration (100+ bones, finger pose scalars) |
| `PlayerNodes` | VR-specific head/hand/body reference nodes (43 `NiNode*` at `PlayerCharacter + 0x6E0`) |
| `F4VRThumbstickControls` | Thumbstick input + gesture recognition |
| `ScaleformUtils` | Scaleform GFx menu manipulation, HUD access |
| `GameMenusHandler` | Menu state callbacks, pause detection |
| `F4VROffsets` | All reverse-engineered RVAs (see reference library `F4VR-CommonFramework_RE_REFERENCE.md`) |

PlayerNodes full layout (all 43 nodes) is in `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md` in the reference library.

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
