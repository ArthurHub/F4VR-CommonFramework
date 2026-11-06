# `modsconfig/` — Mods Config FO4 VR client

[Mods Config FO4 VR](https://github.com/ArthurHub/F4VR-Mods-Config) is an in-game settings menu for
Fallout 4 VR mods. A mod ships a config spec (`spec.json`) describing its settings, and its DLL can be a
native client of the menu, adding what only its code knows. This folder is the framework's side of that
API.

> Part of the [F4VR Common Framework](../README.md) source tree.

## One client per mod

`ModBase` connects the client at F4SE's `kPostLoad`, under `Settings::name`, when Mods Config FO4 VR is
installed; the menu matches it to the config spec whose `modName` is that name, in any case. The mod does
nothing for it (`Settings::modsConfigClient = false` opts out). Without the menu, the mod logs one line.

- **`modVersion`,** a client state from `Settings::version`: a config spec shows it with
  `{clientState:modVersion}`, or tests it in a `clientState` condition.
- **`configFile`,** a client state with the absolute path of the config file the mod loaded
  (`ConfigBase::getConfigFilePath()`). A mod that switches files at runtime, as FRIK loads
  `FRIK_FOLVR.toml` with Fallout London VR, has its spec's file take its path from it, so the menu edits
  the file the mod loaded:

  ```json
  "main": { "format": "toml", "root": "myGames", "path": "Mods_Config/FRIK/FRIK.toml", "pathFromClientState": "configFile" }
  ```

- **`Settings::modsConfigMinVersion`** is the oldest menu API the mod works with, 1 by default. A mod
  that uses something newer checks first, `ModsConfigFO4Client::get().apiVersion() >= 2`; one that
  can't work at all with an older menu sets the setting, and isn't connected to one, with a warning.
- **One client per name:** a mod that connects a client of its own under its name gets none, and the
  framework's is refused, with a warning in the menu's log.

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

// the mod's own UI hands an edit back to the menu as a pending value, by the setting's id, which
// the menu's Save writes
modsConfig.setPendingValue("fScale:Main", 1.5f);
```

- **Names** of functions, states and options are registered once: the first stays. A quick launch
  entry adds its function, under its name.
- **A function returns at once.** Longer work goes on after it, and a state tells the rows that it runs.
- **A state's setter** applies what the menu gives it: each preview of a `live` setting, its Discard,
  and the value a Save wrote. It doesn't persist: a mod that keeps the value writes it from
  `subscribeToSaves`. A state with no setter is a `readOnly` setting's.
- **`minimizeMenu` / `restoreMenu`** move the menu aside and back, from any flow of the mod.
  `restoreMenu` does nothing unless the menu is minimized.
- **`setPendingValue`** hands an edit of the mod's own UI to the menu as a setting's pending value, in
  its valueType, so the menu's Save writes it. The menu takes it while it shows the mod, shown or
  minimized, but not from a `subscribeToChanges` or `subscribeToSaves` handler. The mod applies the value
  before it hands it over: a `live` one comes back during the call marked `handedOver`, which the live
  preview records with no reload, so a mod can hand over a value every frame.
- **`subscribeToChanges` / `subscribeToSaves`** tell each change of a `live` setting and what each Save
  wrote, for what the overrides don't cover ([live preview](#live-preview-of-the-config)).
- **`api()`** is the menu's interface, for what the class doesn't wrap; null until connected.
- **Game thread:** everything runs on it. A callback that throws or faults cuts the mod's client off
  until the game restarts.

The calls, what may pass between the DLLs and the versions are in the client header and the menu's
[Native API](https://github.com/ArthurHub/F4VR-Mods-Config/blob/main/docs/design/architecture.md#native-api).

## Live preview of the config

A `live` setting kept in the mod's own config file is previewed with no code of the mod's.
`ConfigPreviews`, which `ModBase` subscribes to the client, turns the menu's changes into session
overrides, under the owner `ModsConfigFO4VR`:

- **A preview** sets the override of the value's path (`Main` and `fScale` for `["Main", "fScale"]`), and
  the config reloads as for a change of the file.
- **A Discard** clears it, and the file's value applies again.
- **A Save** clears the overrides of what it wrote, with one reload that reads the file.
- **A change the mod handed over** (`handedOver`, from its own `setPendingValue`) sets or clears the override
  with no reload: the mod has the value already, and its values not handed over yet stay.

So a value the mod reads on each load previews live; one read only at startup doesn't, and its setting
says `restart`. A setting of another file or of the game is the mod's, through `subscribeToChanges` and
`subscribeToSaves`. The overrides of devbench and the mod's API callers stay.

## Vendored header

`ModsConfigFO4Api.h` is the menu's client header, copied verbatim from its `src/api/`: never edit or
reformat it here. It holds the interfaces between the DLLs and the C++ client over them, compiled into
the mod, and has its own include guard, so a mod with its own copy compiles with both.
