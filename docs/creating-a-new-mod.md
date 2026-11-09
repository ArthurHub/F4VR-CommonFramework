# Creating a new mod

From an empty folder to a Fallout 4 VR F4SE plugin built on the framework, in about 15 minutes. You copy
[`mod-template/`](../mod-template/), rename it, and add a feature. The example mod, **HelloVR**, shows a
notification when you pull the trigger, with its text in a config file that reloads while the game runs.

## Prerequisites

- **`VCPKG_ROOT`** set to a [vcpkg](https://github.com/microsoft/vcpkg) checkout.
- **Visual Studio 2022 (v143) or 2026 (v145)**, x64, with the *Desktop development with C++* workload.
- **CMake 4.2+** and **Git**.
- **Fallout 4 VR** with [F4SEVR](https://f4se.silverlock.org/). Launch it once through
  `f4sevr_loader.exe` to check it works: if F4SE doesn't load, no plugin will.

The install commands are in the [README](../README.md#prerequisites).

## 1. Create the repo

```sh
git init HelloVR
cd HelloVR
mkdir external
git submodule add https://github.com/ArthurHub/F4VR-CommonFramework.git external/F4VR-CommonFramework
git submodule update --init --recursive
```

`--recursive` matters: the framework has submodules of its own (CommonLibF4VR). The submodule pins the
framework version your mod builds against. To use a checkout elsewhere instead, set
`F4VR_COMMON_FRAMEWORK_PATH` in step 4.

## 2. Copy the template

```sh
cp -r external/F4VR-CommonFramework/mod-template/. .
mv src/PCH.h.template src/PCH.h
mv CMakeUserPresets.json.template CMakeUserPresets.json
python -m pip install pre-commit
pre-commit install
```

You now have a complete mod that builds. It came with:

- **`src/`:** the mod class and its F4SE entry points (`MyMod.h/.cpp`), its config (`Config.h/.cpp`), the
  precompiled header.
- **`data/config/MyMod.toml`:** the shipped config, also embedded in the DLL as the default.
- **`data/mod/`:** a game `Data` tree shipped beside the DLL, with the framework's meshes and textures.
- **`data/resources/`:** the source PNGs of the UI atlases, not shipped.
- **`CMakeLists.txt`:** `NAME`, `FRIENDLY_NAME` and `VERSION` at the top. The version flows into the DLL
  and the package name, so bump it there and nowhere else.
- **`CMakeUserPresets.json`:** your machine's paths, git-ignored.
- **Code style and CI:** `.pre-commit-config.yaml` runs clang-format for C++ and Prettier for JSON,
  plus basic checks, on each commit. Prettier uses `.prettierrc.json`, includes JSON specs under
  `data/mod/`, and excludes exported configs under `data/config/`.
  `.github/workflows/` runs them on GitHub and commits the fixes, and builds Release, on each push and
  pull request to `main`.
- **`CLAUDE.md`:** guidance for Claude Code. Fill in its TODOs as the mod grows, or delete it.

## 3. Rename `MyMod`

Find and replace across the tree, case-sensitive:

| Find     | Replace    | What it is                                         |
| -------- | ---------- | -------------------------------------------------- |
| `MyMod`  | `HelloVR`  | The code name and the DLL's: no spaces             |
| `My Mod` | `Hello VR` | The display name                                   |
| `my_mod` | `hello_vr` | The C++ namespace, which a search for `MyMod` misses |
| `myMod`  | `helloVR`  | The global, `g_myMod`                              |

Then rename the files and asset folders that carry the name:

```sh
mv src/MyMod.h src/HelloVR.h
mv src/MyMod.cpp src/HelloVR.cpp
mv data/config/MyMod.toml data/config/HelloVR.toml
mv data/mod/Meshes/MyMod data/mod/Meshes/HelloVR
mv data/mod/Textures/MyMod data/mod/Textures/HelloVR
```

> **The config section is the CMake `NAME`.** `NAME` is both the config file's name and its default
> section, so `[HelloVR]` in the config has to match it exactly, or the mod finds no settings.

### The assets

- **`f4cf\`, the framework's assets:** the activation-sphere mesh and textures, their icons, the binding
  prompts' controller icons, and shared VR UI button icons. Nothing in them names the mod, so renaming
  the folders is enough. Ship them as they came, and copy them again from `mod-template/` when you update
  the framework. Your own assets go beside them, as `Textures\HelloVR\vrui\`.
- **`ui-common` and `ui-example`, the NIF button atlases:** each `.nif` has its texture path inside it,
  so after the `mv` it still points at `Textures\MyMod\`. Re-pack them under your name from
  `data/resources/`, which overwrites them in place:

  ```sh
  python external/F4VR-CommonFramework/nif-tools/vrui_atlas.py pack \
    --name ui-common --texture-subpath HelloVR data/resources/common --output data/mod
  ```

  `ui-example` is a demo: re-pack it the same way, replace it, or delete it. Each PNG's name is the
  `.nif` name you give `UIButton` / `UIWidget`. All the options: [nif-tools](../nif-tools/README.md).

You may need no atlas at all: a `vrui::UIButtonPanel` draws its face from text and a `.dds` at runtime,
with no NIF ([vrui assets](../src/vrui/README.md#assets)).

## 4. Point the build at the game

In `CMakeUserPresets.json`, delete the comment block at the top (JSON has no comments), then set where
each build copies the DLL: your MO2 mod folder or the game's `Data` folder.

```json
"cacheVariables": {
  "POST_BUILD_COPY_PLUGIN": true,
  "COPY_PLUGIN_CONFIGURATIONS": "all",
  "COPY_PLUGIN_BASE_PATH": "C:/Path/To/MO2/mods/HelloVR",
  "F4VR_COMMON_FRAMEWORK_PATH": ""
}
```

- **`COPY_PLUGIN_BASE_PATH`:** the DLL and its `.pdb` go to `<path>/F4SE/Plugins/`. Several paths are
  separated by `;`. Use forward slashes: a backslash is an escape in JSON.
- **`COPY_PLUGIN_CONFIGURATIONS`:** `all`, or the configurations to copy on, as `"Release;Tracy"`.
- **`F4VR_COMMON_FRAMEWORK_PATH`:** empty uses the submodule.

## 5. Build

```sh
cmake --preset custom
cmake --build --preset custom-debug
```

The first configure is slow, as vcpkg builds the dependencies once. The solution is
`build/HelloVR.slnx`, if you'd rather build and debug in Visual Studio.

- **`custom-release`:** a Release build.
- **`custom-package`:** a Release build, packaged into a versioned `.7z` in `build/package/`: the DLL and
  `data/mod/`, ready for a mod manager. That archive is your Nexus upload.
- **`custom-tracy`:** a Release build the [Tracy](https://github.com/wolfpld/tracy) profiler connects to
  ([perf README](../src/perf/README.md#tracy)).
- **Visual Studio 2022:** the presets default to 2026. Make `custom` inherit
  `["cmake-dev", "vcpkg", "windows", "vs2022"]` instead of `default`.

The mod now loads, and does nothing.

## 6. Add the feature

In `src/HelloVR.cpp`, add the includes and fill in two of the hooks:

```cpp
#include "HelloVR.h"

#include "f4vr/F4VRUtils.h"
#include "vrcf/VRControllersManager.h"

// ... the F4SE entry points stay as they are ...

namespace hello_vr
{
    void HelloVR::onGameLoaded()
    {
        logger::info("HelloVR loaded - pull the trigger to say hi");
    }

    void HelloVR::onFrameUpdate()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->loadedData) {
            logger::sample(3000, "no player data yet - noop");
            return;
        }

        if (vrcf::VRControllers.isPressed(vrcf::Hand::Primary, vr::k_EButton_SteamVR_Trigger)) {
            f4vr::showNotification("Hello from HelloVR!");
            logger::info("trigger pressed - greeting shown");
        }
    }
}
```

- **`VRControllers.isPressed`** is true only on the frame the button goes down, not while it is held.
- **`Hand::Primary`** is the weapon hand, and follows left-handed mode.
- **`showNotification`** is the corner message; `showMessagebox` is a modal box.
- **`logger::sample(3000, ...)`** logs at most once every 3 s.

The trigger also fires your weapon, which is fine for a test. To take a button from the game, see
[suppressing the game input](input-binding.md#suppressing-the-game-input).

The other hooks: `onModLoaded` runs at F4SE load (Papyrus functions, hooks), `onGameLoaded` once, and
`onGameSessionLoaded` on each new game and save load.

## 7. Add a config value

**Declare it** in `src/Config.h`, beside `myConfigValue`:

```cpp
std::string greeting = "Hello from HelloVR!";
```

**Read it** in `Config::loadConfigInternal` in `src/Config.cpp`. `value_or` gives the default for a key
that is missing or of another type:

```cpp
greeting = config[DEFAULT_SECTION]["sGreeting"].value_or(std::string("Hello from HelloVR!"));
```

**Use it** in `HelloVR.cpp`: `f4vr::showNotification(g_config.greeting);`

**Ship it** in `data/config/HelloVR.toml`:

```toml
[HelloVR]
sGreeting = "Hello from HelloVR!"
```

Rebuild, start the game, and edit the live config while it runs:
`Documents\My Games\Fallout4VR\Mods_Config\HelloVR\HelloVR.toml`, created from the embedded default on
the first run. Save it, pull the trigger, and the new text shows. `loadConfigInternal()` runs again on
each save, on the file watcher's thread, so it only reads values. Every way to read a value, and what
runs after a load: [config README](../src/config/README.md).

To make the button configurable too, read an `InputBinding` with `getInputBindingValue` and check it with
`VRControllers.check(binding)`. Players then rebind it in the config:
`kGreetButton = { hand = "offhand", type = "longpress", button = "b" }`
([bindings](input-binding.md), [`vrcf/`](../src/vrcf/README.md#config-driven-bindings)).

## 8. Test in game

With `COPY_PLUGIN_BASE_PATH` set, the DLL is in place after each build. Otherwise copy
`HelloVR.dll` (and `HelloVR.pdb`, for crash symbols) to `Data/F4SE/Plugins/`. Ship `data/mod/` beside
it as a `Data` tree: without the `f4cf\` assets, activation spheres, their icons and binding prompts
draw nothing.

The log is `Documents\My Games\Fallout4VR\F4SE\HelloVR.log`. It has your `HelloVR loaded` line, then
`trigger pressed` each time you pull it. For more detail, set `iLogLevel = 1` under `[Debug]`
([debug settings](debug-config.md)).

## Troubleshooting

| Symptom                                    | Fix                                                                                          |
| ------------------------------------------ | -------------------------------------------------------------------------------------------- |
| CMake: `F4VR-CommonFramework path not found` | `git submodule update --init --recursive`                                                  |
| CMake: `VCPKG_ROOT` not set                | Set it to your vcpkg checkout, then open a new terminal                                      |
| CMake: can't parse `CMakeUserPresets.json` | Delete the comment block at its top                                                          |
| vcpkg fails to build CommonLibF4VR         | Keep `vcpkg.json`'s `builtin-baseline`, the one the framework is tested against              |
| The DLL never loads                        | Launch through `f4sevr_loader.exe`, and check the DLL is in `Data/F4SE/Plugins/`             |
| It loads, but no notification              | Load a save fully, and pull the trigger of your weapon hand                                  |
| Config edits don't apply                   | Edit the live file under `Documents\My Games\...\Mods_Config\HelloVR\`, not `data/config/`   |

## Where to go next

- **[`src/README.md`](../src/README.md):** how the subsystems fit, and `ModBase::Settings`.
- **[Controller bindings](input-binding.md)** and **[activation spheres](activation-spheres.md):**
  rebindable inputs and reach-into-a-zone gestures, as players see them.
- **[`vrcf/`](../src/vrcf/README.md):** controller input, haptics, and taking buttons from the game.
- **[`f4vr/`](../src/f4vr/README.md):** nodes, skeleton, weapon and menu state, the HUD, activation spheres.
- **[`vrui/`](../src/vrui/README.md):** in-world VR UI, from meshes or from text and images.
- **[`imgui/`](../src/imgui/README.md):** Dear ImGui on a world-space quad.
- **[`render/`](../src/render/README.md):** your own lines, images and text over the VR view.
- **[`modsconfig/`](../src/modsconfig/README.md):** the mod's page in Mods Config FO4 VR.
- **[`devbench/`](../src/devbench/README.md):** actions an agent calls in the running game.
- **[`debug/`](../src/debug/README.md)** and **[`perf/`](../src/perf/README.md):** debug draw, and timing.
- **[`f4sevr/`](../src/f4sevr/README.md):** Papyrus native functions.
