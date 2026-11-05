# Debug settings

Every mod built on F4VR-CommonFramework has a `[Debug]` section in its config file, with the same keys in
every mod. A player usually needs only the log level, to report a problem. The rest is for testing and
for mod authors.

- **File:** `Documents\My Games\Fallout4VR\Mods_Config\<ModName>\<ModName>.toml`, under `[Debug]`.
- **Log:** `Documents\My Games\Fallout4VR\F4SE\<ModName>.log`.
- **Live:** save the file while the game runs and the change applies at once.
- **Mistakes:** a typo that breaks the file is logged with its line, and every setting keeps its value
  until it is fixed.

## Quick start

- **A more detailed log, for a bug report:** `iLogLevel = 1`, or `0` for everything. Set it back to `2`
  afterwards, as `0` fills the log fast.
- **A dump a mod author asked for:** `aDumpDataOnce = ["skelly"]`. It is written to the log once.
- **Items, for testing:** `aAddItemsOnce = [{ op = "print", category = "weapons", name = "laser" }]`
  lists them in the log, and `op = "get"` adds them. Keep a save first.

## The keys

| Key                                                    | What it does                                          |
| ------------------------------------------------------ | ----------------------------------------------------- |
| **For everyone**                                       |                                                       |
| [`iLogLevel`](#iloglevel)                              | How much goes into the log                            |
| [`aDumpDataOnce`](#adumpdataonce)                      | Writes game-state dumps to the log, once              |
| [`aAddItemsOnce`](#aadditemsonce)                      | Adds items, or lists them in the log, once            |
| **Support switches**: leave them unless asked          |                                                       |
| [`sSceneDepthStrategy`, `bSceneDepthDiagnostics`](#scene-depth-occlusion) | How overlays hide behind the world |
| [`sOverlayDrawPass`](#soverlaydrawpass)                | Overlays under the game's menus, or on top            |
| [`bVRUIShowFingerTip`](#bvruishowfingertip)            | Marks the point that presses VR UI buttons            |
| **For mod authors**                                    |                                                       |
| [`sAdjustTarget`](#sadjusttarget)                      | Tunes a transform, hand pose or number with the sticks |
| [`fFlowFlag1/2/3`, `sFlowText1/2`](#flow-values)       | Values the mod's code reads, for experiments          |
| [`sLogPattern`](#slogpattern)                          | The layout of each log line                           |
| [`bVRUIDevLayout`](#bvruidevlayout)                    | Tunes the VR UI's layout in a file                    |
| `bDebugDrawEnabled` and the other `DebugDraw` keys     | The in-world debug overlay, see [`src/debug/README.md`](../src/debug/README.md) |
| [`iVersion`](#iversion)                                | The config's version. Never change it                 |

---

## For everyone

### `iLogLevel`

| Value | Logs                                   |
| ----- | -------------------------------------- |
| `0`   | Everything (trace), for a short session |
| `1`   | Debug detail                           |
| `2`   | Normal use. The default                |

### `aDumpDataOnce`

A list of dumps to write to the log. Each runs once, and its name is then taken out of the list, in the
file too.

| Name         | Dumps                                                    |
| ------------ | -------------------------------------------------------- |
| `ui_tree`    | The mod's VR UI elements                                 |
| `skelly`     | The player's skeleton                                    |
| `fp_skelly`  | The first-person skeleton                                |
| `geometry`   | The player's geometry, and what is shown or hidden       |
| `pipboy`     | The Pip-Boy's nodes                                      |
| `world`      | The player and weapon part of the world                  |
| `all_nodes`  | The whole scene. **Huge**                                |
| `perf`       | What each part of the frame cost, since the last reset   |
| `perf_reset` | Like `perf`, then starts a new window                    |

A mod can add names of its own (FRIK has `weapon_pos`, `weapon_muzzle` and `menus`). A name nothing asks
for stays in the list. Names are matched whole, in their case.

Timing records only once the mod's devbench tool arms, which it does when devbench is installed or a
Tracy viewer connects. Without either, `perf_reset` starts it: reset, hold the situation you want to
measure, then `perf`. See [`src/perf/README.md`](../src/perf/README.md).

### `aAddItemsOnce`

A list of requests, each an operation on one or more item categories, with optional filters:

```toml
[Debug]
aAddItemsOnce = [
  # list the obtainable laser guns
  { op = "print", category = "weapons", class = "gun", keyword = "laser" },
  # add the obtainable weapons, and five of each obtainable grenade and mine
  { op = "get", category = ["weapons", "throwables"] },
  # list every heavy torso or body armor, unobtainable too
  { op = "print-all", category = "armor", slot = ["torso", 33], class = "heavy" },
  # add every named aid item with "stimpak" in its name
  { op = "get-all", category = "aid", name = "stimpak" },
]
```

- **Start with `print`:** `get` can add a lot of items. Keep a save.
- **Once:** the list is emptied, in the file too, before it runs, so a request never runs twice, and a
  wrong one has to be entered again. A comment inside the list goes with it: keep notes above the key.
- **All or nothing:** every request is checked first. If one is wrong, the log says which and why, and
  none runs.

| Operation   | Does                                                                         |
| ----------- | ---------------------------------------------------------------------------- |
| `get`       | Adds the items you can normally get: from a leveled list or a recipe         |
| `get-all`   | Adds **every** matching named, playable item                                 |
| `print`     | Lists in the log what `get` would add: form ID, name, plugin, keywords       |
| `print-all` | Lists in the log what `get-all` would add                                    |

| Key        | Needed | Value                                                                    |
| ---------- | ------ | ------------------------------------------------------------------------ |
| `op`       | Yes    | The operation                                                            |
| `category` | Yes    | `weapons`, `throwables`, `ammo`, `armor`, `aid` or `misc`, or a list     |
| `name`     |        | Part of the item's name                                                  |
| `keyword`  |        | Part of one of its keywords' editor IDs                                  |
| `slot`     |        | Armor: a slot or a list of them, matching any, see below                 |
| `class`    |        | Armor: `light`, `heavy` or `none`. Weapons: `melee`, `gun` or `unarmed`  |

An item has to match every filter of its request. Values can be in any case; keys can't. A `slot` or
`class` that doesn't fit a category is skipped there with a warning, so one request can cover weapons
and armor. `get` adds one of each match, but 5 of each throwable and 50 of each ammo.

A slot is a Creation Kit number from 30 to 61, or one of these names, in quotes:

| Name                                                           | Slot                  |
| -------------------------------------------------------------- | --------------------- |
| `head`, `helmet`, `hair`                                       | 30                    |
| `body`, `outfit`, `underarmor`                                 | 33                    |
| `hands`, `lhand`, `rhand`                                      | 34, 35                |
| `chest`, `torso`                                               | 41                    |
| `arms`, `larm`, `leftarm`, `rarm`, `rightarm`                  | 42, 43                |
| `legs`, `lleg`, `leftleg`, `rleg`, `rightleg`                  | 44, 45                |
| `headband`, `eyes`, `glasses`, `mouth`, `mask`, `neck`, `ring` | 46, 47, 49, 50, 51    |

---

## Support switches

Each one ships on the right value. Change one when a mod author asks you to, to tell where a problem
comes from.

### Scene-depth occlusion

```toml
[Debug]
sSceneDepthStrategy = "auto"
bSceneDepthDiagnostics = false
```

`sSceneDepthStrategy` picks how the world's depth is found, which hides overlay panels behind the world:

- **`auto`:** the shipped one.
- **`direct`:** never resamples. Right without an upscaler, wrong with one.
- **`off`:** no occlusion, overlays draw on top of the world.
- **`depthpass`:** an alternative, kept for re-testing.

Between them they tell whether a misdrawn overlay comes from the capture, the resample, or neither.
`bSceneDepthDiagnostics` adds the capture's rows to the debug overlay and re-runs its A/B comparison.
The capture reports itself to the log anyway, so a log is usually enough. The design:
[`tech/scene-depth-occlusion.md`](tech/scene-depth-occlusion.md).

### `sOverlayDrawPass`

Where the overlays draw. Debug draw is always on top.

- **`auto`:** the shipped one. In the game's pass, under its menus, or at Submit where that pass can't be
  hooked, which the log says.
- **`submit`:** always at Submit, on top of the finished frame and the game's menus.

`submit` tells whether a misdrawn overlay comes from drawing inside the game's pass. See
[`src/render/README.md`](../src/render/README.md#under-the-games-menus).

### `bVRUIShowFingerTip`

`true` marks the point that presses VR UI buttons with a small gold sphere. Off, the sphere shows only
while no hand is drawn at the controller (no FRIK, or the hand is away from it), where the controller
itself presses.

---

## For mod authors

### `sAdjustTarget`

Tunes a value with the controllers in game, and saves it back to the file: the fast way to place a node
or pose a hand. While a target is set, the mod takes **all** controller input, so you can't move.

1. Set `sAdjustTarget` to a target and save the file.
2. After the config-reload notification, adjust it with the sticks (below).
3. **Tap primary A** to save it to the file. **Long-press primary A** to drop the changes and reload.
4. Set `sAdjustTarget = "none"` to get your controllers back.

| Target                      | Edits                                                       |
| --------------------------- | ----------------------------------------------------------- |
| `none` or empty             | Nothing: controllers back to normal                         |
| `transform`                 | `[Debug]`'s scratch `tTransform`, created on save           |
| `handpose`                  | `[Debug]`'s scratch `hHandPose`, created on save            |
| `flag1` / `flag2` / `flag3` | One flow flag                                               |
| `flag123`                   | All three flow flags                                        |
| `haptictest`                | Plays the haptic sequence in `sFlowText1`, see below        |
| `Section::Key`              | Any key, by its first letter: `t` a transform, `h` a hand pose, `f` a number |

```toml
[Debug]
sAdjustTarget = "MyMod::tWeaponOffset"
```

**Transform:** the sticks move it; an offhand button held switches to rotation or scale.

| Hold         | Primary stick up/down | Primary stick left/right | Offhand stick         |
| ------------ | --------------------- | ------------------------ | --------------------- |
| Nothing      | X                     | Y                        | Z (up/down)           |
| Offhand grip | Heading (pitch)       | Attitude (yaw)           | Roll (left/right)     |
| Offhand A    | Scale (at least 0.05) |                          |                       |

**Hand pose:** 22 numbers, `[proximal, middle, distal, splay]` for each finger, then the palm's `[pitch,
yaw]`. One slot at a time: **tap offhand A** to step thumb, index, middle, ring, pinky, palm, and around.

| Hold         | Primary stick up/down | Primary stick left/right | Offhand stick up/down |
| ------------ | --------------------- | ------------------------ | --------------------- |
| Nothing      | Proximal bend         | Middle bend              | Distal bend           |
| Offhand grip | Splay                 |                          |                       |

A finger goes from -0.8 to 2.0. On the palm, the primary stick changes pitch (up/down) and yaw
(left/right), from -10 to 15.

**Flow flags:** the primary stick up/down changes the flag. With `flag123`, primary up/down is
`fFlowFlag1`, primary left/right `fFlowFlag2`, offhand up/down `fFlowFlag3`.

#### Haptic test

`sFlowText1` is a sequence of `seconds,startIntensity,endIntensity` segments, separated by `;`, and
**tap primary A** plays it on the primary controller. Edit the file, feel it, repeat. A malformed segment
is skipped and logged.

```toml
[Debug]
sFlowText1 = "0.4,0.1,1.0; 0.1,0,0; 0.05,1,1"
sAdjustTarget = "haptictest"
```

### Flow values

`fFlowFlag1`, `fFlowFlag2`, `fFlowFlag3` (numbers) and `sFlowText1`, `sFlowText2` (text) mean nothing to
the framework: the mod's code reads them as `config->debug.flowFlag1` and so on, to switch experiments on
without a rebuild. An empty value reads as `0` or `""`. The one built-in use is `sFlowText1` for
[`haptictest`](#haptic-test).

### `sLogPattern`

Each log line's layout, in [spdlog's pattern syntax](https://github.com/gabime/spdlog/wiki/Custom-formatting):

```toml
[Debug]
sLogPattern = "%H:%M:%S.%e %l: %v"
# with the source file and line:
# sLogPattern = "%H:%M:%S.%e %L [%-25s:%-4#]: %v"
```

### `bVRUIDevLayout`

Tunes where the mod's VR UI stands, and its size, while the game runs. It isn't in the shipped config,
so add the line:

```toml
[Debug]
bVRUIDevLayout = true
```

While it is on, the mod keeps `<ModName>_DevLayout.ini` beside its config, with a section for each UI
shown and a line for each element:

```ini
[MainConfig]
MainConfig = Pos:(0.00,0.00,0.00), Rot:(0.00,0.00,0.00), Scale:(1.60), Padding:(0.35), Layout:(5)
Row1 = Pos:(0.00,0.00,0.00), Rot:(0.00,0.00,0.00), Scale:(1.00), Padding:(0.30), Layout:(1)
```

Edit a line and save, and the UI follows in a fraction of a second. A closed UI keeps its lines. The
file is removed when the key is turned off, so copy the values you want into the code. The fields, and
what a removed line does: [`src/vrui/README.md`](../src/vrui/README.md#notes).

### `iVersion`

The config's version, which the framework migrates from. Never change it, or copy it between mods.
