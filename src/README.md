# `src/` — F4VR Common Framework source

This is the source tree of the **F4VR Common Framework**, a static library for building
[Fallout 4 VR](https://store.steampowered.com/app/611660/) F4SE plugins on top of
[CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).

If you are _consuming_ the framework to write a mod, start with the repo
[README](../README.md) and the [wiki](https://github.com/ArthurHub/F4VR-CommonFramework/wiki),
then come back here when you need to know what a specific subsystem offers. Each subfolder has its
own README with the details and a small usage snippet.

## Quick start

The bundled [`mod-template/`](../mod-template/) is the real starting point — copy it and rename
`MyMod` (see the repo [README](../README.md#usage)). This section shows how the three core pieces —
`ConfigBase`, `ModBase`, and the logger — fit together in code.

Every translation unit pulls in [`PCH.h`](PCH.h), which does `using namespace f4cf;`, so `ModBase`,
`ConfigBase`, and `logger::` resolve unqualified.

### 1. Config — derive from `ConfigBase`

```cpp
// Config.h
#include "ConfigBase.h"
#include "Resources.h"   // defines IDR_CONFIG_INI

namespace my_mod
{
    class Config : public ConfigBase
    {
    public:
        Config()
            : ConfigBase(Version::PROJECT, INI_PATH, IDR_CONFIG_INI)
        {}

        float myValue = 1.0f;   // your settings

    protected:
        void loadIniConfigInternal(const CSimpleIniA& ini) override;
    };

    inline Config g_config;     // global singleton
}

// Config.cpp
void Config::loadIniConfigInternal(const CSimpleIniA& ini)
{
    myValue = static_cast<float>(ini.GetDoubleValue("MyMod", "fMyValue", 1.0));
}
```

`loadIniConfigInternal` is called on load **and** automatically whenever the INI changes on disk
(hot-reload) — no restart needed. The INI lives under
`%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\{ModName}\`; the default is embedded in the
DLL (resource `IDR_CONFIG_INI`) and extracted on first run.

### 2. Mod — derive from `ModBase`

```cpp
// MyMod.h
#include "Config.h"
#include "ModBase.h"

namespace my_mod
{
    class MyMod : public ModBase
    {
    public:
        MyMod()
            // Settings(modName, version, config, trampolineSize, setupMainGameLoop)
            : ModBase(Settings(Version::PROJECT, Version::NAME, &g_config, 32, true))
        {}

    protected:
        void onModLoaded(const F4SE::LoadInterface* f4se) override;
        void onGameLoaded() override;
        void onGameSessionLoaded() override;
        void onFrameUpdate() override;
    };

    inline MyMod g_myMod;       // constructing it sets the f4cf::g_mod singleton
}
```

`setupMainGameLoop = true` installs the frame hook so `onFrameUpdate()` runs every frame.

### 3. Entry points + lifecycle

```cpp
// MyMod.cpp
#include "MyMod.h"
#include "f4vr/F4VRUtils.h"            // f4vr::showNotification
#include "vrcf/VRControllersManager.h" // vrcf::VRControllers

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* skse, F4SE::PluginInfo* info)
{
    return g_mod->onF4SEPluginQuery(skse, info);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* f4se)
{
    return g_mod->onF4SEPluginLoad(f4se);   // initializes logging + config, then calls onModLoaded
}

namespace my_mod
{
    void MyMod::onModLoaded(const F4SE::LoadInterface*) {}   // register Papyrus / install hooks
    void MyMod::onGameLoaded() {}                            // once, when the world is ready
    void MyMod::onGameSessionLoaded() {}                     // new game + each save load

    void MyMod::onFrameUpdate()
    {
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->loadedData) {
            logger::sample(3000, "no player data yet");   // logs at most once per 3s
            return;
        }

        // The frame the primary-hand trigger is pressed (debounced): notify + log.
        if (vrcf::VRControllers.isPressed(vrcf::Hand::Primary, vr::k_EButton_SteamVR_Trigger)) {
            f4vr::showNotification("Trigger pressed!");
            logger::info("trigger pressed; myValue = {}", g_config.myValue);
        }
    }
}
```

`isPressed` is one of many debounced controller helpers — see [`vrcf/`](vrcf/README.md) for the full
button/trigger/thumbstick API, and [`f4vr/`](f4vr/README.md) for `showNotification` / `showMessagebox`
and the rest of the game utilities.

### Logging

The logger is initialized for you inside `onF4SEPluginLoad` — just call it. Logs rotate under
`Documents\My Games\Fallout4VR\F4SE\{logFileName}.log`.

```cpp
logger::info("value = {}", x);        // levels: trace / debug / info / warn / error / critical
logger::sample(1000, "spammy {}", x); // rate-limited: one log per N ms, keyed by the format string
logger::infoRaw("no timestamp/prefix line");

if (logger::isDebugEnabled()) { /* skip expensive work when the level is above debug */ }
```

Log level and pattern come from the `[Debug]` INI section (`iLogLevel`, `sLogPattern`) and update live
on reload. See [`Logger.h`](Logger.h) and [`ConfigBase.h`](ConfigBase.h) for the rest (`[Debug]` flow
flags for runtime toggling, debug-dump triggers, and batch config saves).

## Layout

| Folder / File                        | Namespace      | What it is                                                                                                       |
| ------------------------------------ | -------------- | ---------------------------------------------------------------------------------------------------------------- |
| [`ModBase`](ModBase.h)               | `f4cf`         | Base class every mod derives from. Owns the F4SE lifecycle, logging init, and the per-frame update hook.         |
| [`ConfigBase`](ConfigBase.h)         | `f4cf`         | INI-backed config with hot-reload, embedded-default extraction, and version migration.                           |
| [`Logger.h`](Logger.h)               | `f4cf::logger` | spdlog wrapper. `logger::trace` / `debug` / `info` / `warn` / `error` functions + rate-limited `logger::sample`. |
| [`common/`](common/README.md)        | `f4cf::common` | Math (quaternions, matrices, transforms) and engine-agnostic utilities (strings, files, resources, time).        |
| [`f4vr/`](f4vr/README.md)            | `f4cf::f4vr`   | Fallout 4 VR game-state utilities: nodes, skeleton, player nodes, menus, Scaleform, thumbstick.                  |
| [`f4sevr/`](f4sevr/README.md)        | `F4SEVR`       | Ported F4SE VR SDK: Papyrus VM interop, native-function registration, VM value/arg marshalling.                  |
| [`vrcf/`](vrcf/README.md)            | `f4cf::vrcf`   | VR Controller Framework. OpenVR button/trigger/thumbstick state, input suppression, haptic feedback.             |
| [`vrui/`](vrui/README.md)            | `f4cf::vrui`   | VR UI widget system: panels, buttons, toggles, containers, scene graph, input dispatch.                          |
| [`PCH.h`](PCH.h)                     | —              | Precompiled header, included implicitly in every translation unit.                                               |
| [`MainLoopHook.h`](MainLoopHook.h)   | `f4cf`         | Trampoline into the game main loop that drives `ModBase::onFrameUpdate`.                                         |
| [`DebugAdjuster.h`](DebugAdjuster.h) | `f4cf`         | Live-tune a transform / hand pose / flow flags at runtime via controller input + INI reload.                     |

## How the pieces fit together

```
  Your mod (extends ModBase)
        │
        ├── ConfigBase ........ INI load/save, hot-reload, [Debug] keys
        ├── Logger ............ logger::info("...") etc.
        │
        └── onFrameUpdate()  ◄── MainLoopHook (game frame)
              ├── vrcf::VRControllers ........... read controller input
              ├── vrcf::VRControllersSuppress ... hide input from the game
              ├── vrcf::VRHaptics ............... haptic feedback patterns
              ├── vrui::UIManager ............... drive + render the VR UI
              └── f4vr::* / common::* ........... game state + math helpers
```

## Conventions

- **Root namespace is `f4cf`.** Subsystems live in nested namespaces (`f4cf::common`, `f4cf::f4vr`,
  `f4cf::vrcf`, `f4cf::vrui`). The exception is [`f4sevr/`](f4sevr/README.md), which hosts the
  ported F4SE VR SDK under `namespace F4SEVR`.
- **Globals.** A handful of subsystems expose a single global instance by design:
  `f4cf::g_mod`, `f4cf::vrcf::VRControllers`, `f4cf::vrcf::VRControllersSuppress`,
  `f4cf::vrcf::VRHaptics`, and `f4cf::vrui::g_uiManager`.
- **Style.** clang-format (LLVM-based, 180 cols, 4-space indent, CRLF, braces on new lines). See the
  repo [CLAUDE.md](../CLAUDE.md) and [.clang-format](../.clang-format).

## Reverse-engineering reference

RVAs, struct offsets, and vtable indices used here are documented in the modding reference library
(`Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md`). Authority order:
f4sevr 0.6.21 headers → CommonLibF4VR AddressLib → individual `_RE_REFERENCE.md` files.
