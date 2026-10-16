# Debug configuration

Every mod built on F4VR-CommonFramework ships a `[Debug]` section in its config file. The fields are the
same across mods: log control, a few runtime scratch values, an in-game live-tuning tool, and
one-shot diagnostic actions. This page explains each one with examples.

**Edits apply live.** The config file is watched while the game runs, so almost everything here takes effect
the moment you save the file — no restart, usually no reload of the save. Set a value, alt-tab back
in, and it's active.

> **Examples use `MyMod` as the config section.** Replace it with your mod's section name — e.g.
> `ImmersiveFlashlightVR`. The `[Debug]` keys themselves are identical in every mod.

## Fields at a glance

| Field                                                  | What it does                                                       |
| ------------------------------------------------------ | ------------------------------------------------------------------ |
| [`iLogLevel`](#iloglevel)                              | Minimum severity written to the log                                |
| [`sAdjustTarget`](#sadjusttarget--in-game-live-tuning) | Tune transforms / hand poses / floats in-game with the controllers |
| [`aDumpDataOnce`](#adumpdataonce)                      | Write named game-state dumps to the log, once                      |
| [`aAddItemsOnce`](#aadditemsonce)                      | Bulk-add or preview game items, once                               |
| [`sLogPattern`](#slogpattern)                          | Format of each log line                                            |
| [`fFlowFlag1/2/3`, `sFlowText1/2`](#flow-values)       | Mod-defined scratch values read by mod code                        |
| [`sSceneDepthStrategy`, `bSceneDepthDiagnostics`](#scene-depth-occlusion) | Overlay-occlusion support switches                |
| [`bVRUIShowFingerTip`](#bvruishowfingertip)            | Mark the fingertip that presses the VR UI with a small sphere      |
| [`bVRUIDevLayout`](#bvruidevlayout)                    | Tune the placement of the VR UI in a file while the game runs      |
| [`iVersion`](#iversion)                                | Internal schema version — don't touch                              |

---

## `iLogLevel`

Minimum severity written to the mod's log. Lower numbers log more.

| Value | Severity | When to use                                   |
| ----- | -------- | --------------------------------------------- |
| `0`   | Trace    | Maximum detail for a short diagnostic session |
| `1`   | Debug    | Development diagnostics                       |
| `2`   | Info     | **Recommended default** — normal use          |

```toml
[Debug]
iLogLevel = 1
```

The file is TOML: a text value goes in quotes (`sLogPattern = "%H:%M:%S.%e %l: %v"`), a float has its
decimal point, and a key appears once. A file broken by a typo is logged with the line and what is
wrong at it, and the values stay as they were until it is fixed.

> **Trace (`0`) and large dumps fill the log fast.** Return `iLogLevel` to `2` once you've collected
> what you need.

## `sAdjustTarget` — in-game live-tuning

The adjuster lets you dial in a transform, a hand pose, or a floating-point value **with the VR
controllers, in-game**, then save the result straight back to the config file. It's the fast way to author
node offsets and poses without guessing numbers by hand.

While a target is active the framework **suppresses all normal controller input** — you can't move
or interact, so the sticks are free to drive the adjustment. Controller names are handedness-aware:
**`primary`** is your weapon hand, **`offhand`** is the other.

### How to use

1. Set `sAdjustTarget` to a [target](#targets) (a fixed keyword or `Section::Key`) and save the file.
2. Wait for the config-reload notification, then adjust the value in-game with the sticks (see the
   control maps below).
3. **Tap primary-hand A** to save the current value to the config file.
4. **Long-press primary-hand A** to discard unsaved changes and reload from disk.
5. Set `sAdjustTarget = "none"` when you're done to restore normal controller input.

> `haptictest` is the exception — there, primary-hand A _plays_ the pattern instead of saving. See
> [Haptic test](#haptic-test).

### Targets

| Value                       | Edits                                     |
| --------------------------- | ----------------------------------------- |
| `none` or blank             | Disabled (controllers restored)           |
| `transform`                 | The `[Debug]` scratch field `tTransform`  |
| `handpose`                  | The `[Debug]` scratch field `hHandPose`   |
| `flag1` / `flag2` / `flag3` | One flow flag                             |
| `flag123`                   | All three flow flags at once              |
| `haptictest`                | Plays the haptic sequence in `sFlowText1` |
| `Section::Key`              | Any supported field in any config section |

`transform` and `handpose` are reusable scratch targets — they default to an identity transform and
an all-zero pose if the field is missing, and saving creates/updates it under `[Debug]`.

**`Section::Key`** edits a real field in place. The value type is inferred from the first letter of
the key:

- **`t…`** — a transform, `{ pos = [x, y, z], rot = [heading, roll, attitude], scale = s }`
  (translation in game units, rotation in degrees).
- **`h…`** — a hand pose, `{ thumb = [prox, mid, dist, splay], index = [..], middle = [..], ring = [..],
  pinky = [..], palm = [pitch, yaw] }`, a finger or the palm left out being zeros.
- **`f…`** — a float.

Integer, boolean, and string fields aren't supported by `Section::Key`.

```toml
[Debug]
# Pick ONE of these, a key appears once in the file:
sAdjustTarget = "MyMod::tWeaponOffset"      # a transform field in the mod section
# sAdjustTarget = "MyMod::fBeamRadius"      # a float field
# sAdjustTarget = "transform"               # the [Debug] scratch transform
# sAdjustTarget = "flag123"                 # all three flow flags
```

### Transform controls

The sticks drive translation by default; hold an offhand modifier to switch to rotation or scale.

| Hold             | Input           | Changes            |
| ---------------- | --------------- | ------------------ |
| _(nothing)_      | Primary stick Y | Translation X      |
| _(nothing)_      | Primary stick X | Translation Y      |
| _(nothing)_      | Offhand stick Y | Translation Z      |
| Offhand **Grip** | Primary stick Y | Heading (pitch)    |
| Offhand **Grip** | Primary stick X | Attitude (yaw)     |
| Offhand **Grip** | Offhand stick X | Roll               |
| Offhand **A**    | Primary stick Y | Scale (min `0.05`) |

### Hand-pose controls

A hand pose is 22 floats: five fingers of `[proximal, middle, distal, splay]` followed by the palm's
`[pitch, yaw]`. You edit **one slot at a time**, starting at the thumb. **Tap offhand A** to
advance `thumb → index → middle → ring → pinky → palm → thumb` (wraps around); a notification names
the new slot.

For a **finger** slot:

| Hold             | Input           | Changes       |
| ---------------- | --------------- | ------------- |
| _(nothing)_      | Primary stick Y | Proximal bend |
| _(nothing)_      | Primary stick X | Middle bend   |
| _(nothing)_      | Offhand stick Y | Distal bend   |
| Offhand **Grip** | Primary stick Y | Splay         |

Finger values clamp to `-0.8 … 2.0`. For the **palm** slot, primary stick Y changes `palmPitch` and
primary stick X changes `palmYaw`, both clamped to `-10 … 15`.

### Flow-flag controls

For `flag1` / `flag2` / `flag3`, primary stick Y changes the selected value. For `flag123`:

| Input           | Changes      |
| --------------- | ------------ |
| Primary stick Y | `fFlowFlag1` |
| Primary stick X | `fFlowFlag2` |
| Offhand stick Y | `fFlowFlag3` |

### Haptic test

Set `sAdjustTarget = "haptictest"` and put a semicolon-separated sequence in `sFlowText1`. Each segment
is `durationSeconds,startIntensity,endIntensity` (intensities normally `0 … 1`). Whitespace is fine;
malformed segments are skipped and logged.

```toml
[Debug]
sFlowText1 = "0.4,0.1,1.0; 0.1,0,0; 0.05,1,1"
sAdjustTarget = "haptictest"
```

**Tap primary-hand A** to play the sequence on the primary controller. Combined with live-reload,
this lets you edit `sFlowText1`, feel the result, and iterate.

## `aDumpDataOnce`

Writes one or more diagnostic node/state dumps to the mod log, then **removes each name from the
array after it runs**, in memory and in the file, so a later file reload doesn't repeat it. The value
is an array of names, matched whole and in their case.

| Name        | Dumps                                                     |
| ----------- | --------------------------------------------------------- |
| `ui_tree`   | Framework VR UI element tree (needs a running UI manager) |
| `skelly`    | Player skeleton node tree                                 |
| `fp_skelly` | First-person skeleton node tree                           |
| `geometry`  | Player geometry entries and their visible/hidden state    |
| `pipboy`    | Pip-Boy node tree                                         |
| `world`     | Framework player/weapon-related world subtree             |
| `all_nodes` | Full scene node tree — **potentially huge**               |
| `perf`      | The [perf sites](../src/perf/README.md) since the last reset, as an indented table (the devbench `perf` action's `text` format) |
| `perf_reset` | Like `perf`, then starts a new window. When recording is off, it only switches it on |

A mod adds names of its own (FRIK has `weapon_pos`, `weapon_muzzle` and `menus`). A name no code
asks for stays in the array.

Recording is off until the mod's devbench tool arms, which it does as soon as devbench is
installed or a Tracy viewer connects. Without either, start with `perf_reset`, hold
the condition being measured, then use `perf`, or `perf_reset` again to log that window and start
the next one.

```toml
[Debug]
aDumpDataOnce = ["skelly", "fp_skelly", "geometry"]
```

## `aAddItemsOnce`

Runs inventory requests, each an operation on one or more item categories with an optional filter.
Like the dumps, the array is cleared (from memory and the file) before it's read, so even an
invalid request has to be entered again, and a valid one never runs twice. All the requests are read
before any runs: when one is not valid, the log says which and why, by its key, and **none of them
runs**. The array is written back as `[]`, so a comment inside it goes too: keep notes above the key.

```toml
[Debug]
aAddItemsOnce = [
  { op = "print", category = "weapons", class = "gun", keyword = "laser" },
  { op = "get", category = ["weapons", "throwables"] },
]
```

> **Start with a `print`.** `get`/`get-all` can add a lot of items to your character. Preview with a
> print operation and keep a save before committing.

### Request keys

| Key        | Required | Value                                                                 |
| ---------- | -------- | --------------------------------------------------------------------- |
| `op`       | yes      | The [operation](#operations)                                          |
| `category` | yes      | One [category](#categories), or an array of them                      |
| `name`     |          | Case-insensitive display-name substring                               |
| `keyword`  |          | Case-insensitive keyword editor-ID substring                          |
| `slot`     |          | Armor only: one [slot](#armor-slots), or an array of them, OR-matched |
| `class`    |          | Armor: `light`, `heavy` or `none`. Weapons: `melee`, `gun` or `unarmed` |

An item has to match every filter key of its request. Values are case-insensitive, keys are not, and
a key not in the table is an error. `slot` and `class` are skipped, with a warning, for a category
they don't apply to, so one request can cover weapons and armor.

### Operations

| Operation   | Behavior                                                                              |
| ----------- | ------------------------------------------------------------------------------------- |
| `get`       | Add items reachable from a leveled list or crafting recipe (i.e. normally obtainable) |
| `get-all`   | Add **every** matching named/playable item, including normally unobtainable ones      |
| `print`     | Log what `get` would add, without adding                                              |
| `print-all` | Log what `get-all` would add, without adding                                          |

Print operations log FormID, editor ID (when available), display name, source plugin, keywords, and
relevant armor/weapon details.

### Categories

| Category     | Items                                      | Quantity per match |
| ------------ | ------------------------------------------ | ------------------ |
| `weapons`    | Guns, melee, unarmed (throwables excluded) | 1                  |
| `throwables` | Grenades and mines                         | 5                  |
| `ammo`       | Ammunition                                 | 50                 |
| `armor`      | Armor and clothing                         | 1                  |
| `aid`        | Aid / alchemy items                        | 1                  |
| `misc`       | Miscellaneous items                        | 1                  |

### Armor slots

A slot is a Creation Kit number `30 … 61`, written as a number, or one of these names, in quotes:

| Name                                                           | Slot(s)               |
| -------------------------------------------------------------- | --------------------- |
| `head`, `helmet`, `hair`                                       | 30                    |
| `body`, `outfit`, `underarmor`                                 | 33                    |
| `hands`, `lhand`, `rhand`                                      | 34 and/or 35          |
| `chest`, `torso`                                               | 41                    |
| `arms`, `larm`, `leftarm`, `rarm`, `rightarm`                  | 42 and/or 43          |
| `legs`, `lleg`, `leftleg`, `rleg`, `rightleg`                  | 44 and/or 45          |
| `headband`, `eyes`, `glasses`, `mouth`, `mask`, `neck`, `ring` | 46, 47, 49, 50, or 51 |

### Examples

```toml
[Debug]
aAddItemsOnce = [
  # Preview obtainable laser guns
  { op = "print", category = "weapons", class = "gun", keyword = "laser" },
  # Add obtainable weapons and five of each obtainable throwable
  { op = "get", category = ["weapons", "throwables"] },
  # Preview every heavy torso or body armor, including unobtainable
  { op = "print-all", category = "armor", slot = ["torso", 33], class = "heavy" },
  # Add all named aid items containing "stimpak"
  { op = "get-all", category = "aid", name = "stimpak" },
]
```

## Flow values

`fFlowFlag1`, `fFlowFlag2`, `fFlowFlag3` (numeric) and `sFlowText1`, `sFlowText2` (text) are scratch
values with **no fixed meaning** — the framework just loads and live-reloads them, and mod code
decides what they do. They're handy for toggling experimental behavior or feeding a value into the
mod without rebuilding.

Mod code reads them as `config->debug.flowFlag1` … `flowFlag3`, `flowText1`, `flowText2`. Blank
numeric values load as `0`; blank text loads as an empty string.

```toml
[Debug]
fFlowFlag1 = 1.0
fFlowFlag2 = 0.25
sFlowText1 = "experimental-beam"
```

> The one built-in use is `sFlowText1`, which supplies the haptic sequence for
> [`sAdjustTarget = "haptictest"`](#haptic-test).

## `sLogPattern`

The prefix and layout of each log line, in [spdlog pattern syntax](https://github.com/gabime/spdlog/wiki/Custom-formatting).

```toml
[Debug]
# Compact default
sLogPattern = "%H:%M:%S.%e %l: %v"

# or: source file + line, fixed-width columns
# sLogPattern = "%H:%M:%S.%e %L [%-25s:%-4#]: %v"
```

## Scene-depth occlusion

Two support switches for the capture that hides overlay panels behind the world. Both default to the
right thing, so you only touch them when an overlay draws where it should not.

```toml
[Debug]
# auto (shipped) | depthpass | direct | off
sSceneDepthStrategy = "auto"

# adds the capture's rows to the debug-draw overlay and re-runs its internal A/B comparison
bSceneDepthDiagnostics = false
```

`sSceneDepthStrategy` picks which implementation resolves the world's depth. `direct` keeps the
capture but never resamples — correct without an upscaler, wrong with one — and `off` drops occlusion
entirely, so overlays draw on top as they did before it existed. Between them they say whether a
misdrawn overlay is the capture, the resample, or neither. `depthpass` is an alternative election kept
for re-testing.

`bSceneDepthDiagnostics` is for whoever came to look at the capture rather than for everyone who opens
the debug overlay. The capture reports itself to the log either way, whenever something changes, at
the default log level — so a log is usually enough and neither key needs touching.

Design, measurements and the failure modes they address:
[`docs/tech/scene-depth-occlusion.md`](tech/scene-depth-occlusion.md).

## `bVRUIShowFingerTip`

While a VR UI is shown, a small gold sphere marks the point that presses its buttons. It is off by default,
and the sphere then appears only while no hand is drawn at the controller (no FRIK, or the hand is
away from the controller), where the controller presses the buttons and there is no finger to see.
Set it to see where the framework finds the fingertip:

```toml
[Debug]
bVRUIShowFingerTip = true
```

## `bVRUIDevLayout`

A development tool for a mod's author: it tunes where the mod's VR UI stands, and how big it is, while
the game runs. It is off by default and is not in the shipped config, so add the line:

```toml
[Debug]
bVRUIDevLayout = true
```

While it is on, the mod keeps `<mod>_DevLayout.ini` beside its config file, with a section for each UI that is
shown and a line for each of its elements:

```ini
[MainConfig]
MainConfig = Pos:(0.00,0.00,0.00), Rot:(0.00,0.00,0.00), Scale:(1.60), Padding:(0.35), Layout:(5)
Row1 = Pos:(0.00,0.00,0.00), Rot:(0.00,0.00,0.00), Scale:(1.00), Padding:(0.30), Layout:(1)
```

Edit a line and save the file, and the UI in the game follows in a fraction of a second. A UI that is
closed keeps its lines, so going to another screen and back keeps what you tuned. The file is removed
when the key is turned off, and starts empty on the next run: copy the values you want into the code.

The fields of a line, and what happens to lines you remove:
[VR UI README](../src/vrui/README.md#notes).

## `iVersion`

The mod's internal config schema version, used by the framework to apply config migrations. **Don't
change it by hand or copy it between mods.**
