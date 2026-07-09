# Creating a new Fallout 4 VR mod

This walks you from an empty folder to a **working F4SE plugin** built on the F4VR Common
Framework. By the end you'll have a DLL you can drop into Fallout 4 VR that pops an in-game
notification when you pull the trigger — and a hot-reloadable INI setting to go with it.

Everything here starts from the bundled [`mod-template/`](../mod-template/), a complete, buildable
plugin skeleton. You copy it, rename it, and fill in the one hook that matters.

- **Time:** ~15 minutes once the [prerequisites](#prerequisites) are in place.
- **You already know:** basic C++, Git, and where your Fallout 4 VR install lives.

---

## What you'll end up with

A minimal but real mod — call it **HelloVR** — that:

- loads as an F4SE VR plugin (`HelloVR.dll`),
- shows a *"Hello from HelloVR!"* notification each time you press the primary trigger,
- reads one setting from an INI that **hot-reloads** while the game runs (no restart),
- writes a rotating log you can tail to see it working.

The finished layout of your mod repo:

```
HelloVR/
├── external/
│   └── F4VR-CommonFramework/     # the framework, as a git submodule
├── cmake/                        # version + resource templates (from the template)
├── data/
│   ├── config/HelloVR.ini        # shipped INI (also embedded in the DLL as the default)
│   └── mod/                      # meshes/textures that ship next to the DLL
├── src/
│   ├── HelloVR.h / HelloVR.cpp   # your mod class + F4SE entry points
│   ├── Config.h / Config.cpp     # your INI-backed settings
│   ├── PCH.h                     # precompiled header
│   └── Resources.h               # embedded-resource IDs
├── CMakeLists.txt                # NAME / FRIENDLY_NAME / VERSION live at the top
├── CMakeUserPresets.json         # your local build settings (git-ignored)
└── vcpkg.json                    # dependency manifest
```

---

## Prerequisites

Same toolchain the framework itself uses — see the repo [README → Install](../README.md#install) for
the exact winget commands:

- **`VCPKG_ROOT`** environment variable pointing at a [vcpkg](https://github.com/microsoft/vcpkg) checkout.
- **Visual Studio 2022 (v143)** or **2026 (v145)**, x64, with the *Desktop development with C++* workload.
- **CMake 4.2+**.
- **Git** (the framework comes in as a submodule).
- A **Fallout 4 VR** install with the **[Fallout 4 VR Script Extender (F4SEVR)](https://f4se.silverlock.org/)** working.

> Verify F4SEVR first. Launch the game through `f4sevr_loader.exe` once and confirm it runs — if F4SE
> isn't loading, no plugin you build will load either.

---

## The steps at a glance

1. [Create the repo and add the framework submodule](#1-create-the-repo--add-the-framework)
2. [Copy the template in](#2-copy-the-template-in)
3. [Rename `MyMod` → your mod's name](#3-rename-mymod--your-mods-name)
4. [Point the build at your game folder](#4-point-the-build-at-your-game-folder)
5. [Generate and build](#5-generate-and-build)
6. [Write the feature: notification on button press](#6-write-the-feature-notification-on-button-press)
7. [Add a config value with hot-reload](#7-add-a-config-value-with-hot-reload)
8. [Deploy and test in-game](#8-deploy-and-test-in-game)

---

## 1. Create the repo & add the framework

Make your repo and pull the framework in as a submodule under `external/`:

```sh
git init HelloVR
cd HelloVR

mkdir external
git submodule add https://github.com/ArthurHub/F4VR-CommonFramework.git external/F4VR-CommonFramework
git submodule update --init --recursive
```

The last line is important: the framework has its own submodules (CommonLibF4VR and friends), and
`--recursive` pulls them too. Skipping it is the #1 cause of a `F4VR-CommonFramework path not found`
CMake error later.

> **Not using a submodule?** You can point `F4VR_COMMON_FRAMEWORK_PATH` at an existing checkout in
> step 4 instead. The submodule is the recommended path — it pins the exact framework version your mod
> builds against.

---

## 2. Copy the template in

Copy the **contents** of the framework's `mod-template/` folder into the root of your new repo:

```sh
cp -r external/F4VR-CommonFramework/mod-template/. .
```

Two files ship with a `.template` suffix so they don't accidentally get used as-is — rename them:

```sh
mv src/PCH.h.template            src/PCH.h
mv CMakeUserPresets.json.template CMakeUserPresets.json
```

`CMakeUserPresets.json` is git-ignored on purpose — it holds *your* machine's paths, not something you
commit. We'll fill it in at step 4.

You now have a **complete, buildable mod** — it just says `MyMod` everywhere. Renaming is next.

---

## 3. Rename `MyMod` → your mod's name

Two tokens run through the template, plus one that's easy to miss:

| Find                | Replace with          | What it is                                             |
| ------------------- | --------------------- | ----------------------------------------------------- |
| `MyMod`             | `HelloVR`             | Code identifier / DLL name (no spaces, case-sensitive) |
| `My Mod`            | `Hello VR`            | Friendly display name (may contain spaces)            |
| `my_mod` *(namespace)* | `hello_vr`         | The C++ namespace — a naive `MyMod` search misses it  |

Do the replacements, then rename the files whose names contain `MyMod`:

```sh
mv src/MyMod.h          src/HelloVR.h
mv src/MyMod.cpp        src/HelloVR.cpp
mv data/config/MyMod.ini data/config/HelloVR.ini
```

Places the tokens actually appear (all handled by a find-and-replace across the tree, but worth
knowing):

| File | What changes |
| ---- | ------------ |
| `CMakeLists.txt` | `set(NAME "MyMod")`, `set(FRIENDLY_NAME "My Mod")`, and `set(VERSION 0.1.0)` at the very top |
| `src/HelloVR.h` / `.cpp` | class `MyMod` → `HelloVR`, `namespace my_mod` → `hello_vr`, global `g_myMod` → `g_helloVR`, `#include "MyMod.h"` |
| `src/Config.h` / `.cpp` | `namespace my_mod` → `hello_vr` |
| `data/config/HelloVR.ini` | the `[MyMod]` section header and the `[MyMod_AnActivationSphere]` section |
| `vcpkg.json` | `"name": "f4vr-my-mod"` (cosmetic) |
| `README.md` | the template README's placeholders |

> **Why the INI section name matters.** `CMakeLists.txt`'s `NAME` becomes `Version::PROJECT`, which the
> config reads back as *both* the INI file name (`HelloVR.ini`) **and** the default section header. So
> the `[HelloVR]` section header in your INI must match the CMake `NAME` exactly, or your settings
> won't be found. The rename table above keeps them in sync.

The version at the top of `CMakeLists.txt` flows automatically into the DLL metadata and the packaged
release name — bump it there, nowhere else.

---

## 4. Point the build at your game folder

Open `CMakeUserPresets.json` and set where the built DLL should be copied after each build so you can
test it immediately. Point it at your **MO2 mod folder** or your **`Fallout4VR\Data`** folder:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "custom",
      "inherits": "default",
      "cacheVariables": {
        "POST_BUILD_COPY_PLUGIN": true,
        "COPY_PLUGIN_BASE_PATH": "C:/Path/To/MO2/mods/HelloVR"
      }
    }
  ]
}
```

- `POST_BUILD_COPY_PLUGIN: true` copies `HelloVR.dll` + `.pdb` into `<base>/F4SE/Plugins/` after every
  build. Set it `false` to skip.
- `COPY_PLUGIN_BASE_PATH` can be **several paths** separated by `;` (e.g. a MO2 folder *and* a live
  Data folder).
- Add `"F4VR_COMMON_FRAMEWORK_PATH": "C:/path/to/checkout"` here only if you skipped the submodule.

> Forward slashes in JSON. Use `C:/Path/...`, not `C:\Path\...` — a backslash is a JSON escape.

---

## 5. Generate and build

From the repo root:

```sh
cmake --preset custom     # or: cmake --preset default  (VS 2026 toolset)
```

This configures the project and writes a Visual Studio solution to `build/`. The first configure is
slow — vcpkg builds the dependencies once.

> **`default` vs `custom`:** `default` (VS 2026 / v145) is defined by the template. `custom` is the one
> *you* just defined; it `inherits` from `default` and adds the copy-to-game step. Use `custom` so your
> DLL lands in-game automatically. On VS 2022, add `"vs2022"` handling or edit the generator in
> `CMakePresets.json`.

Open `build/HelloVR.sln`, pick the **Debug** configuration, and build. You should get:

- `build/.../HelloVR.dll` and `HelloVR.pdb`,
- a copy of both under `<COPY_PLUGIN_BASE_PATH>/F4SE/Plugins/` (if you enabled the copy).

A **Release** build additionally stages `data/mod/` + the DLL and zips a versioned `.7z` into
`build/package/` — that archive *is* your Nexus upload.

At this point the mod builds and loads, but does nothing. Time to give it a body.

---

## 6. Write the feature: notification on button press

Open `src/HelloVR.cpp`. The template already guards `onFrameUpdate()` on the player being loaded — you
just add the input check. Replace the file body with this:

```cpp
#include "HelloVR.h"

#include "f4vr/F4VRUtils.h"             // f4vr::showNotification
#include "vrcf/VRControllersManager.h"  // vrcf::VRControllers

// F4SE entry points — the framework does the real work behind g_mod.
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    return g_mod->onF4SEPluginQuery(a_f4se, a_info);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    return g_mod->onF4SEPluginLoad(a_f4se);
}

namespace hello_vr
{
    void HelloVR::onModLoaded(const F4SE::LoadInterface*)
    {
        // Register Papyrus functions / install hooks here. Nothing to do yet.
    }

    void HelloVR::onGameLoaded()
    {
        logger::info("HelloVR loaded — pull the trigger to say hi");
    }

    void HelloVR::onGameSessionLoaded()
    {
        // Fires on new game and on every save load.
    }

    void HelloVR::onFrameUpdate()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->loadedData) {
            logger::sample(3000, "no player data yet - noop");
            return;
        }

        // The exact frame the primary-hand trigger goes down (debounced):
        if (vrcf::VRControllers.isPressed(vrcf::Hand::Primary, vr::k_EButton_SteamVR_Trigger)) {
            f4vr::showNotification("Hello from HelloVR!");
            logger::info("trigger pressed - greeting shown");
        }
    }
}
```

What each piece is doing:

- **`vrcf::VRControllers`** is the framework's per-frame controller snapshot (a global, driven for you
  by `ModBase`). `isPressed(...)` is *debounced* — it returns true only on the frame the button first
  goes down, not every frame it's held.
- **`vrcf::Hand::Primary`** is the player's dominant/weapon hand and respects left-handed mode
  automatically — prefer it over `Left`/`Right`.
- **`f4vr::showNotification(...)`** is the small corner toast (like "Quest updated"). For a modal box
  instead, use `f4vr::showMessagebox(...)`.
- **`logger::info` / `logger::sample`** write to your rotating log (see step 8). `sample(3000, ...)`
  logs at most once every 3 s so the "no player" branch doesn't spam.

> The primary trigger *also* fires your weapon — that's fine for a first test. Step 7 makes the button
> configurable, and the [input-binding guide](input-binding.md#suppressing-the-game-input) shows how to
> *suppress* the game's use of a button when you want it for yourself.

Rebuild. Load a save, pull the trigger — you should see **"Hello from HelloVR!"** in the corner.

---

## 7. Add a config value with hot-reload

Hard-coding the trigger is fine, but the framework's real strength is INI settings that reload live.
Let's make the greeting text configurable.

**1. Declare the setting** in `src/Config.h` (next to the existing `myConfigValue`):

```cpp
// Mod configs
std::string greeting = "Hello from HelloVR!";
```

**2. Read it** in `src/Config.cpp` → `loadIniConfigInternal()`:

```cpp
void Config::loadIniConfigInternal(const CSimpleIniA& ini)
{
    greeting = ini.GetValue(DEFAULT_SECTION, "sGreeting", "Hello from HelloVR!");
}
```

**3. Use it** in `HelloVR.cpp` instead of the literal:

```cpp
f4vr::showNotification(g_config.greeting);
```

**4. Add the key** to `data/config/HelloVR.ini` under the `[HelloVR]` section:

```ini
[HelloVR]
sGreeting = Hello from HelloVR!
```

Rebuild once. Now the payoff: **launch the game, then edit the INI while it's running.**

The live INI lives at:

```
%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\HelloVR\HelloVR.ini
```

It's created from the DLL's embedded default on first run. Change `sGreeting` to something else, save
the file, and pull the trigger again — the new text shows **without restarting**. `ConfigBase` watches
the file and re-runs `loadIniConfigInternal()` on every save.

> There's also a `{ModName}_Custom.ini` in the same folder that layers on top of the main INI — handy
> for user overrides that survive a mod update. See [`ConfigBase.h`](../src/ConfigBase.h).

**Want the *button* itself configurable too?** Swap the hard-coded `isPressed` for a data-driven
binding: declare an `f4cf::vrcf::InputBinding` in your config, load it with
`getInputBindingValue(ini, "HelloVR", "sGreetButton", …)`, and evaluate it each frame with
`vrcf::VRControllers.check(binding)`. Then players rebind it from the INI (`sGreetButton = offhand
longpress b`) with no rebuild. Full grammar: the [input-binding guide](input-binding.md).

---

## 8. Deploy and test in-game

If you set `COPY_PLUGIN_BASE_PATH`, the DLL is already in place after each build. Otherwise, copy by
hand so the final layout is:

```
<Fallout 4 VR>/Data/F4SE/Plugins/HelloVR.dll      (or your MO2 mod folder)
<Fallout 4 VR>/Data/F4SE/Plugins/HelloVR.pdb      (optional, for crash symbols)
```

Ship the contents of `data/mod/` alongside (meshes/textures) — for HelloVR there aren't any required,
but real mods put their `.nif`/`.dds` assets there.

**Confirm it loaded.** F4SE writes a per-plugin log to:

```
%USERPROFILE%\Documents\My Games\Fallout4VR\F4SE\HelloVR.log
```

You should see your `HelloVR loaded — pull the trigger to say hi` line, then a `trigger pressed`
line each time you fire. Bump `iLogLevel` in the `[Debug]` INI section for more detail (`1` = debug,
`0` = trace) — it hot-reloads like everything else.

That's a complete mod: it builds, loads, responds to input, and reads live config. 🎉

---

## Troubleshooting

| Symptom | Likely cause / fix |
| ------- | ------------------ |
| CMake: `F4VR-CommonFramework path not found` | Submodules not fetched — run `git submodule update --init --recursive`. |
| CMake: `VCPKG_ROOT is not set` | Set the `VCPKG_ROOT` env var to your vcpkg checkout, then re-open the terminal. |
| vcpkg fails building CommonLibF4VR | Keep the `"builtin-baseline"` in `vcpkg.json` (`b4a3d89…`) — newer baselines break the build. |
| Build succeeds, DLL never loads in-game | You're launching the flatscreen `Fallout4.exe` or plain launcher — launch through **`f4sevr_loader.exe`**. Also check the DLL is under `Data/F4SE/Plugins/`. |
| Loads (log appears) but no notification | Make sure a save is fully loaded (the `no player data` guard), and that you're pressing the *primary* (weapon-hand) trigger. |
| INI edits don't apply | Edit the **live** copy under `Documents\My Games\Fallout4VR\Mods_Config\HelloVR\`, not `data/config/` in your repo. |

---

## Where to go next

You have the skeleton; the framework has a lot more to offer. Each subsystem has a code-adjacent README
with examples:

- **[Controller bindings & activation spheres](input-binding.md)** — make inputs rebindable from the
  INI, and build reach-into-a-zone gestures.
- **[`vrcf/`](../src/vrcf/README.md)** — the full controller API (long-press, double-tap, thumbstick,
  haptics) and input suppression.
- **[`f4vr/`](../src/f4vr/README.md)** — game-state utilities: nodes, skeleton, weapon/menu state,
  Scaleform/HUD.
- **[`vrui/`](../src/vrui/README.md)** — build an in-world VR UI (buttons/panels rendered as meshes)
  and wire it into FRIK's config menu.
- **[`f4sevr/`](../src/f4sevr/README.md)** — register Papyrus native functions to talk to scripts.
- **[Debug & logging config](debug-config.md)** — the `[Debug]` INI keys: log patterns, flow flags,
  data dumps, live tuning.
- **[`src/README.md`](../src/README.md)** — how the subsystems fit together, at a glance.

When you're ready to publish, do a **Release** build — it packages a versioned `.7z` under
`build/package/` with the DLL and `data/mod/` laid out ready for a mod manager.
