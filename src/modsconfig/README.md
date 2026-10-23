# `modsconfig/` — Mods Config VR client

[Mods Config VR](https://github.com/ArthurHub/F4VR-Mods-Config) is an in-game configuration menu for
Fallout 4 VR mods. A mod ships a config spec, a `spec.json` that describes its settings, and its DLL
can be a native client of the menu, which adds what only the mod's code knows. This folder is the
framework's side of the menu's native API.

> Part of the [F4VR Common Framework](../README.md) source tree.

## One client per mod

`ModBase` connects the mod's client at F4SE's `kPostLoad`, under `Settings::name`, when Mods Config
VR is installed. The menu matches it to the config spec whose `modName` is that name, without regard
to case. A mod does nothing to get it; set `Settings::modsConfigClient = false` in the constructor to
opt out. When Mods Config VR isn't installed the mod logs one line and carries on.

The framework adds the `modVersion` client state from `Settings::version` before connecting. A config
spec can show the mod's version with `{clientState:modVersion}` in its text, without registering a
state of its own. The state can also be used in a `clientState` condition.

`Settings::modsConfigMinVersion` is the least version of Mods Config VR's API the mod works with,
1 by default, which connects to any. A mod that uses what a later version added asks the client
first, and skips it with an older Mods Config VR:

```cpp
if (mods_config::api::ModsConfigVRClient::get().apiVersion() >= 2) {
    // what version 2 added
}
```

A mod that does not work at all with an older one sets the setting to the version it needs, and an
older Mods Config VR is not connected to, with a warning in the mod's log.

A name has one client. A mod that connects a client of its own under its name gets none, and the
framework's is refused, with a warning in Mods Config VR's log.

## The client

The client is `mods_config::api::ModsConfigVRClient`, of the client header. A mod adds what the menu
calls to it in C++, at any time: what is added before `kPostLoad` is given to the menu as the client
connects. While Mods Config VR is not installed nothing is called.

```cpp
#include "modsconfig/ModsConfigVRApi.h"

using namespace mods_config::api;
auto& modsConfig = ModsConfigVRClient::get();

// a function that a button of the config spec runs: { "type": "CallNativeFunction", "function": "calibrate",
// "params": { "samples": 30 } }. Its message is the result in the menu's help line
modsConfig.addFunction("calibrate", [](const Params& params) {
    const float height = measureHeight(params.getInt("samples", 10));
    return FunctionResult{ .done = true, .message = std::format("Done: {:.2f} m.", height) };
});

// a state that a condition of the config spec asks: { "clientState": "calibrating", "equals": false }
modsConfig.addState("calibrating", [] { return std::optional<Value>(isCalibrating()); });

// the options of a choice with "listFromClientCallback": true, by the setting's id
modsConfig.addOptions("preset", [] { return std::vector<Option>{ { .value = std::string("default"), .text = "Default" } }; });

// the mod's own config UI: a function of its name, which a button can run too, and an entry of the
// quick launch section. The menu is minimized while it is open, and shown again as it closes
modsConfig.addConfigUI("openPipboyConfig", { .text = "FRIK: Pip-Boy", .help = "Moves the Pip-Boy on your wrist.", .buttonText = "Place" },
    [](std::string& message) {
        if (!isSkeletonReady()) {
            message = "The body is not ready yet.";
            return false;
        }
        openPipboyConfig([] { ModsConfigVRClient::get().configUIClosed(); });
        return true;
    });

// the menu's events, and the page of the config spec that is shown
modsConfig.subscribeToMenuEvents([](const MenuEventType event, std::string_view pageId) { /* ... */ });
const bool spheresShown = modsConfig.shownPage() == "spheres";

// the mod's wrist button opens its settings in the menu, on a page by its id
modsConfig.openMenu("body");
```

- A name of a function, a state, a config UI and a setting's options is registered once: the first
  stays. A config UI is a function, so its name is a function's.
- A function returns at once: work that takes longer goes on after it, and a state tells the rows
  that it runs.
- `api()` gives the menu's interface, for what the class does not wrap. It is null until the client
  connects.

The calls, the rules of what passes between the DLLs and the versions are in the client header,
and in Mods Config VR's [Native API](https://github.com/ArthurHub/F4VR-Mods-Config/blob/main/docs/design/architecture.md#native-api).
Everything runs on the game's main thread, and a callback the menu calls that throws or faults cuts
the mod's client off until the game is restarted.

## Vendored header

`ModsConfigVRApi.h` is Mods Config VR's client header, copied verbatim from its `src/api/`: never
edit or reformat it here. It is two layers: the interfaces that pass between the DLLs, the menu's and
the client's, and the C++ client over them, which is compiled into the mod. It has an include guard of its own, so a mod that has
its own copy beside the framework's compiles with both.
