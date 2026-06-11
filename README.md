# F4VR Common Framework

[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)](LICENSE)

> A static library for building Fallout 4 VR F4SE plugins on top of [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).

F4VR Common Framework gives Fallout 4 VR mod authors a ready-made foundation so they can focus on
their mod instead of the plumbing: plugin lifecycle management, VR controller input, an in-world VR
UI widget system, INI config with hot-reload, and a large set of game-state utilities. Mods link it
as a static library and start from the included `mod-template/`.

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
(v0.2.0) that wraps [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).

What it provides:

- **Plugin lifecycle** — a `ModBase` class with `onModLoaded` / `onGameLoaded` / `onGameSessionLoaded` / `onFrameUpdate` hooks.
- **Config** — `ConfigBase`: INI loading, embedded defaults, version migration, and file-watch hot-reload.
- **VR controller input** — debounced button/trigger/thumbstick reads, plus owner-keyed input suppression.
- **VR UI** — a widget/button/container hierarchy rendered as in-world meshes with finger-collision interaction.
- **Game utilities** — node/skeleton manipulation, player/weapon/menu state, Scaleform/HUD access, and math.

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
cmake --preset default          # or: cmake --preset vs2026
```

This generates a Visual Studio solution in `build/`. Open `build/F4VRCommonFramework.sln`, then build
and debug as usual. All project configuration changes go in `CMakeLists.txt`, not the VS project.

Build options: `F4VR_BUILD_SHARED=ON` builds a DLL instead of a static lib (default `OFF`);
`COMMON_LIB_F4VR_PATH` overrides the path to CommonLibF4VR.

## Usage

The fastest path to a working plugin is the bundled `mod-template/`, which produces a DLL (F4SE
plugin) linked against this framework.

1. [Add the framework as a submodule](#add-to-your-mod) to your mod's repo.
2. Copy the relevant files from the `mod-template/` folder into your repo.
3. Replace every occurrence of `MyMod` (and `My Mod`) in the template files with your mod's name.
4. Generate and build: `cmake --preset default`.

Your mod class derives from `ModBase` and overrides the lifecycle hooks:

```cpp
void onModLoaded();          // F4SE load phase — register Papyrus functions, install hooks
void onGameLoaded();         // once, when the game world finishes loading
void onGameSessionLoaded();  // on new game + each save load
void onFrameUpdate();        // every frame while the player is initialized
```

## Modules

Each source subsystem has its own README with usage details and examples:

| Module                            | Description                                                              |
| --------------------------------- | ------------------------------------------------------------------------ |
| [`src/`](src/README.md)           | Source-tree overview and how the subsystems fit together.                |
| [`common/`](src/common/README.md) | Math (quaternions, matrices, transforms) and shared utilities.           |
| [`f4vr/`](src/f4vr/README.md)     | Fallout 4 VR game-state utilities: nodes, skeleton, menus, Scaleform.    |
| [`f4sevr/`](src/f4sevr/README.md) | Ported F4SE VR SDK: Papyrus VM interop and native-function registration. |
| [`vrcf/`](src/vrcf/README.md)     | VR Controller Framework: OpenVR input and input suppression.             |
| [`vrui/`](src/vrui/README.md)     | VR UI widget system: panels, buttons, toggles, containers.               |

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

[MIT](LICENSE) © 2025 Arthur
