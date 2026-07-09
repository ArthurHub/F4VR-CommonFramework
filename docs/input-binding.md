# Controller bindings & activation spheres

Mods built on F4VR-CommonFramework let you **rebind controller inputs from the INI** — pick the hand,
the button, and how it has to be pressed — without rebuilding the mod. This page covers two related
things:

- **[The binding line](#the-binding-line)** — the format for a single rebindable input. Every
  rebindable action is one INI key whose value is a binding line, for example:

  ```ini
  [Controls]
  sOpenMenu = offhand longpress grip
  ```

- **[Activation spheres](#activation-spheres)** — a higher-level _proximity gesture_: a spherical zone
  around a hand, the HMD, or a prop that fires a binding when your hand enters it. A sphere is a small
  group of INI keys (a zone + up to two binding lines + haptics + a visual), each grouped in its own
  section. **Not every binding is part of a sphere** — plain action keys (menus, toggles, …) use just a
  binding line; spheres are for reach-into-a-zone gestures.

```ini
[MyMod_SomeActivation]
tZone = 0,0,0;0,0,0;18
sPrimaryBinding = offhand tap trigger suppress
sSecondaryBinding = none
sEntryHaptic = Tick
sPrimaryHaptic = DoubleClick
```

Which keys and sections exist (and their defaults) is up to each mod — check its INI. The formats
below are the same in every mod.

> **Edits apply live.** The INI is watched while the game runs, so saving takes effect immediately — no
> restart. A malformed value is logged as a warning and the action keeps its previous (or default)
> value.

---

# The binding line

```
<hand> <type> <button> [duration] [suppress|nosuppress] [+modifier]
```

- **Case-insensitive** and forgiving of spacing — tokens may be separated by spaces, commas, or colons.
- `<hand>` and `<type>` are always required. Button activations also need a `<button>`; thumbstick /
  axis activations need a `<direction>` instead (see [Thumbstick & axis bindings](#thumbstick--axis-bindings)).
- `[duration]`, `[suppress|nosuppress]`, and `[+modifier]` are optional and may be omitted.

| Example                             | Meaning                                                       |
| ----------------------------------- | ------------------------------------------------------------- |
| `offhand tap grip`                  | Quick press-and-release of the off-hand grip                  |
| `primary press trigger`             | Primary trigger pressed this frame                            |
| `primary longpress a 0.6`           | Hold the primary **A** button for 0.6 s                       |
| `left double b`                     | Double-press **B** (menu) on the left controller              |
| `offhand tap trigger suppress`      | Tap the off-hand trigger; hide it from the game while active  |
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
<hand> thumbstick <direction> [threshold] [suppress|nosuppress] [+modifier]   # shorthand — implies the thumbstick axis
<hand> axis <axis> <direction> [threshold] [suppress|nosuppress] [+modifier]   # explicit axis
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

## Suppressing the game input

Add `suppress` (or `nosuppress`) anywhere on the line to opt this binding **in or out of input
suppression**: while a consumer treats the binding as active, its physical button is hidden from the
game so it can't _also_ fire its normal action. It is **off by default** — most bindings simply read
the input and leave it alone.

Suppression only has an effect where something applies it. Today that consumer is an
[activation sphere](#activation-spheres), which suppresses a `suppress` binding for as long as the
hand is inside its zone (so, e.g., the trigger you use to grab the flashlight doesn't also fire your
weapon while your hand is in the grab zone).

```ini
sGrab = offhand tap trigger suppress
```

## Disabling a binding

Set the value to `none` or leave it **empty** to turn an action off — it parses cleanly and simply
never fires (no warning logged).

```ini
sOpenMenu = none
```

> Note the difference between _empty_ and _absent_. A present-but-empty value is an explicit "off",
> while removing the key entirely falls back to the mod's built-in default binding.

---

# Activation spheres

An **activation sphere** is a proximity gesture. It defines a spherical zone anchored to a node — a
hand's wand, the HMD, or a prop attached to the body — and, while a bound hand is inside that zone:

- **suppresses** each of its bindings that opted in with `suppress` (so the button doesn't also fire
  its normal action),
- plays a one-shot **entry haptic** when the hand first enters,
- **fires** whichever of its (up to two) bindings is pressed, with a per-binding **activation haptic**,
- and optionally **draws the zone** as a translucent sphere so you can see where to reach.

A mod groups each sphere into **its own INI section**. The section name is chosen by the mod (check its
INI); the keys inside are always these:

| Key                 | What it sets                                                                                                                                                                                                                                                   |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `tZone`             | The zone as a transform `x,y,z;heading,roll,attitude;scale`. Only **translate + scale** matter — the zone is a sphere, so rotation is ignored and `scale` is its **diameter**.                                                                                 |
| `tZonePA`           | Optional **power-armor variant** of `tZone` (for a zone whose anchor moves in PA). Omit to reuse `tZone`.                                                                                                                                                      |
| `sPrimaryBinding`   | The main [binding line](#the-binding-line). Append `suppress` to hide its button while the hand is in the zone.                                                                                                                                                |
| `sSecondaryBinding` | An optional second binding line (e.g. a `longpress` variant of the same button). `none` to omit.                                                                                                                                                               |
| `sEntryHaptic`      | Haptic played **once** when a hand enters the zone. `none` = silent.                                                                                                                                                                                           |
| `sPrimaryHaptic`    | Haptic played when `sPrimaryBinding` fires. `none` = silent.                                                                                                                                                                                                   |
| `sSecondaryHaptic`  | Haptic played when `sSecondaryBinding` fires. `none` = silent.                                                                                                                                                                                                 |
| `sShowSphere`       | When the zone's visual is drawn: `never`, `always`, or `wheninside` (only while a bound hand is in it — a proximity hint).                                                                                                                                     |
| `sSphereNif`        | The `.nif` mesh drawn as the zone's visual (a mesh path resolved like any prop nif). Empty uses the framework's default sphere mesh. Changing it at runtime releases the old mesh and loads the new one.                                                       |
| `fSphereScale`      | Scale multiplier for the drawn visual **only** — the proximity hit test always uses the full `tZone` scale. `< 1` draws the sphere smaller than the interaction radius (e.g. an "inside the zone" hint with `sShowSphere = wheninside`); `1` matches the zone. |

Any key you leave out keeps the mod's built-in default for that sphere.

## Haptic names

`sEntryHaptic` / `sPrimaryHaptic` / `sSecondaryHaptic` take one of these names
(case-insensitive), or `none` for silent:

`Tick`, `Click`, `DoubleClick`, `TripleClick`, `Success`, `Warning`, `Error`, `Notification`,
`Start`, `Stop`, `RampUp`, `RampDown`, `Heartbeat1`, `Heartbeat2`, `Heartbeat3`, `Buzz`, `MidBuzz`,
`LongBuzz`.

## Controlling the visual sphere

The sphere you see is **purely cosmetic** and separate from the interaction zone: the proximity hit test
always uses the full `tZone`, so tuning the visual never changes where the gesture actually fires. Three
keys control it independently:

- **Whether it shows** — `sShowSphere`: `never` (invisible — the default for most spheres), `always` (a
  fixed marker, handy while tuning placement), or `wheninside` (appears only while a bound hand is in the
  zone — a "you're in range" hint).
- **Which mesh** — `sSphereNif`: point it at any `.nif` to change the look (or pick one of the built-in
  meshes below); empty falls back to the framework's default sphere. Editing this while the game runs
  hot-swaps the mesh (the old one is released and the new one loaded on the next frame it's shown).
- **How big** — `fSphereScale`: multiplies the drawn size **only**. Use `< 1` to draw a small marker inside
  the real (larger) zone so the hint doesn't fill your whole reach; `1` matches the zone exactly.

**Built-in meshes** shipped under `ui-common\`:

- `ui-common\activation-sphere@white-full.nif`
- `ui-common\activation-sphere@white-medium.nif`
- `ui-common\activation-sphere@white-subtle.nif`
- `ui-common\activation-sphere@cyan-full.nif`
- `ui-common\activation-sphere@cyan-medium.nif`
- `ui-common\activation-sphere@cyan-subtle.nif`
- `ui-common\activation-sphere@green-medium.nif`
- `ui-common\activation-sphere@green-subtle.nif`
- `ui-common\activation-sphere@purple-medium.nif`
- `ui-common\activation-sphere@purple-subtle.nif`
- `ui-common\activation-sphere@amber-medium.nif`
- `ui-common\activation-sphere@amber-subtle.nif`
- `ui-common\activation-sphere@gold-subtle.nif`
- `ui-common\debug-sphere.nif`
- `ui-common\debug-sphere@strong.nif`

A common recipe is a small proximity dot — `sShowSphere = wheninside` with `fSphereScale = 0.5` — that
appears only as your hand nears the zone. To hide the visual entirely, set `sShowSphere = never`.

## Example

```ini
[MyMod_SomeActivation]
tZone = 0,0,0;0,0,0;18
sPrimaryBinding = offhand tap trigger suppress
sSecondaryBinding = offhand longpress trigger suppress
sEntryHaptic = Tick
sPrimaryHaptic = DoubleClick
sSecondaryHaptic = Click
sShowSphere = wheninside
sSphereNif = ui-common\activation-sphere@white-medium.nif
```

A tap of the off-hand trigger while it's near the HMD fires the primary gesture (its trigger hidden
from the game); a long-press fires the secondary; the zone shows only while your hand is near it.

---

# For mod authors

**A single binding** is `f4cf::vrcf::InputBinding`, evaluated each frame with one
`VRControllers.check(binding)`. Load one from the INI with `ConfigBase::getInputBindingValue(...)`, or
parse a string directly with `parseInputBinding(...)`. The `suppress` flag rides on the binding
(`InputBinding::suppress`); a consumer that wants exclusive use of the button applies it via
`VRControllersSuppress`. Full API, code examples, and the button map are in
[`src/vrcf/README.md`](../src/vrcf/README.md).

**An activation sphere** is two types:

- `f4cf::f4vr::WandActivationConfig` — the authored bundle (zone + bindings + haptics + visibility + visual mesh/scale).
  Load one whole INI section with `ConfigBase::loadWandActivationConfig(ini, "SectionName", defaults)`.
- `f4cf::f4vr::WandActivationSphere` — the runtime zone. Drive it each frame with `onFrameUpdate(frame,
onActivated)`, composing the per-frame `Frame` from your `WandActivationConfig` (gating a binding off
  for the frame by passing the disabled binding, and re-anchoring the zone if needed). It handles the
  proximity test, owner-keyed suppression, haptics, cooldown, and the debug/proximity visual.

See [`src/f4vr/README.md`](../src/f4vr/README.md) for the `WandActivationSphere` API and an example.
