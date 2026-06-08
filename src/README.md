# `src/` — F4VR Common Framework source

This is the source tree of the **F4VR Common Framework**, a static library for building
[Fallout 4 VR](https://store.steampowered.com/app/611660/) F4SE plugins on top of
[CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).

If you are *consuming* the framework to write a mod, start with the repo
[README](../README.md) and the [wiki](https://github.com/ArthurHub/F4VR-CommonFramework/wiki),
then come back here when you need to know what a specific subsystem offers. Each subfolder has its
own README with the details and a small usage snippet.

## Layout

| Folder / File | Namespace | What it is |
|---------------|-----------|------------|
| [`ModBase`](ModBase.h) | `f4cf` | Base class every mod derives from. Owns the F4SE lifecycle, logging init, and the per-frame update hook. |
| [`ConfigBase`](ConfigBase.h) | `f4cf` | INI-backed config with hot-reload, embedded-default extraction, and version migration. |
| [`Logger.h`](Logger.h) | `f4cf::logger` | spdlog wrapper. `log_trace` / `log_debug` / `log_info` / `log_warn` / `log_error` macros + sampling. |
| [`common/`](common/README.md) | `f4cf::common` | Math (quaternions, matrices, transforms) and engine-agnostic utilities (strings, files, resources, time). |
| [`f4vr/`](f4vr/README.md) | `f4cf::f4vr` | Fallout 4 VR game-state utilities: nodes, skeleton, player nodes, menus, Scaleform, thumbstick. |
| [`f4sevr/`](f4sevr/README.md) | `F4SEVR` | Ported F4SE VR SDK: Papyrus VM interop, native-function registration, VM value/arg marshalling. |
| [`vrcf/`](vrcf/README.md) | `f4cf::vrcf` | VR Controller Framework. OpenVR button/trigger/thumbstick state + input suppression. |
| [`vrui/`](vrui/README.md) | `f4cf::vrui` | VR UI widget system: panels, buttons, toggles, containers, scene graph, input dispatch. |
| [`PCH.h`](PCH.h) | — | Precompiled header, included implicitly in every translation unit. |
| [`MainLoopHook.h`](MainLoopHook.h) | `f4cf` | Trampoline into the game main loop that drives `ModBase::onFrameUpdate`. |
| [`DebugAdjuster.h`](DebugAdjuster.h) | `f4cf` | Live-tune a transform / hand pose / flow flags at runtime via controller input + INI reload. |

## How the pieces fit together

```
  Your mod (extends ModBase)
        │
        ├── ConfigBase ........ INI load/save, hot-reload, [Debug] keys
        ├── Logger ............ log_info("...") etc.
        │
        └── onFrameUpdate()  ◄── MainLoopHook (game frame)
              ├── vrcf::VRControllers ........... read controller input
              ├── vrcf::VRControllersSuppress ... hide input from the game
              ├── vrui::UIManager ............... drive + render the VR UI
              └── f4vr::* / common::* ........... game state + math helpers
```

## Conventions

- **Root namespace is `f4cf`.** Subsystems live in nested namespaces (`f4cf::common`, `f4cf::f4vr`,
  `f4cf::vrcf`, `f4cf::vrui`). The exception is [`f4sevr/`](f4sevr/README.md), which hosts the
  ported F4SE VR SDK under `namespace F4SEVR`.
- **Globals.** A handful of subsystems expose a single global instance by design:
  `f4cf::g_mod`, `f4cf::vrcf::VRControllers`, `f4cf::vrcf::VRControllersSuppress`, and
  `f4cf::vrui::g_uiManager`.
- **Style.** clang-format (LLVM-based, 180 cols, 4-space indent, CRLF, braces on new lines). See the
  repo [CLAUDE.md](../CLAUDE.md) and [.clang-format](../.clang-format).

## Reverse-engineering reference

RVAs, struct offsets, and vtable indices used here are documented in the modding reference library
(`Analysis/gold/F4VR-CommonFramework_RE_REFERENCE.md`). Authority order:
f4sevr 0.6.21 headers → CommonLibF4VR AddressLib → individual `_RE_REFERENCE.md` files.
