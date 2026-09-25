# Changelog

The notable changes in each version of F4VR-CommonFramework. Only the big things are listed:
new modules and capabilities, and changes a consuming mod has to act on. Small fixes, refactors
and tooling tweaks are left to the git history.

Versions follow `set(VERSION ...)` in the root [`CMakeLists.txt`](../CMakeLists.txt). While the
major version is `0`, a minor bump may break the API; the **Upgrading** notes say what to change.

## 0.4.0 — unreleased

**Rendering in VR, built into the framework.**

- **`f4cf::render`**: one shared OpenVR `Submit` hook that every framework overlay draws through,
  with lines, filled triangles, game-texture images and text in an embedded Roboto
  distance-field font. It can capture the engine's scene depth so overlays are hidden behind world
  geometry instead of always drawing on top ([design](tech/scene-depth-occlusion.md)).
- **`f4cf::debug`**: an in-world debug-draw overlay (shapes, labels, and a watch table in front of
  the HMD) for seeing what a mod is doing live ([design](tech/debug-draw-overlay.md)).
- **vrui panels**: UI elements drawn by the primitive renderer instead of one NIF per button:
  `UITextPanel`, `UIImagePanel`, `UIButtonPanel`, `UIToggleButtonPanel` and
  `UIMultiStateToggleButtonPanel`. They size themselves to their content, wrap text, share one
  panel style, and can show controller prompts generated from the actual input binding.
- **`f4cf::imgui`**: Dear ImGui windows drawn as world-space panels that fit into vrui layouts
  (`UIImGuiPanel`). Controlled by the `F4CF_WITH_IMGUI_UI` build option (on by default).
- **Activation spheres**: `WandActivationSphere` is now driven by a `WandActivationConfig` loaded
  from one INI section: a zone with an optional power-armor variant, two bindings, suppression you
  opt into per binding, configurable haptics, and a visual shown never / always / when inside /
  when available. The visual is a single mesh styled at runtime (preset + per-value overrides),
  and a zone can also be marked by an icon.
- **Player turning**: `f4vr::PlayerRotation` snap/smooth-turns the player the way the game's VR
  comfort settings say, even where vanilla turning is taken away ([design](tech/vr-player-rotation.md)).
- **Game-state helpers**: actor range and projectile queries, Havok collision layers, actor
  perception / combat / light-level access, live light refresh entry points, and
  `getVRPlayerNodes()` typed by CommonLibF4.
- **Input**: `VRControllers.setControllerStateAdjuster()` lets a mod correct the polled controller
  state when another mod remaps it for every reader (e.g. ROCK's trigger remap).

**Upgrading**

- Assets the framework ships now live in an `f4cf` folder inside the mod's own folders (sphere
  visuals, binding prompt icons, shared vrui icons). Re-copy them from `mod-template/`.
- The per-look sphere NIF variants are replaced by one mesh plus an `sSphereStyle` preset.
- A mod using `f4cf::imgui` must declare `imgui` (feature `dx11-binding`) in its own `vcpkg.json`.
  The mod template already does.
- **Deprecated, removed in 0.5.0:** `f4vr::PlayerNodes` and `getPlayerNodes()`. Use
  `getVRPlayerNodes()`, which returns CommonLibF4's `RE::VRPlayerNodes`. That struct is the same
  table, but its member names are camelCase and some are corrected (e.g. `HmdNode` → `hmdNode`,
  `SecondaryWandNode` → `secondaryWandNode`, `unk750` → `equippedWeaponNode`).

## 0.3.0 — 2026-07-02

**Configurable input, haptics, and in-game tuning.**

- **License**: relicensed from MIT to **GPL-3.0-or-later**, and `mod-template/` along with it.
- **CommonLibF4VR types throughout**: the framework no longer uses F4SEVR's `Forms.h`. All
  game types are CommonLibF4VR `RE::` types.
- **Input bindings**: `InputBinding` + `VRControllersManager::check()` evaluate a binding
  described as data (hand, tap / press / long-press / double-press, button or axis, cross-hand
  modifier), parsed from a readable INI string ([guide](input-binding.md)).
- **Input suppression**: `VRControllersSuppressor` hides chosen buttons and axes from the game
  and other mods while a gesture owns them.
- **Haptics**: `VRControllersHaptic` with a library of named haptic patterns.
- **Interaction and state helpers**: `WandActivationSphere` (a proximity zone for gestures) and
  `EquippedWeaponHandler` (tracks equipped-weapon changes).
- **Config**: `NiTransform` and hand-pose INI values, INI change subscriptions, and
  session-only value overrides.
- **Debugging**: `DebugAdjuster` for tuning transforms, hand poses and INI values in-game with the
  controllers, `DebugInventory` for adding items in bulk, `PerfMonitor` for profiling hot paths,
  and a guide to the `[Debug]` settings ([guide](debug-config.md)).
- **vrui**: elements sized from their real texture dimensions, a disabled state, and atlas
  pack/unpack tools in `nif-tools`.
- **Main loop**: early and late frame-update hooks, so a mod that moves the body (FRIK) can run
  before the others.
- **Build**: moved to Visual Studio 2026, with CI build and maintenance workflows.

## 0.2.0 — 2025-11-27

**A reusable library with a mod template.**

- Built on **CommonLibF4VR** instead of CommonLibF4.
- A CMake target that a mod adds with `add_subdirectory(...)`, rather than a DLL.
- **`ModBase`**: the shared plugin lifecycle (`onModLoaded` / `onGameLoaded` /
  `onGameSessionLoaded` / `onFrameUpdate`) on a common main-loop hook, with exception handling.
- **`mod-template/`**: a starting point for a new mod.
- **vrui**: a multi-state toggle button, and layout values read from config so a UI can be tuned
  without rebuilding.

**Upgrading**

- Everything moved under the `f4cf` namespace (`f4cf::f4vr`, `f4cf::vrcf`, `f4cf::vrui`), and
  the controller and UI code moved to `vrcf/` and `vrui/`.

## 0.1.0 — 2025-09-09

- First version: the common code built up across FRIK and other mods (`common`, `f4vr`,
  `f4sevr`, `ui`), collected into one repository.
