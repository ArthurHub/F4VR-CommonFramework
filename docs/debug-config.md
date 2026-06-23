# Debug configuration

Every mod built on F4VR-CommonFramework ships a `[Debug]` section in its INI. The fields are the
same across mods: log control, a few runtime scratch values, an in-game live-tuning tool, and
one-shot diagnostic actions. This page explains each one with examples.

**Edits apply live.** The INI is watched while the game runs, so almost everything here takes effect
the moment you save the file — no restart, usually no reload of the save. Set a value, alt-tab back
in, and it's active.

> **Examples use `MyMod` as the config section.** Replace it with your mod's section name — e.g.
> `ImmersiveFlashlightVR`. The `[Debug]` keys themselves are identical in every mod.

## Fields at a glance

| Field                                                  | What it does                                                       |
| ------------------------------------------------------ | ------------------------------------------------------------------ |
| [`iLogLevel`](#iloglevel)                              | Minimum severity written to the log                                |
| [`sAdjustTarget`](#sadjusttarget--in-game-live-tuning) | Tune transforms / hand poses / floats in-game with the controllers |
| [`sDumpDataOnceNames`](#sdumpdataoncenames)            | Write named game-state dumps to the log, once                      |
| [`sAddItemsOnceNames`](#sadditemsoncenames)            | Bulk-add or preview game items, once                               |
| [`sLogPattern`](#slogpattern)                          | Format of each log line                                            |
| [`fFlowFlag1/2/3`, `sFlowText1/2`](#flow-values)       | Mod-defined scratch values read by mod code                        |
| [`iVersion`](#iversion)                                | Internal schema version — don't touch                              |

---

## `iLogLevel`

Minimum severity written to the mod's log. Lower numbers log more.

| Value | Severity | When to use                                   |
| ----- | -------- | --------------------------------------------- |
| `0`   | Trace    | Maximum detail for a short diagnostic session |
| `1`   | Debug    | Development diagnostics                       |
| `2`   | Info     | **Recommended default** — normal use          |

```ini
[Debug]
iLogLevel = 1
```

> **Trace (`0`) and large dumps fill the log fast.** Return `iLogLevel` to `2` once you've collected
> what you need.

## `sAdjustTarget` — in-game live-tuning

The adjuster lets you dial in a transform, a hand pose, or a floating-point value **with the VR
controllers, in-game**, then save the result straight back to the INI. It's the fast way to author
node offsets and poses without guessing numbers by hand.

While a target is active the framework **suppresses all normal controller input** — you can't move
or interact, so the sticks are free to drive the adjustment. Controller names are handedness-aware:
**`primary`** is your weapon hand, **`offhand`** is the other.

### How to use

1. Set `sAdjustTarget` to a [target](#targets) (a fixed keyword or `Section::Key`) and save the INI.
2. Wait for the config-reload notification, then adjust the value in-game with the sticks (see the
   control maps below).
3. **Tap primary-hand A** to save the current value to the INI.
4. **Long-press primary-hand A** to discard unsaved changes and reload from disk.
5. Set `sAdjustTarget = none` when you're done to restore normal controller input.

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
| `Section::Key`              | Any supported field in any INI section    |

`transform` and `handpose` are reusable scratch targets — they default to an identity transform and
an all-zero pose if the field is missing, and saving creates/updates it under `[Debug]`.

**`Section::Key`** edits a real field in place. The value type is inferred from the first letter of
the key:

- **`t…`** — a transform, `x,y,z;heading,roll,attitude;scale` (translation in game units, rotation
  in degrees).
- **`h…`** — a 22-float hand pose.
- **`f…`** — a float.

Integer, boolean, and string fields aren't supported by `Section::Key`.

```ini
[Debug]
# Pick ONE of these — they're separate examples:
sAdjustTarget = MyMod::tWeaponOffset      # a transform field in the mod section
sAdjustTarget = MyMod::fBeamRadius        # a float field
sAdjustTarget = transform                 # the [Debug] scratch transform
sAdjustTarget = flag123                   # all three flow flags
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

A hand pose is 22 floats: five finger groups (`proximal,middle,distal,splay`) followed by
`palmPitch,palmYaw`. You edit **one slot at a time**, starting at the thumb. **Tap offhand A** to
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

Set `sAdjustTarget = haptictest` and put a semicolon-separated sequence in `sFlowText1`. Each segment
is `durationSeconds,startIntensity,endIntensity` (intensities normally `0 … 1`). Whitespace is fine;
malformed segments are skipped and logged.

```ini
[Debug]
sFlowText1 = 0.4,0.1,1.0; 0.1,0,0; 0.05,1,1
sAdjustTarget = haptictest
```

**Tap primary-hand A** to play the sequence on the primary controller. Combined with live-reload,
this lets you edit `sFlowText1`, feel the result, and iterate.

## `sDumpDataOnceNames`

Writes one or more diagnostic node/state dumps to the mod log, then **clears each name after it
runs** so a later file reload doesn't repeat it. Names are comma-separated.

| Name        | Dumps                                                     |
| ----------- | --------------------------------------------------------- |
| `ui_tree`   | Framework VR UI element tree (needs a running UI manager) |
| `skelly`    | Player skeleton node tree                                 |
| `fp_skelly` | First-person skeleton node tree                           |
| `geometry`  | Player geometry entries and their visible/hidden state    |
| `pipboy`    | Pip-Boy node tree                                         |
| `world`     | Framework player/weapon-related world subtree             |
| `all_nodes` | Full scene node tree — **potentially huge**               |

```ini
[Debug]
sDumpDataOnceNames = skelly, fp_skelly, geometry
```

## `sAddItemsOnceNames`

Runs **one** inventory operation against one or more item categories. Like the dumps, the value is
cleared (from memory and the INI) before it's parsed — so even an invalid request has to be entered
again, and a valid one never runs twice.

**Syntax:** `operation, category[:filter], category[:filter], …` — operation and category names are
case-insensitive.

> **Start with a `print`.** `get`/`get-all` can add a lot of items to your character. Preview with a
> print operation and keep a save before committing.

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

### Filters

Add `:` after a category, then join clauses with `|`. Multiple clauses on one category are **AND-ed**.

| Filter           | Applies to | Meaning                                      |
| ---------------- | ---------- | -------------------------------------------- |
| `name=<text>`    | Any        | Case-insensitive display-name substring      |
| `keyword=<text>` | Any        | Case-insensitive keyword editor-ID substring |
| `slot=<list>`    | Armor      | One or more `+`-separated slots, OR-matched  |
| `class=<value>`  | Armor      | `light`, `heavy`, or `none`                  |
| `class=<value>`  | Weapons    | `melee`, `gun`, or `unarmed`                 |

A bare clause is shorthand for `name=`, so `weapons:laser` means `weapons:name=laser`.

Armor slots accept Creation Kit numbers `30 … 61` or these aliases:

| Alias                                                          | Slot(s)               |
| -------------------------------------------------------------- | --------------------- |
| `head`, `helmet`, `hair`                                       | 30                    |
| `body`, `outfit`, `underarmor`                                 | 33                    |
| `hands`, `lhand`, `rhand`                                      | 34 and/or 35          |
| `chest`, `torso`                                               | 41                    |
| `arms`, `larm`, `leftarm`, `rarm`, `rightarm`                  | 42 and/or 43          |
| `legs`, `lleg`, `leftleg`, `rleg`, `rightleg`                  | 44 and/or 45          |
| `headband`, `eyes`, `glasses`, `mouth`, `mask`, `neck`, `ring` | 46, 47, 49, 50, or 51 |

```ini
[Debug]
# Preview obtainable laser guns
sAddItemsOnceNames = print, weapons:class=gun|keyword=laser

# Add obtainable weapons and five of each obtainable throwable
sAddItemsOnceNames = get, weapons, throwables

# Preview every heavy torso armor, including unobtainable
sAddItemsOnceNames = print-all, armor:slot=torso|class=heavy

# Add all named aid items containing "stimpak"
sAddItemsOnceNames = get-all, aid:stimpak
```

## Flow values

`fFlowFlag1`, `fFlowFlag2`, `fFlowFlag3` (numeric) and `sFlowText1`, `sFlowText2` (text) are scratch
values with **no fixed meaning** — the framework just loads and live-reloads them, and mod code
decides what they do. They're handy for toggling experimental behavior or feeding a value into the
mod without rebuilding.

Mod code reads them as `config->debug.flowFlag1` … `flowFlag3`, `flowText1`, `flowText2`. Blank
numeric values load as `0`; blank text loads as an empty string.

```ini
[Debug]
fFlowFlag1 = 1
fFlowFlag2 = 0.25
sFlowText1 = experimental-beam
```

> The one built-in use is `sFlowText1`, which supplies the haptic sequence for
> [`sAdjustTarget = haptictest`](#haptic-test).

## `sLogPattern`

The prefix and layout of each log line, in [spdlog pattern syntax](https://github.com/gabime/spdlog/wiki/Custom-formatting).

```ini
[Debug]
# Compact default
sLogPattern = %H:%M:%S.%e %l: %v

# Source file + line, fixed-width columns
sLogPattern = %H:%M:%S.%e %L [%-25s:%-4#]: %v
```

## `iVersion`

The mod's internal INI schema version, used by the framework to apply config migrations. **Don't
change it by hand or copy it between mods.**
