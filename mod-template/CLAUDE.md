# CLAUDE.md

My Mod (`MyMod`) is a C++23 F4SE plugin for Fallout 4 VR, built on the [F4VR Common Framework](external/F4VR-CommonFramework/README.md).

TODO: one paragraph on what the mod does.

## Build and test

Needs `VCPKG_ROOT` set, Visual Studio 2022 or 2026 (x64), CMake 4.2+, and `git submodule update --init --recursive`.

```bash
cmake --preset default                                    # VS 2026; the vs2022 preset for VS 2022
cmake --build --preset release                            # or debug / tracy / package
ctest --test-dir build -C Release --output-on-failure     # the unit tests
```

- **Presets:** `default` is `cmake-dev + vcpkg + windows + vs2026`, and the `release` / `debug` / `tracy` / `package` build presets build it into `build/`. `tracy` is Release with the Tracy profiler client. In-source builds are blocked.
- **Copy to the game:** `CMakeUserPresets.json` (git-ignored, from `CMakeUserPresets.json.template`) defines a `custom` preset whose `POST_BUILD_COPY_PLUGIN` and `COPY_PLUGIN_BASE_PATH` copy the DLL and PDB to `<path>/F4SE/Plugins/` on each build, for each of several `;`-separated paths. `COPY_PLUGIN_CONFIGURATIONS` limits it to some configurations: `all` (default) or a list such as `Release;Tracy`.
- **Packaging** is its own target, `package_mod` (the `package` preset): it builds the plugin if needed and runs `cmake/package.cmake`, which writes a versioned `.7z` of the DLL and `data/mod/` to `build/package/`. A non-Release archive has the configuration in its name.
- **CI:** `.github/workflows/build.yml` builds Release and runs the unit tests on each push and pull request to `main`; `maintenance.yml` runs pre-commit there and commits its fixes back (a fork's pull request is only checked).

### Unit tests

Catch2, in two executables set up in [tests/CMakeLists.txt](tests/CMakeLists.txt), where a new test file and the sources it covers are listed ([the framework's guide](external/F4VR-CommonFramework/docs/mod-tests.md)). `MOD_BUILD_TESTS` (ON) builds them.

- `tests/<area>/*Tests.cpp`: **plain std** logic, compiled with the sources it covers and no PCH, F4SE or CommonLibF4. The first choice: keep game-independent logic in a plain-std file of its own and test it here.
- `tests/linked/<area>/*Tests.cpp`: code that needs the framework and CommonLibF4, linked with them and the mod's embedded resources. There is no game in the process: a test that calls the game crashes with an access violation, so nodes, forms, the player and OpenVR stay out of reach.
- The mod's name and the folder start each test's CTest name (`MyMod: linked: Config: ...`), which the VS Code Testing panel shows as a tree. A test file's name is unique across the folders.

Behavior in the game is tested by hand, in the game.

## Code style

- After editing code, run `pre-commit run --files <changed files>`: clang-format for `src/` and `tests/`, Prettier for JSON outside `data/config/`, and the repo's other checks (`.pre-commit-config.yaml`). After cloning, `pre-commit install` once.
- clang-format (`.clang-format`): 180 columns, 4-space indent, CRLF, `T* p`, indented namespaces, braces on their own line for classes, functions and namespaces.
- camelCase variables and functions, PascalCase types; all project code in `namespace my_mod`.
- Log with the framework's `logger::trace/debug/info/warn/error`.
- Widely used includes go in the precompiled header [src/PCH.h](src/PCH.h).

## Architecture

[src/MyMod.cpp](src/MyMod.cpp) holds the F4SE exports, which forward to the mod class: a `ModBase` subclass (global `g_myMod`) implementing the framework's lifecycle hooks:

- `onModLoaded()`: F4SE load; register Papyrus functions and install hooks.
- `onGameLoaded()`: once, when the game finishes loading.
- `onGameSessionLoaded()`: on a new game and on every save load.
- `onFrameUpdate()`: every frame while the player is loaded.

| Component  | Files                                                 | Responsibility                                                                    |
| ---------- | ----------------------------------------------------- | --------------------------------------------------------------------------------- |
| **MyMod**  | [src/MyMod.h](src/MyMod.h) / [.cpp](src/MyMod.cpp)    | The mod class, the F4SE entry points, the head gesture, the devbench tool's description and state |
| **Config** | [src/Config.h](src/Config.h) / [.cpp](src/Config.cpp) | The mod's TOML config through `ConfigBase` (global `g_config`), reloaded live      |

TODO: add a row for each component as the mod grows.

### Config

[data/config/MyMod.toml](data/config/MyMod.toml) is the shipped config. It is embedded in the DLL (resource `IDR_CONFIG`) and written on first run to `%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\MyMod\MyMod.toml`.

- The main section is `[MyMod]`: it must match `NAME` in `CMakeLists.txt`, which the config reads as both the file name and the default section.
- `[Debug]` holds the framework's keys (log level and pattern, flow flags, data dumps, debug draw): [debug-config.md](external/F4VR-CommonFramework/docs/debug-config.md).
- To add a value: a member in `Config.h`, a read in `Config::loadConfigInternal()` (`config[SECTION]["key"].value_or(default)`, the default for a key that is missing or of another type), and the key in the shipped file. Write a float with a decimal point (`1.0`), a string in quotes, and a Windows path in single quotes (`'Textures\MyMod\icon.dds'`), where a backslash is not an escape.
- The file is watched and reloaded on change, on the watcher's thread, so `loadConfigInternal()` only reads values. Anything that calls into the engine goes in a `subscribeForConfigChangedEvent` callback, which runs on the game thread before the next frame, or in `onFrameUpdate()`.
- `[MyMod_HeadActivation]` is the head gesture, an activation sphere: `Config` reads it with `loadWandActivationConfig`, and `MyMod::onFrameUpdate()` drives it.
- Bump `iVersion` in `[Debug]` when the shipped file changes, so existing users' files are migrated; a value whose type changes is left to `updateConfigToLatestVersionCustom`. Saves go through `saveConfigValue(s)`, which change only the values and keep the file's comments and layout.

### Mod data

`data/mod/` is the mod's game `Data` tree, packaged beside the DLL. `Meshes\MyMod\f4cf\` and `Textures\MyMod\f4cf\` hold the framework's assets (activation sphere, binding icons, shared VR UI icons): ship them as they came, and copy them again from `external/F4VR-CommonFramework/mod-template/` when updating the framework. The mod's own assets go beside them.

`data/mod/ModsConfigFO4/Config/MyMod/spec.json` is the mod's page in Mods Config FO4 VR, a config spec of the settings in the TOML. A key added to, renamed in or removed from the TOML changes its row there, with the same default ([the config spec guide](https://github.com/ArthurHub/F4VR-Mods-Config/blob/main/docs/for-mod-authors/README.md)).

## Framework

The framework's [CLAUDE.md](external/F4VR-CommonFramework/CLAUDE.md) maps its subsystems and lists its traps: most bugs come from running code on the wrong thread (the render, config load, OpenVR and devbench threads) or from a lifetime that ends too soon. Read the matching README, under `external/F4VR-CommonFramework/`, before using a subsystem:

| Need                                                         | Read                                                |
| ------------------------------------------------------------ | --------------------------------------------------- |
| Config values, live reload, overrides, migration             | `src/config/README.md`                              |
| Controller input, input suppression, haptics                 | `src/vrcf/README.md`                                |
| Rebindable controller bindings                               | `docs/input-binding.md`                             |
| Activation spheres (reach-into-a-zone gestures)              | `docs/activation-spheres.md`, `src/f4vr/README.md`  |
| In-world VR UI: panels and buttons from text and images      | `src/vrui/README.md`                                |
| Lines, images and text drawn over the VR view                | `src/render/README.md`                              |
| Dear ImGui content on world-space quads, its themed controls | `src/imgui/README.md`, `src/imgui/controls/README.md` |
| Game state: nodes, skeleton, weapon and menu state, HUD      | `src/f4vr/README.md`                                |
| Papyrus: native functions, calling scripts                   | `src/f4vr/README.md` (Papyrus)                      |
| Debug draw overlay, the `[Debug]` config keys                | `src/debug/README.md`, `docs/debug-config.md`       |
| Timing a hot path (`F4CF_PERF_SCOPE`), Tracy                 | `src/perf/README.md`                                |
| The mod's devbench tool (drive and inspect the running game): keep its description and `state` current as features land | `src/devbench/README.md` |
| The mod's page in Mods Config FO4 VR                         | `src/modsconfig/README.md`                          |

## Dependencies

`vcpkg.json` lists them. Its `builtin-baseline` is the framework's, `5cd4931b18d49c77074703cf293a7837ac7506fa`, which CommonLibF4 builds with: keep it, unless CommonLibF4 is verified to build with the new one.
