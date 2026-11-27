# F4VR Common Framework

[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)
[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)](LICENSE)

> A static library for building Fallout 4 VR F4SE plugins on top of [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).

F4VR Common Framework gives Fallout 4 VR mod authors a ready-made foundation so they can focus on
their mod instead of the plumbing: plugin lifecycle management, VR controller input, an in-world VR
UI widget system with its own overlay renderer, TOML config with hot-reload, and a large set of
game-state utilities. Mods link it as a static library and start from the included `mod-template/`.

## Table of Contents

- [Background](#background)
- [Install](#install)
- [Usage](#usage)
- [Modules](#modules)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [Acknowledgements](#acknowledgements)
- [License](#license)

## Background

Writing a Fallout 4 VR F4SE plugin means re-solving the same problems every time: hooking the game
main loop, registering Papyrus functions, reading OpenVR controllers, drawing UI in 3D space, and
loading config that can change at runtime. This framework packages those into one reusable library
on [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR). Its version and every change a mod
sees are in the [changelog](docs/CHANGELOG.md).

What it provides:

- **Plugin lifecycle** — a `ModBase` class with `onModLoaded` / `onGameLoaded` / `onGameSessionLoaded` / `onFrameUpdate` hooks.
- **Config** — `ConfigBase`: TOML loading, saves that keep the file's comments and layout, embedded defaults, version migration, and file-watch hot-reload.
- **VR controller input** — debounced button/trigger/thumbstick reads, rebindable bindings, haptics, and owner-keyed input suppression.
- **VR UI** — panels, buttons and containers drawn from text and images at runtime, pressed by the fingertip.
- **Overlay rendering** — world-space lines, panels, images and text drawn over the VR view through one shared compositor hook, hidden behind the world where you want them to be.
- **Dear ImGui in VR** — the full immediate-mode widget set on world-space quads, standalone or as an element in a VR UI layout, with a themed set of controls and a VR keyboard.
- **Debug draw** — immediate-mode wire primitives, world labels and a watch table, at zero cost until the first call.
- **Game utilities** — node/skeleton manipulation, player/weapon/menu state, Scaleform/HUD access, activation spheres, Papyrus, and math.
- **Tooling** — a devbench tool per mod for agents driving the running game, a [Mods Config FO4 VR](https://github.com/ArthurHub/F4VR-Mods-Config) client, perf timing with Tracy, and the pieces of a mod's unit tests.

### Mods built on the framework

- [FRIK - Full Player Body with IK](https://github.com/rollingrock/Fallout-4-VR-Body)
- [Comfort Swim VR](https://github.com/ArthurHub/F4VR-ComfortSwim)
- [Immersive Flashlight VR](https://github.com/ArthurHub/F4VR-ImmersiveFlashlight)

## Install

### Prerequisites

- `VCPKG_ROOT` environment variable pointing to a [vcpkg](https://github.com/microsoft/vcpkg) installation
  - `git clone https://github.com/microsoft/vcpkg.git`
  - run `bootstrap-vcpkg.bat`. Example: `C:\github\vcpkg\bootstrap-vcpkg.bat`
  - Set environment variable `VCPKG_ROOT`. Example: `setx VCPKG_ROOT "C:\github\vcpkg"`
- Visual Studio 2022 (v143) or 2026 (v145), x64 only with C++ Desktop Development
  - `winget install -e --id Microsoft.VisualStudio.Community --source winget --override "--passive --wait --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --norestart"`
- CMake 4.2+
  - `winget install -e --id Kitware.CMake --source winget`

### Add to your mod

Add the framework as a git submodule of your mod's repo:

```sh
mkdir external
git submodule add https://github.com/ArthurHub/F4VR-CommonFramework.git external/F4VR-CommonFramework
git submodule update --init --recursive
```

### Build the framework standalone

To build or hack on the framework by itself:

```sh
git clone https://github.com/ArthurHub/F4VR-CommonFramework.git
cd F4VR-CommonFramework
git submodule update --init --recursive
cmake --preset default          # VS 2026; or: cmake --preset vs2022
```

This generates a Visual Studio solution in `build/`. Open `build/F4VRCommonFramework.slnx`, then build
and debug as usual. All project configuration changes go in `CMakeLists.txt`, not the VS project. The
unit tests run with `ctest --test-dir build -C Release`.

Build options:

- `F4CF_RUNTIME` (`VR`): the game the build is for, `VR` (Fallout 4 VR) or `FLAT` (flat Fallout 4, 1.10.163 and
  1.10.984 in one build). One build runs in one game. `FLAT` leaves out the VR-only parts, listed in
  `CMakeLists.txt`: the VR UI, the controllers (but the binding parser, the haptic patterns and an activation
  sphere's config, which a mod edits as values), the debug adjuster, the player's VR skeleton, turning, weapon
  tracking, wands and activation spheres, the world-space drawing (debug draw, primitives, the Submit hook, scene
  depth), the wand pointer and the VR keyboard. A shared file keeps its VR lines under `#ifdef ENABLE_FALLOUT_VR`:
  the VR nodes, the haptic player, the compositor timing, devbench's `input` and `keyboard` actions.
- `F4CF_WITH_IMGUI_UI` (`ON`): `OFF` drops the Dear ImGui UI layer and its vcpkg port.
- `F4CF_WITH_TRACY` (`ON`): adds the `Tracy` configuration, a Release build with the Tracy profiler client.
- `F4CF_BUILD_TESTS`: builds the unit tests in `tests/`, `ON` for a standalone build and `OFF` in a mod's.
- `F4VR_BUILD_SHARED` (`OFF`): builds a DLL instead of a static lib.
- `COMMON_LIB_F4VR_PATH`: overrides the path to CommonLibF4VR.

## Usage

A mod starts from the bundled `mod-template/`, a complete F4SE plugin linked against this framework:

1. [Add the framework as a submodule](#add-to-your-mod) to your mod's repo.
2. Copy `mod-template/` into the repo's root.
3. Rename `MyMod`, `My Mod`, `my_mod` and `myMod` to your mod's names, in the files and in the file names.
4. Configure and build: `cmake --preset default`, then `cmake --build --preset release`.

The [Creating a new mod](docs/creating-a-new-mod.md) guide walks through each step and ends with a
running example mod that shows a notification on a button press.

Your mod class derives from `ModBase` and overrides the lifecycle hooks:

```cpp
void onModLoaded();          // F4SE load phase — register Papyrus functions, install hooks
void onGameLoaded();         // once, when the game world finishes loading
void onGameSessionLoaded();  // on new game + each save load
void onFrameUpdate();        // every frame while the player is initialized
```

## Modules

Each source subsystem has its own README with usage details and examples:

| Module                                       | Description                                                                               |
| -------------------------------------------- | ----------------------------------------------------------------------------------------- |
| [`src/`](src/README.md)                      | Source-tree overview: `ModBase`, logging, and how the subsystems fit together.            |
| [`config/`](src/config/README.md)            | `ConfigBase`: TOML load and save, hot-reload, session overrides, migration.               |
| [`common/`](src/common/README.md)            | Math (quaternions, matrices, transforms), shared utilities, `TomlEditor`.                 |
| [`f4vr/`](src/f4vr/README.md)                | Game state: nodes, skeleton, menus, Scaleform, activation spheres, Papyrus.               |
| [`vrcf/`](src/vrcf/README.md)                | VR Controller Framework: OpenVR input, bindings, haptics and input suppression.           |
| [`vrui/`](src/vrui/README.md)                | VR UI: panels, buttons, toggles, containers.                                              |
| [`imgui/`](src/imgui/README.md)              | Dear ImGui panels as world-space quads, standalone or inside a vrui layout; the keyboard. |
| [`imgui/controls/`](src/imgui/controls/README.md) | A panel's controls in one themed look.                                               |
| [`render/`](src/render/README.md)            | Overlay rendering: lines, fills, images and text over the VR view; scene-depth occlusion. |
| [`debug/`](src/debug/README.md)              | In-world debug draw: wire primitives, HUD text, world labels, watch table.                |
| [`perf/`](src/perf/README.md)                | Hot-path CPU and GPU timing, read on demand; Tracy.                                       |
| [`devbench/`](src/devbench/README.md)        | Each mod's devbench tool, for agents and scripts driving the running game.                |
| [`modsconfig/`](src/modsconfig/README.md)    | The mod's Mods Config FO4 VR client: functions, states, live preview.                     |

Player-facing guides a mod links from its own docs: [controller bindings](docs/input-binding.md),
[activation spheres](docs/activation-spheres.md) and [debug settings](docs/debug-config.md). A mod's
unit tests: [docs/mod-tests.md](docs/mod-tests.md).

## Maintainers

[@ArthurHub](https://github.com/ArthurHub)

## Contributing

PRs accepted. For larger changes, please open an issue first to discuss what you would like to change.

Code style is enforced by clang-format (LLVM-based, 180-column, 4-space indent, CRLF). After cloning,
run `pre-commit install` once so the formatter runs on every commit. See [CLAUDE.md](CLAUDE.md) for
the full architecture and conventions.

## Acknowledgements

Modding is built on the community — this wouldn't exist without others' public code:

- Ryan-rsm-McKenzie, alandtse, and the other [CommonLib](https://github.com/alandtse/CommonLibVR/tree/vr) contributors.
- RollingRock, alandtse, shizof, and CylonSurfer for open-source mods like FRIK and VirtualHolsters to learn and adapt code from.

## License

[GPL-3.0-or-later](LICENSE) © 2025 Arthur T
