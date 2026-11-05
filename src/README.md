# `src/` — F4VR Common Framework source

The source of the **F4VR Common Framework**, a static library for
[Fallout 4 VR](https://store.steampowered.com/app/611660/) F4SE plugins on
[CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR). A new mod starts from
[`mod-template/`](../mod-template/) ([creating a new mod](../docs/creating-a-new-mod.md)); this page is
how its three core pieces fit, and a map of the subsystems, each with a README of its own.

[`PCH.h`](PCH.h) does `using namespace f4cf;`, so `ModBase`, `ConfigBase` and `logger::` resolve
unqualified.

## The config: derive from `ConfigBase`

```cpp
// Config.h
#include "config/ConfigBase.h"
#include "Resources.h" // IDR_CONFIG, the embedded default config

namespace my_mod
{
    class Config : public ConfigBase
    {
    public:
        Config()
            : ConfigBase(Version::PROJECT, CONFIG_PATH, IDR_CONFIG)
        {}

        float myValue = 1.0f;

    protected:
        void loadConfigInternal(const toml::table& config) override;
    };

    inline Config g_config;
}

// Config.cpp
void Config::loadConfigInternal(const toml::table& config)
{
    // the default for a key that is missing or of another type; an integer reads as a float
    myValue = config["MyMod"]["fMyValue"].value_or(1.0f);
}
```

- **The file:** `Documents\My Games\Fallout4VR\Mods_Config\<ModName>\<ModName>.toml`, written from the
  embedded default on the first run. A broken file is logged with its line, and the values stay.
- **Hot reload:** `loadConfigInternal` runs again on each change of the file, on the file watcher's
  thread (or an override setter's), so it **only reads values**.
- **Engine work after a load** goes in a subscriber, which runs on the game thread before the next
  `onFrameUpdate()`. It can name keys, and is then called only when one of them changed:

```cpp
g_config.subscribeForConfigChangedEvent("MyMod", [](const std::string&) { /* apply */ });
g_config.subscribeForConfigChangedEvent("MyMod.hand", { { "Main", "fScale" }, { "Main", "hand" } },
    [](const std::vector<f4cf::config::ConfigKey>& changed) { /* rebuild what they make */ });
```

- **Saving** is `saveConfigValue(s)`, which keeps the file's comments and layout and isn't loaded back.
- **Session overrides** (`setConfigOverride`) replace a value until cleared, never written to the file:
  how devbench and Mods Config FO4 VR preview a setting. Each owner clears only its own.

All of it, with the value readers and migration: [`config/README.md`](config/README.md).

## The mod: derive from `ModBase`

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
            // Settings(name, version, config, trampolineSize, setupMainGameLoop)
            : ModBase(Settings(Version::PROJECT, Version::NAME, &g_config, 64, true))
        {}

    protected:
        void onModLoaded(const F4SE::LoadInterface* f4se) override; // F4SE load: Papyrus, hooks
        void onGameLoaded() override;                               // once, when the world is ready
        void onGameSessionLoaded() override;                        // each new game and save load
        void onFrameUpdate() override;                              // every frame
    };

    inline MyMod g_myMod; // constructing it sets f4cf::g_mod
}

