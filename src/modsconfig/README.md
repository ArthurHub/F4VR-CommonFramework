# `modsconfig/` — Mods Config VR client

[Mods Config VR](https://github.com/ArthurHub/F4VR-Mods-Config) is an in-game configuration menu for
Fallout 4 VR mods. A mod ships a config spec, a `spec.json` that describes its settings, and its DLL
can be a native client of the menu, which adds what only the mod's code knows. This folder is the
framework's side of the menu's native API.

> Part of the [F4VR Common Framework](../README.md) source tree.

## One client per mod

`ModBase` registers the mod as a client at F4SE's `kPostLoad`, under `Settings::name`, when Mods
Config VR is installed. The menu matches it to the config spec whose `modName` is that name, without
regard to case. A mod does nothing to get it; set `Settings::modsConfigClient = false` in the
constructor to opt out. When Mods Config VR isn't installed the mod logs one line and carries on.

A name has one client. A mod that registers a client of its own under its name gets none, and the
framework's registration is refused, with a warning in Mods Config VR's log.

## The API

`ModsConfigClient::get()` holds the API and the mod's handle in it, from `kPostLoad` on:

```cpp
#include "modsconfig/ModsConfigClient.h"

// in onGameLoaded(): the options of the choice of the setting "preset" of the mod's config spec,
// which has "listFromClientCallback": true
const auto& modsConfig = f4cf::modsconfig::ModsConfigClient::get();
if (modsConfig.isRegistered()) {
    modsConfig.api()->registerOptions(modsConfig.client(), "preset", [](void*, mods_config::api::OptionsCall* call) {
        const mods_config::api::SettingValue value{ .type = mods_config::api::ValueType::String, .text = "default" };
        call->addOption(call, &value, "Default preset");
    }, nullptr);
}
```

`api()` and `client()` are null while Mods Config VR is not installed, before `kPostLoad`, and for a
mod that opts out.

The calls, the rules of what passes between the DLLs and the versions are in the client header,
`ModsConfigVRApi.h`, and in Mods Config VR's [Native API](https://github.com/ArthurHub/F4VR-Mods-Config/blob/main/docs/design/architecture.md#native-api).
Every call is on the game's main thread, and a callback the menu calls that throws or faults cuts
the mod's client off until the game is restarted.

## Vendored header

`ModsConfigVRApi.h` is Mods Config VR's client header, copied verbatim from its `src/api/`: never
edit or reformat it here. It has an include guard of its own, so a mod that has its own copy beside
the framework's compiles with both.
