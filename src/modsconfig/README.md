# `modsconfig/` — Mods Config FO4 VR client

[Mods Config FO4 VR](https://github.com/ArthurHub/F4VR-Mods-Config) is an in-game configuration menu for
Fallout 4 VR mods. A mod ships a config spec, a `spec.json` that describes its settings, and its DLL
can be a native client of the menu, which adds what only the mod's code knows. This folder is the
framework's side of the menu's native API.

> Part of the [F4VR Common Framework](../README.md) source tree.

## One client per mod

`ModBase` connects the mod's client at F4SE's `kPostLoad`, under `Settings::name`, when Mods Config
VR is installed. The menu matches it to the config spec whose `modName` is that name, without regard
to case. A mod does nothing to get it; set `Settings::modsConfigClient = false` in the constructor to
opt out. When Mods Config FO4 VR isn't installed the mod logs one line and carries on.

The framework adds the `modVersion` client state from `Settings::version` before connecting. A config
spec can show the mod's version with `{clientState:modVersion}` in its text, without registering a
state of its own. The state can also be used in a `clientState` condition.

It adds the `configFile` client state too, the absolute path of the config file the mod loaded, as
`ConfigBase::getConfigFilePath()` has it. A mod that loads another file at runtime, as FRIK loads
`FRIK_FOLVR.toml` with Fallout London VR (`switchConfigFile`), has the file of its config spec take
its path from it, so the menu edits the file the mod loaded, with no code of the mod's:

```json
"main": { "format": "toml", "root": "myGames", "path": "Mods_Config/FRIK/FRIK.toml", "pathFromClientState": "configFile" }
```

`Settings::modsConfigMinVersion` is the least version of Mods Config FO4 VR's API the mod works with,
1 by default, which connects to any. A mod that uses what a later version added asks the client
first, and skips it with an older Mods Config FO4 VR:

```cpp
if (mods_config::api::ModsConfigFO4Client::get().apiVersion() >= 2) {
    // what version 2 added
}
```

A mod that does not work at all with an older one sets the setting to the version it needs, and an
older Mods Config FO4 VR is not connected to, with a warning in the mod's log.

A name has one client. A mod that connects a client of its own under its name gets none, and the
framework's is refused, with a warning in Mods Config FO4 VR's log.

## The client

The client is `mods_config::api::ModsConfigFO4Client`, of the client header. A mod adds what the menu
calls to it in C++, at any time: what is added before `kPostLoad` is given to the menu as the client
connects. While Mods Config FO4 VR is not installed nothing is called.

```cpp
#include "modsconfig/ModsConfigFO4Api.h"

using namespace mods_config::api;
auto& modsConfig = ModsConfigFO4Client::get();

// a function that a button of the config spec runs: { "type": "CallNativeFunction", "function": "calibrate",
// "params": { "samples": 30 } }. Its message is the result in the menu's help line
modsConfig.addFunction("calibrate", [](const Params& params) {
    const float height = measureHeight(params.getInt("samples", 10));
    return FunctionResult{ .done = true, .message = std::format("Done: {:.2f} m.", height) };
});

// a state that a condition of the config spec asks: { "clientState": "calibrating", "equals": false }
modsConfig.addState("calibrating", [] { return std::optional<Value>(isCalibrating()); });

// a state that a setting of the config spec is kept in: "storage": { "clientState": "pipboyType" }. The
// setter applies the value in the mod as the menu changes it
modsConfig.addState("pipboyType", [] { return std::optional<Value>(pipboyType()); },
    [](const Value& value) { setPipboyType(std::get<int>(value)); });

// the options of a choice with "listFromClientCallback": true, by the setting's id
modsConfig.addOptions("preset", [] { return std::vector<Option>{ { .value = std::string("default"), .text = "Default" } }; });

// an entry of the quick launch section and the function a press runs, by its name, which a button
// can run too. A function that opens the mod's own UI minimizes the menu, and the mod shows it again
// as the UI closes
modsConfig.addQuickLaunch("openPipboyConfig", { .text = "FRIK: Pip-Boy", .help = "Moves the Pip-Boy on your wrist.", .buttonText = "Place" },
    [&modsConfig](const Params&) {
        if (!isSkeletonReady()) {
            return FunctionResult{ .done = false, .message = "The body is not ready yet." };
        }
        openPipboyConfig([&modsConfig] { modsConfig.restoreMenu(); });
        modsConfig.minimizeMenu();
        return FunctionResult{ .done = true };
    });

// a quick action: a function that does its work, whose message the help line tells
modsConfig.addQuickLaunch("recalibrate", { .text = "Recalibrate", .help = "Measures your height again.", .buttonText = "Run" },
    [](const Params&) { return FunctionResult{ .done = true, .message = "Calibrated." }; });

// the menu's events, and the page of the config spec that is shown
modsConfig.subscribeToMenuEvents([](const MenuEventType event, std::string_view pageId) { /* ... */ });
const bool spheresShown = modsConfig.shownPage() == "spheres";

// the mod's wrist button opens its settings in the menu, on a page by its id
modsConfig.openMenu("body");
```

- A name of a function, a state and a setting's options is registered once: the first stays. A
  quick launch entry adds its function, so its name is a function's.
- `minimizeMenu` and `restoreMenu` move the menu out of the way and back, from any flow of the mod.
  `restoreMenu` does nothing while the menu is not minimized, as the player showed or closed it.
- A function returns at once: work that takes longer goes on after it, and a state tells the rows
  that it runs.
- A state's setter applies a value the menu gives it: each preview of a `live` setting, its Discard
  back to the value before, and the value a Save wrote. It does not persist: a mod that keeps the
  value writes it from `subscribeToSaves`. A state with no setter is a `readOnly` setting's.
- `api()` gives the menu's interface, for what the class does not wrap. It is null until the client
  connects.

- `subscribeToChanges` and `subscribeToSaves` tell a mod each change of a `live` setting and what
  each Save wrote, for what the overrides do not do, see [Live preview of the config](#live-preview-of-the-config).

The calls, the rules of what passes between the DLLs and the versions are in the client header,
and in Mods Config FO4 VR's [Native API](https://github.com/ArthurHub/F4VR-Mods-Config/blob/main/docs/design/architecture.md#native-api).
Everything runs on the game's main thread, and a callback the menu calls that throws or faults cuts
the mod's client off until the game is restarted.

## Live preview of the config

A setting of the mod's config spec whose `applies` is `live`, and that is kept in the mod's own
config file, is previewed with no code of the mod's. `ConfigPreviews`, which `ModBase` subscribes to
the client, turns the menu's changes into session overrides of the config, under the owner
`ModsConfigFO4VR`:

- A preview sets the override of the value's path, `Main` and `fScale` for `["Main", "fScale"]`, and
  the config reloads the mod's values as a change of the file does.
- A discard clears it, and the file's value applies again.
- A Save clears the overrides of the settings it wrote, with one reload that reads the file, which
  has their values by then.

So a value the mod reads as its config loads is previewed as it is changed. One that the mod reads
only once, as the game starts, is not, and its setting says `restart`. A setting of another file, or
of the game, is left to the mod: its handlers of `subscribeToChanges` and `subscribeToSaves` see every
change and every Save. The overrides of the devbench tool and of the mod's API callers stay.

## Vendored header

`ModsConfigFO4Api.h` is Mods Config FO4 VR's client header, copied verbatim from its `src/api/`: never
edit or reformat it here. It is two layers: the interfaces that pass between the DLLs, the menu's and
the client's, and the C++ client over them, which is compiled into the mod. It has an include guard of its own, so a mod that has
its own copy beside the framework's compiles with both.