// MyMod.cpp
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info)
{
    return g_mod->onF4SEPluginQuery(f4se, info);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* f4se)
{
    return g_mod->onF4SEPluginLoad(f4se); // logging and config, then onModLoaded
}
```

`setupMainGameLoop = true` installs the frame hook that runs `onFrameUpdate()`. The other
[`Settings`](ModBase.h) fields are set in the constructor's body (`_settings.x = ...;`):

| Field | Default | What it does |
|-------|---------|--------------|
| `f4seName`, `logFileName` | `name` | The name reported to F4SE, and the log file's. |
| `trampolineAllocationSize` | 256 | Bytes of trampoline for the mod's hooks. |
| `setupMainGameLoopLate` | `false` | Install the frame hook at game loaded, so this mod's frame runs before every other's (FRIK, which updates the skeleton). |
| `preloadRendering` | `false` | Build the font (from plugin load, in the background) and the render pipeline (at game loaded) ahead, instead of stalling the first frame that draws (~0.1 s). On for a mod that draws panels, activation-sphere icons or overlays. |
| `devbenchTool` | `true` | Register the mod's [devbench tool](devbench/README.md) when devbench is installed. |
| `modsConfigClient`, `modsConfigMinVersion` | `true`, 1 | Connect to [Mods Config FO4 VR](modsconfig/README.md) at `kPostLoad`, when installed with at least that API version. |

## Logging

Initialized in `onF4SEPluginLoad`. The log rotates at `Documents\My Games\Fallout4VR\F4SE\<logFileName>.log`,
and its level and pattern are `[Debug]`'s `iLogLevel` and `sLogPattern`, live
([debug settings](../docs/debug-config.md)).

```cpp
logger::info("value = {}", x);        // trace, debug, info, warn, error, critical
logger::sample(1000, "spammy {}", x); // at most once per 1000 ms, keyed by the format string
logger::infoRaw("a line with no timestamp or prefix");
if (logger::isDebugEnabled()) { /* skip expensive work otherwise */ }
```

## Layout

| Folder / file | Namespace | What it is |
| ------------- | --------- | ---------- |
| [`ModBase`](ModBase.h) | `f4cf` | The base of every mod: the F4SE lifecycle, logging, the frame hook. |
| [`config/`](config/README.md) | `f4cf`, `f4cf::config` | `ConfigBase`: TOML with hot reload, layout-keeping saves, session overrides, migration. |
| [`Logger.h`](Logger.h) | `f4cf::logger` | spdlog, and the rate-limited `logger::sample`. |
| [`common/`](common/README.md) | `f4cf::common` | Math (quaternions, matrices, transforms) and utilities (strings, files, resources, time). |
| [`vrcf/`](vrcf/README.md) | `f4cf::vrcf` | Controller input, taking input from the game, haptics, bindings. |
| [`vrui/`](vrui/README.md) | `f4cf::vrui` | The in-world VR UI: panels, buttons, toggles, containers. |
| [`imgui/`](imgui/README.md) | `f4cf::imgui` | Dear ImGui on world-space quads: a `Canvas`, and `UIImGuiPanel` for a vrui layout. |
| [`render/`](render/README.md) | `f4cf::render` | Overlay drawing: lines, fills, images and text over the VR view, the Submit hook, occlusion. |
| [`debug/`](debug/README.md) | `f4cf::debug` | Immediate-mode in-world debug draw, labels, a watch table. |
| [`f4vr/`](f4vr/README.md) | `f4cf::f4vr` | Game state: nodes, skeleton, player nodes, menus, Scaleform, activation spheres. |
| [`f4sevr/`](f4sevr/README.md) | `F4SEVR` | The ported F4SE VR SDK: Papyrus interop and native functions. |
| [`devbench/`](devbench/README.md) | `f4cf::devbench` | The mod's devbench tool: actions an agent or script calls in the running game. |
| [`modsconfig/`](modsconfig/README.md) | `mods_config::api` | The mod's Mods Config FO4 VR client: functions, states, quick launch, live preview. |
| [`perf/`](perf/README.md) | `f4cf::perf` | Hot-path timing that nests into a tree, read on demand. |
| [`MainLoopHook.h`](MainLoopHook.h) | `f4cf` | The hook into the game's main loop that drives `onFrameUpdate`. |
| [`DebugAdjuster.h`](DebugAdjuster.h) | `f4cf` | `[Debug] sAdjustTarget`: tune a transform, hand pose or number with the controllers. |
| [`PCH.h`](PCH.h) | | The precompiled header. |

```
  Your mod (extends ModBase)
        │
        ├── ConfigBase ........ TOML load/save, hot reload, [Debug] keys
        ├── Logger ............ logger::info("...") etc.
        │
        └── onFrameUpdate()  ◄── MainLoopHook (game frame)
              ├── vrcf::VRControllers ........... read controller input
              ├── vrcf::VRControllersSuppress ... hide input from the game
              ├── vrcf::VRHaptics ............... haptic patterns
              ├── vrui::g_uiManager ............. the VR UI
              ├── imgui::UIImGuiPanel ........... Dear ImGui inside that UI
              ├── debug::dd() ................... debug draw
              └── f4vr::* / common::* ........... game state and math
                          │
                          └── render::* ......... the one overlay path the panels, the canvases
                                                  and debug draw all draw through
```

## Conventions

- **Namespaces:** the root is `f4cf`, each subsystem nested in it. [`f4sevr/`](f4sevr/README.md) is the
  exception, the ported SDK under `F4SEVR`.
- **Globals,** one each by design: `f4cf::g_mod`, `vrcf::VRControllers`, `vrcf::VRControllersSuppress`,
  `vrcf::VRHaptics`, `vrui::g_uiManager`.
- **Style:** clang-format (180 columns, 4-space indent, CRLF), applied by
  `pre-commit run --files <changed files>`.
- **Addresses:** RVAs, offsets and vtable indices are documented in the modding reference library's
  `Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md`. The authority runs f4sevr 0.6.21 headers, then
  CommonLibF4VR's address library, then the `_RE_REFERENCE.md` files.
