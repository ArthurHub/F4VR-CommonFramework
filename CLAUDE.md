# CLAUDE.md

F4VR Common Framework is a **static library** for Fallout 4 VR F4SE plugins, on
[CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR): the plugin lifecycle (`ModBase`), TOML
config with hot-reload, VR controller input, VR UI, an overlay renderer (lines, text, images, Dear
ImGui), devbench and Mods Config FO4 VR integration, perf timing, and game-state utilities. Its
version and every change a mod sees are in [`docs/CHANGELOG.md`](docs/CHANGELOG.md). A mod starts
from [`mod-template/`](mod-template/), following [`docs/creating-a-new-mod.md`](docs/creating-a-new-mod.md).

## Build and test

- Needs `VCPKG_ROOT` set, Visual Studio 2022 (v143) or 2026 (v145) x64, CMake 4.2+, and
  `git submodule update --init --recursive`.
- `cmake --preset default` generates the VS solution in `build/`; project changes go in
  `CMakeLists.txt`. Build with `cmake --build build --config Release`, test with
  `ctest --test-dir build -C Release`.
- CMake options:
  - `F4CF_WITH_IMGUI_UI` (ON): OFF drops `f4cf::imgui` and its headers, so code using it fails to compile.
  - `F4CF_WITH_TRACY` (ON): adds the `Tracy` configuration, Release plus the Tracy client ([perf README](src/perf/README.md#tracy)). A mod's `CMakeLists.txt` writes `$<CONFIG:Release,Tracy>` wherever it means Release.
  - `F4CF_BUILD_TESTS`: ON when top-level, OFF under a mod's `add_subdirectory`, where turning it on needs `catch2` in the mod's `vcpkg.json`.
  - `F4VR_BUILD_SHARED` (OFF) builds a DLL; `COMMON_LIB_F4VR_PATH` overrides `external/CommonLibF4VR`.

### Tests

Catch2, in two executables; a new test file is added to [`tests/CMakeLists.txt`](tests/CMakeLists.txt).

- `tests/*.cpp`: **plain std** logic. The binary compiles the `.cpp` files it covers directly
  (listed in `tests/CMakeLists.txt`), with no PCH, F4SE or CommonLibF4. This is the first choice:
  put game-independent logic in a plain-std file of its own (`vrcf/AxisRepeat`, `config/ConfigFile`)
  and test it here.
- `tests/linked/*.cpp`: code that needs the framework and CommonLibF4 to link, but not the game to
  run. `tests/linked/Setup.cpp` sets `REL::NO_GAME`, so every game address resolves into an unusable
  range and a test that calls the game crashes with an access violation. Nodes, the left-handed
  setting, controller state and OpenVR stay out of reach.

## Code style

- `pre-commit run --files <changed files>` formats with clang-format (180 columns, 4-space indent,
  CRLF, `T* p`) and runs the repo's checks. After cloning, `pre-commit install` once.
- Vendored verbatim, so never edited or reformatted: `src/devbench/DevBenchAPI.h/.cpp` and
  `src/modsconfig/ModsConfigFO4Api.h`.
- The binding prompt icons in `mod-template/data/mod/Textures/MyMod/f4cf/bindings/` are drawn by
  `tools/gen_binding_icons.py` (Pillow and NumPy): change the script and run it, never the icons.

## Docs

Each subsystem's README is canonical for its API and behavior. Update it with the code, and add a
`docs/CHANGELOG.md` entry for anything a mod sees. Keep this file to the map and the traps.

## Map

`PCH.h` does `using namespace f4cf;`. A mod derives from `ModBase` (global `f4cf::g_mod`) and
overrides `onModLoaded` (F4SE load: Papyrus, hooks), `onGameLoaded` (once), `onGameSessionLoaded`
(each new game and save load) and `onFrameUpdate` (every frame).

| Folder | Namespace | What it is | Read |
|--------|-----------|------------|------|
| `src/ModBase.*`, `Logger.h` | `f4cf`, `f4cf::logger` | Lifecycle, `ModBase::Settings`, spdlog logging | [src/README.md](src/README.md) |
| `src/config/` | `f4cf`, `f4cf::config` | `ConfigBase`: TOML load/save, hot-reload, overrides, migration | [src/config/README.md](src/config/README.md), [docs/debug-config.md](docs/debug-config.md) |
| `src/vrcf/` | `f4cf::vrcf` | Controller input (`VRControllers`), suppression (`VRControllersSuppress`), haptics (`VRHaptics`), bindings | [src/vrcf/README.md](src/vrcf/README.md), [docs/input-binding.md](docs/input-binding.md) |
| `src/vrui/` | `f4cf::vrui` | VR UI: NIF widgets and drawn panels, layout, `g_uiManager`, binding prompts | [src/vrui/README.md](src/vrui/README.md) |
| `src/render/` | `f4cf::render` | Overlay drawing: the shared Submit hook, primitives, text, textures, scene-depth occlusion | [src/render/README.md](src/render/README.md), [docs/tech/scene-depth-occlusion.md](docs/tech/scene-depth-occlusion.md) |
| `src/imgui/` | `f4cf::imgui` | Dear ImGui canvases on world-space quads, the wand pointer, the VR keyboard | [src/imgui/README.md](src/imgui/README.md), [docs/tech/vr-keyboard.md](docs/tech/vr-keyboard.md) |
| `src/debug/` | `f4cf::debug` | Immediate-mode in-world debug draw (`debug::dd()`) | [src/debug/README.md](src/debug/README.md) |
| `src/f4vr/` | `f4cf::f4vr` | Game state: nodes, skeleton, menus, Scaleform, player rotation; all RVAs in `F4VROffsets.h` | [src/f4vr/README.md](src/f4vr/README.md) |
| `src/f4sevr/` | `F4SEVR` | Ported F4SE VR SDK: Papyrus interop and native functions | [src/f4sevr/README.md](src/f4sevr/README.md) |
| `src/common/` | `f4cf::common` | Math (quaternions, matrices), utilities, `TomlEditor` | [src/common/README.md](src/common/README.md) |
| `src/devbench/` | `f4cf::devbench` | The mod's devbench tool: actions an agent calls in the running game | [src/devbench/README.md](src/devbench/README.md) |
| `src/modsconfig/` | `mods_config::api` | The mod's Mods Config FO4 VR client and its config previews | [src/modsconfig/README.md](src/modsconfig/README.md) |
| `src/perf/` | `f4cf::perf` | Hot-path timing (`F4CF_PERF_SCOPE`), GPU timers, Tracy | [src/perf/README.md](src/perf/README.md) |

## Traps

Most bugs here come from running code on the wrong **thread** or from a **lifetime** that ends too soon.

- **Render thread**: a `render` draw callback runs on Submit's thread, which is the game thread
  during play and the loading screen's thread while one is up. It never touches nodes, forms or
  config, never calls OpenVR, and restores anything it binds beyond `ScopedPipelineState`. A producer
  fills a `render::PrimitiveDraw` on the game thread and `publish()`es it.
- **Config load thread**: `loadConfigInternal()` runs on the file watcher's thread or the override
  setter's, so it only reads values. Engine work after a load goes in a
  `subscribeForConfigChangedEvent` callback, which runs on the game thread.
- **OpenVR thread**: the input suppression hook runs there and reads one atomic mask.
  `VRControllersSuppress` `suppress`/`release`/`reset` are game thread calls, and nothing on that hook
  calls F4SE or CommonLibF4.
- **devbench listener thread**: actions run on the game thread by default. `RunOn::Listener`
  actions and the state provider touch no game or mod state, and the state struct holds plain values,
  since a snapshot outlives its frame.
- **Game thread only**: `render::Texture::load()` / `view()`.
- **Lifetime**: render callbacks are never unregistered and the vtable patches are never removed, so
  a layer is held as a static. Draw order is declared (`DRAW_ORDER_*`), never taken from registration
  order.
- **ImGui images** go through `imgui::image` / `imgui::addImage` with a `render::Texture`, so the
  published frame keeps the view alive; `ImGui::Image` with a raw view does not.
- **Config saves** go through `ConfigBase` (`saveConfigValue(s)`, several in one `saveConfigValues`),
  which is how a write is known as the mod's own; any other write is loaded back as someone else's
  change. An override's owner clears only its own overrides.
- **devbench tool names** are one flat namespace in which a duplicate silently replaces the earlier
  tool, so the name comes from `Settings::name`. A devbench C-ABI slot newer than the first version
  needs a `GetBuildNumber()` check.

## Reverse engineering

Before looking up an RVA, struct offset or hook target, read the F4VR reference library's guide,
`C:\Stuff\GitHub\Mine\Modding-Reference\F4VR\CLAUDE.md`; this framework's own addresses are in its
`Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md`. Authority runs f4sevr 0.6.21 headers →
CommonLibF4VR AddressLib → the `_RE_REFERENCE.md` files.
