# Input binding configuration

Mods built on F4VR-CommonFramework can let you **rebind a controller input from the INI** — pick the
hand, the button, and how it has to be pressed — without rebuilding the mod. Each rebindable action
is one INI key whose value is a single binding line, for example:

```ini
[Controls]
sOpenMenu = offhand longpress grip
```

This page documents that binding line: every token, its aliases, and what the optional timing values
mean. Which keys exist (and their defaults) is up to each mod — check its INI. The line format below
is the same in every mod.

> **Edits apply live.** The INI is watched while the game runs, so saving a new binding takes effect
> immediately — no restart. A malformed value is logged as a warning and the action keeps its previous
> (or default) binding.

## The format

```
<hand> <type> <button> [duration] [+modifier]
```

- **Case-insensitive** and forgiving of spacing — tokens may be separated by spaces, commas, or colons.
- `<hand>` and `<type>` are always required. Button activations also need a `<button>`; thumbstick /
  axis activations need a `<direction>` instead (see [Thumbstick & axis bindings](#thumbstick--axis-bindings)).
- `[duration]` and `[+modifier]` are optional and may be omitted.

| Example                             | Meaning                                                       |
| ----------------------------------- | ------------------------------------------------------------- |
| `offhand tap grip`                  | Quick press-and-release of the off-hand grip                  |
| `primary press trigger`             | Primary trigger pressed this frame                            |
| `primary longpress a 0.6`           | Hold the primary **A** button for 0.6 s                       |
| `left double b`                     | Double-press **B** (menu) on the left controller              |
| `primary press trigger +grip`       | Press the trigger while gripping the **same** hand (a chord)  |
| `offhand longpress grip +primary:a` | Long-press off-hand grip while holding **A** on the main hand |
| `primary thumbstick up`             | Push the primary thumbstick up                                |
| `none`                              | Binding disabled — never fires                                |

## Hand

Which controller the binding watches. Names are **handedness-aware**: `primary` follows your weapon
hand and `offhand` the other, so a binding keeps working when left-handed mode is on.

| Token     | Controller                        |
| --------- | --------------------------------- |
| `primary` | Weapon / dominant hand            |
| `offhand` | Non-weapon hand                   |
| `right`   | Physical right controller (fixed) |
| `left`    | Physical left controller (fixed)  |

> Prefer `primary` / `offhand` over `left` / `right` so the binding adapts to the player's handedness.

## Activation type

How the button must be pressed for the binding to fire. This also decides what an optional
`[duration]` means.

| Token        | Fires when…                                                 | `[duration]` controls                       |
| ------------ | ----------------------------------------------------------- | ------------------------------------------- |
| `tap`        | Quick press-and-release (held under ~0.3 s)                 | _(ignored)_                                 |
| `press`      | The button is first pressed this frame                      | _(ignored)_                                 |
| `release`    | The button is released this frame                           | **Max** hold to still count (default: any)  |
| `longpress`  | The button is held past the threshold                       | Hold threshold in seconds (default `0.6`)   |
| `double`     | The button is pressed twice in quick succession             | Max gap between presses (default `0.4`)     |
| `hold`       | The button is held down — fires **every frame** while held  | **Min** hold before it starts (default `0`) |
| `touch`      | The button is touched (capacitive), not necessarily pressed | _(ignored)_                                 |
| `thumbstick` | An analog axis is pushed in a direction — see below         | _(uses `threshold` instead; see below)_     |

`duration` is in **seconds**. Leaving it off (or `0`) uses the per-type default shown above.

## Button

The button the activation watches.

| Token        | Button                                   |
| ------------ | ---------------------------------------- |
| `trigger`    | Trigger                                  |
| `grip`       | Grip                                     |
| `a`          | **A** / **X** (lower face button)        |
| `b`          | **B** / **Y** / menu (upper face button) |
| `thumbstick` | Thumbstick click                         |
| `system`     | System button                            |

## Thumbstick & axis bindings

To trigger on the thumbstick (or another analog axis) being **pushed in a direction**, use an axis
activation. There are two spellings:

```
<hand> thumbstick <direction> [threshold] [+modifier]   # shorthand — implies the thumbstick axis
<hand> axis <axis> <direction> [threshold] [+modifier]   # explicit axis
```

- `<direction>` is `up`, `down`, `left`, or `right`.
- `<axis>` (explicit form only) is `thumbstick` (also `touchpad` / `joystick`), `trigger`, or `grip`.
- The optional trailing number is the **threshold** — how far the axis must travel (0–1) before it
  counts. Default `0.85`.

| Example                       | Meaning                                                  |
| ----------------------------- | -------------------------------------------------------- |
| `primary thumbstick up`       | Primary thumbstick pushed up                             |
| `offhand thumbstick left 0.5` | Off-hand thumbstick pushed left past half travel         |
| `right axis trigger up 0.7`   | Right trigger squeezed past 0.7 (analog, not the button) |

## Modifiers (chords)

Append `+<button>` to require a second button be **held down** while the binding fires — a chord. By
default the modifier is checked on the binding's own hand; prefix it with a hand to pin it elsewhere.

| Form            | Meaning                                                        |
| --------------- | -------------------------------------------------------------- |
| `+grip`         | Hold grip on the **binding's own hand**                        |
| `+offhand:grip` | Hold grip on the **off hand** (regardless of the binding hand) |
| `+primary:a`    | Hold **A** on the primary hand                                 |

The modifier can sit anywhere on the line, but by convention it goes last:

```ini
sFastTravel = primary press trigger +offhand:grip
```

## Disabling a binding

Set the value to `none` or leave it **empty** to turn an action off — it parses
cleanly and simply never fires (no warning logged).

```ini
sOpenMenu = none
```

> Note the difference between _empty_ and _absent_. A present-but-empty value is an explicit "off",
> while removing the key entirely falls back to the mod's built-in default binding.

## For mod authors

The framework exposes this as `f4cf::vrcf::InputBinding`, evaluated each frame with a single
`VRControllers.check(binding)`. Load one from the INI with `ConfigBase::getInputBindingValue(...)`, or
parse a string directly with `parseInputBinding(...)`. Full API, code examples, and the button map
are in [`src/vrcf/README.md`](../src/vrcf/README.md).
