# `devbench/` — devbench Integration

[devbench](https://github.com/ArthurHub/devbench) is a separate F4SE plugin that runs a local MCP +
REST server inside the game, so an AI agent or a script can inspect and drive it. Other plugins add
their own tools to it through a small C ABI. This folder is the framework's side of that ABI.

> Part of the [F4VR Common Framework](../README.md) source tree.

## One tool per mod

`ModBase` registers one devbench tool for the mod once the game has loaded, named after
`Settings::name` in lowercase (`FRIK` → `frik`). A mod does nothing to get it; set
`Settings::devbenchTool = false` to opt out. When devbench isn't installed the mod logs one line
and carries on.

Everything the tool does is an **action**, chosen by its required `action` argument. There is no
default, so a call that names none is an error and never arms the tool. Every framework mod has the
same generic actions:

| Action | Arguments | What it does |
|--------|-----------|--------------|
| `health` | — | Which mod and framework version this is, the tool's `contract` number, the devbench build, whether the tool is armed, and its actions. Answered without the game thread, so it replies while the game is stalled. |
| `config` | `key`, `section` | One INI value as the mod sees it: the session override if one is set, otherwise the file's. |
| `set` | `key`, `value`, `section` | Override one INI value for the session (`ConfigBase::setConfigOverride`); the file is not written. |
| `clear` | `key`, `section`, or `all` | Drop one session override, or all of them. |
| `overrides` | — | The session overrides in effect. |

`section` defaults to the mod's name, the section the mod template uses; a mod whose main section
is named differently says so with `devbench::setDefaultConfigSection`.

A mod adds its own actions and the opening line of the tool's description:

```cpp
#include "devbench/DevBench.h"

void MyMod::onModLoaded(const F4SE::LoadInterface*)
{
    // an agent picks the mod's tool by this line, so name the features someone would ask about
    devbench::setToolDescription("MyMod: swimming comfort for VR - reduced bob, auto-surface, stamina");
    devbench::addAction({
        .name = "surface",
        .description = "float the player to the surface now",
        .arguments = { { "speed", { { "type", "number" }, { "description", "surface: units per second, default 50" } } } },
        .handler = [](const nlohmann::json& args) -> nlohmann::json {
            startSurfacing(args.value("speed", 50.0f));
            return { { "surfacing", true } };
        },
    });
}
```

- **Threads.** Devbench calls the tool on its own listener thread. An action runs on the game
  thread by default: it is queued, run at the start of the next frame right before the mod's
  `onFrameUpdate`, and the caller waits up to 2 seconds for it. `RunOn::Listener` runs it at once
  instead, for answers that must work while the game is stalled; such a handler may only read
  atomics and data that no longer changes. A mod without `setupMainGameLoop` has no frame to run
  the queue in, so its actions go through F4SE's task interface.
- **Answers.** A handler returns a JSON object and gets `"ok": true` added; throwing turns the
  call into `{ "ok": false, "error": "<message>" }`, which carries no other keys.
- **Arming.** The first call of any action but `health` arms the tool (`devbench::isArmed()`) for
  the rest of the session, and anything that costs per-frame work waits for it. `health` never
  arms, so probing every mod is free.
- **Arguments.** All actions share one flat input schema, so start each argument's description
  with the actions that read it (`"surface: ..."`). An argument several actions share is declared
  once; the first declaration wins.
- **Adding late.** Actions and the description added before `onGameLoaded` returns go out in
  the first registration; a later change re-registers the tool.

### Several mods at once

Each mod DLL links its own copy of the framework, so each has its own tool, queue and config, and
they can't see each other. What they share is devbench's list of tool names, where registering a
name that exists **silently replaces** the earlier tool. Deriving the name from `Settings::name`,
which is also the DLL's name, keeps them unique; the framework logs a warning when its
registration replaced something. The generic actions are identical in every mod, and `health`'s
`framework` and `contract` fields say which version of them a mod has.

Only `GetBuildNumber` and `RegisterTool` are used, slots every devbench has, so a mod works with
any devbench version. A later slot would have to be gated on `GetBuildNumber()` (see below).

## Vendored client API

| File | Contents |
|------|----------|
| [`DevBenchAPI.h`](DevBenchAPI.h) | The cross-plugin interface: `IDevBenchInterface001` (register tools, emit events), the handler types, and `GetDevBenchInterface001()`. |
| [`DevBenchAPI.cpp`](DevBenchAPI.cpp) | `GetDevBenchInterface001()`: the F4SE messaging handshake, with a `GetProcAddress` fallback on `devbench.dll`. |
| [`DevBenchAPI.LICENSE.txt`](DevBenchAPI.LICENSE.txt) | MIT license for these two files only; devbench itself is GPL-3.0. |

They are devbench's `include/` files, copied byte for byte from devbench **1.22.0**
([ArthurHub/devbench](https://github.com/ArthurHub/devbench) `cdf903e`). They stay unmodified
so that an update is a plain copy that can be diffed against upstream:

- Don't edit or reformat them. `.pre-commit-config.yaml` excludes them from every hook for that reason.
- To update, copy all three files from devbench's `include/` over these and note the new version
  and commit here.

The ABI is append-only: a vtable slot added in a later devbench exists only on hosts at least that
new, so check `GetBuildNumber()` before calling one. Every slot says which build introduced it.
