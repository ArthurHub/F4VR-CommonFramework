# Controller bindings & activation spheres

Mods built on F4VR-CommonFramework let you change their controls in the mod's INI file: which hand, which
button, and how you press it. Some actions are **gestures**: you reach a hand to a spot, such as your chest,
your head or your other hand, and press a button there. Each of those spots is an **activation sphere**.

The INI is `Documents\My Games\Fallout4VR\Mods_Config\<ModName>\<ModName>.ini`. Which settings it has, and
their defaults, is up to each mod. The formats on this page are the same in every mod.

> **Edits apply live.** Save the INI while the game runs and the change takes effect right away, with no
> restart. A value that can't be read is logged as a warning, and the setting keeps its previous value.

This page has three parts:

- **[Common changes](#common-changes)**: change a button, turn an action off, and hide or adjust a
  gesture's icon and sphere, vibration or zone.
- **The full reference**, for advanced players and mod authors: every option of
  [the binding line](#the-binding-line) and of [activation spheres](#activation-spheres).
- **[For mod authors](#for-mod-authors)**: the code behind them.

---

# Common changes

## Change a button

A binding is one line: the hand, how you press, and the button.

```ini
sOpenMenu = offhand longpress grip
```

| Part   | Common values                                                                                                             |
| ------ | ------------------------------------------------------------------------------------------------------------------------- |
| Hand   | `primary` (your weapon hand), `offhand` (the other hand)                                                                  |
| Press  | `tap` (a quick press and release), `press` (the moment it goes down), `longpress` (hold it 0.6 s), `double` (press twice) |
| Button | `trigger`, `grip`, `a` (A / X), `b` (B / Y), `thumbstick` (click the stick)                                               |

| Example                   | Meaning                                     |
| ------------------------- | ------------------------------------------- |
| `offhand tap grip`        | Tap the off-hand grip                       |
| `primary longpress a`     | Hold **A** on your weapon hand              |
| `primary longpress a 1.2` | The same, but you have to hold it for 1.2 s |
| `offhand double b`        | Double-press **B** on the off hand          |

Upper or lower case doesn't matter. `primary` and `offhand` follow the game's left-handed mode, so a binding
keeps working when you switch hands.

The binding line can do more, such as pushing the thumbstick in a direction or holding one button while
pressing another. See [the binding line](#the-binding-line).

## Turn an action off

Set it to `none`:

```ini
sOpenMenu = none
```

An empty value (`sOpenMenu =`) turns it off too. Deleting the whole line doesn't: the mod then uses its
default binding.

## Gestures

Each gesture has its own INI section. The section's name is up to the mod, but the keys in it are the same
in every mod. The ones you're most likely to change:

```ini
[MyMod_SomeActivation]
tZone = 0,0,0;0,0,0;18
sPrimaryBinding = offhand tap trigger suppress
sSecondaryBinding = offhand longpress trigger suppress
sEntryHaptic = Tick
sShowIcon = wheninside
fIconSize = 1
sShowSphere = never
```

### Hide or show the icon and sphere

A gesture can mark its zone with a small **icon**, a glowing **sphere**, or both. By default only the icon
shows, and only while your hand is in the zone. To never show the icon:

```ini
sShowIcon = never
```

The gesture still works the same, and the vibration as your hand enters still tells you when it's in the
zone.

`sShowIcon` (the icon) and `sShowSphere` (the sphere) each take one of:

| Value           | Shows                                                                                                        |
| --------------- | ------------------------------------------------------------------------------------------------------------ |
| `never`         | Never                                                                                                        |
| `always`        | All the time                                                                                                 |
| `wheninside`    | Only while your hand is in the zone                                                                          |
| `whenavailable` | Whenever the gesture can be used right now, e.g. a gesture that needs a drawn weapon only while one is drawn |

To change how they look:

- `fIconSize`: the icon's size in game units (one unit is about 1.4 cm).
- `sIcon`: which icon. `f4cf\activation-icon-hand.dds` (an open hand), `f4cf\activation-icon-ring.dds` (a
  ring), `f4cf\activation-icon-circle.dds` (a dot), or an icon the mod ships.
- `sSphereStyle`: the sphere's color and strength, e.g. `cyan-subtle` (see [the presets](#the-sphere)).

### Change the button, or turn the gesture off

`sPrimaryBinding` and `sSecondaryBinding` take a binding, as in [Change a button](#change-a-button). Keep
`suppress` at the end: it stops the button's normal action, such as firing your weapon, while your hand is in
the zone.

To turn the gesture off, set both to `none`. Its icon and sphere go with it, unless they're set to `always`.

### Turn off the vibration

`sEntryHaptic = none` stops the one as your hand enters the zone. `sPrimaryHaptic = none` and
`sSecondaryHaptic = none` stop the one when the gesture fires. The other patterns are listed under
[haptics](#haptics).

### Move or resize the zone

`tZone` is `x,y,z;0,0,0;size`. The first three numbers place the zone relative to what it's attached to (a
hand, your head, or a prop on your body), and the last one is its diameter in game units. The three in the
middle aren't used. To see the zone while you move it, set `sShowSphere = always`; the sphere then shows the
zone's real size, as long as `fSphereScale` is left at `1`.

---

# The binding line

The full format, of which [Change a button](#change-a-button) uses the first three parts:

```
<hand> <type> <button> [duration] [suppress|nosuppress] [+modifier]
```

- **Case-insensitive** and forgiving of spacing: tokens may be separated by spaces, commas, or colons.
- `<hand>` and `<type>` are always required. Button activations also need a `<button>`; thumbstick /
  axis activations need a `<direction>` instead (see [Thumbstick & axis bindings](#thumbstick--axis-bindings)).
- `[duration]`, `[suppress|nosuppress]`, and `[+modifier]` are optional and may be omitted.
- `none`, or an empty value, disables the binding (see [Turn an action off](#turn-an-action-off)).

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

---

# Activation spheres

An **activation sphere** is a proximity gesture. It defines a spherical zone anchored to a node — a
hand's wand, the HMD, or a prop attached to the body — and, while a bound hand is inside that zone:

- **suppresses** each of its bindings that opted in with `suppress` (so the button doesn't also fire
  its normal action),
- plays a one-shot **entry haptic** when the hand first enters,
- **fires** whichever of its (up to two) bindings is pressed, with a per-binding **activation haptic**,
- and optionally **marks the zone** — with a small icon at its center, a translucent sphere, or both — so
  you can see where to reach.

A mod groups each sphere into **its own INI section**. The section name is chosen by the mod (check its
INI); the keys inside are always the ones below. Any key you leave out keeps the mod's default for that
sphere.

## Zone and bindings

| Key                 | What it sets                                                                                                                                                                   |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `tZone`             | The zone as a transform `x,y,z;heading,roll,attitude;scale`. Only **translate + scale** matter — the zone is a sphere, so rotation is ignored and `scale` is its **diameter**. |
| `tZonePA`           | Optional **power-armor variant** of `tZone` (for a zone whose anchor moves in PA). Omit to reuse `tZone`.                                                                      |
| `sPrimaryBinding`   | The main [binding line](#the-binding-line). Append `suppress` to hide its button while the hand is in the zone.                                                                |
| `sSecondaryBinding` | An optional second binding line (e.g. a `longpress` variant of the same button). `none` to omit.                                                                               |

## Haptics

| Key                | Played                                |
| ------------------ | ------------------------------------- |
| `sEntryHaptic`     | **Once**, when a hand enters the zone |
| `sPrimaryHaptic`   | When `sPrimaryBinding` fires          |
| `sSecondaryHaptic` | When `sSecondaryBinding` fires        |

Each takes one of these names (case-insensitive), or `none` for silent:

`Tick`, `Click`, `DoubleClick`, `TripleClick`, `Success`, `Warning`, `Error`, `Notification`,
`Start`, `Stop`, `RampUp`, `RampDown`, `Heartbeat1`, `Heartbeat2`, `Heartbeat3`, `Buzz`, `MidBuzz`,
`LongBuzz`.

## Controlling the visuals

A zone can be marked two ways, each shown on its own schedule: a small **icon** at its center and a
translucent **sphere**. Both are **purely cosmetic** and separate from the interaction zone: the proximity
hit test always uses the full `tZone`, so tuning a visual never changes where the gesture actually fires.

**When each shows** — `sShowIcon` / `sShowSphere`, each one of:

- `never` — not drawn.
- `always` — a fixed marker, handy while tuning placement.
- `wheninside` — only while a bound hand is in the zone: a "you're in range" hint. The framework's default
  for the icon.
- `whenavailable` — whenever the gesture can fire, i.e. while the mod has at least one of its bindings
  enabled. A mod turns a binding off in states where it has no action (e.g. "put the light on the gun"
  with no gun drawn), so a visual set this way only advertises gestures that will work.

Each visual gets its own setting so they can combine, e.g. the icon `always` marking where to reach and the
sphere `wheninside` lighting up once your hand arrives.

### The icon

A small image at the zone's center that always turns to face you and is drawn on top of the world (it sits
inside your body or on a held prop, where the world would otherwise hide it). It fades in and out rather
than popping.

| Key          | What it sets                                                                                                                                                                                                                                                                                                                                                                              |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sShowIcon`  | When it's drawn (see above).                                                                                                                                                                                                                                                                                                                                                              |
| `sIcon`      | The image: a `.dds` path, resolved like the mod's other textures. The framework ships `f4cf\activation-icon-hand.dds` (an open hand, "reach here", the framework's default), `f4cf\activation-icon-ring.dds` (a plain ring) and `f4cf\activation-icon-circle.dds` (a filled dot); a mod may ship its own. A white glyph on a transparent background works best, since the tint colors it. |
| `sIconColor` | The tint, multiplied into the image: `r,g,b` or `r,g,b,a`, each a whole number in `0..255`; the optional `a` is its opacity.                                                                                                                                                                                                                                                              |
| `fIconSize`  | The size of its longer side, in game units.                                                                                                                                                                                                                                                                                                                                               |

### The sphere

| Key                  | What it sets                                                                                                                                                                                                                                                                                          |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sShowSphere`        | When it's drawn (see above).                                                                                                                                                                                                                                                                          |
| `sSphereStyle`       | How it looks: a preset (below). The keys under it override single values of the preset.                                                                                                                                                                                                               |
| `sSphereColor`       | The color as `r,g,b` or `r,g,b,a`, each a whole number in `0..255` (e.g. `255,51,51`). The optional `a` is the overall opacity (the activation presets draw at `27`-`40`); leave it off to keep the preset's.                                                                                         |
| `fSphereGlow`        | Brightness, `0..1`. `1` is as bright as the `debug` sphere; the activation presets sit at `0.4`-`0.5`.                                                                                                                                                                                                |
| `sSphereFalloff`     | How the opacity fades from the middle of the sphere to its edge, as `center,rim` (e.g. `0.045,1`). The activation mesh keeps its middle faint so it reads as a glowing rim; equal values draw it evenly.                                                                                              |
| `sSphereTexture`     | The texture set on the mesh (a `.dds` path, resolved like the mod's other textures). `none` keeps the texture the mesh itself names.                                                                                                                                                                  |
| `sSphereNif`         | The `.nif` mesh drawn (resolved like any prop nif).                                                                                                                                                                                                                                                   |
| `fSphereScale`       | The drawn size relative to the zone: `1` matches it, `< 1` draws a smaller marker inside the real zone so the hint doesn't fill your whole reach. The hit test always uses the full `tZone`.                                                                                                          |
| `sSphereOrientation` | Which way it faces: `body` (default — turns with the direction your body faces, kept upright, so a sphere on your body looks still as you turn), `hmd` (turns with the direction your head faces), or `world` (holds still in the world). Only a patterned look such as `debug` shows the difference. |

Editing any of these while the game runs redraws the sphere with the new look the next frame it's shown.

**Presets** for `sSphereStyle`:

- `<color>-<strength>` — the glowing-rim activation sphere. Colors: `white`, `gray`, `cyan`, `green`,
  `purple`, `red`, `amber`, `gold`. Strengths: `full`, `medium`, `subtle`, `low`. For example
  `cyan-subtle`; `white-subtle` is the default.
- `debug` — the evenly lit, two-sided debug sphere: a light grid.

To go beyond the presets, start from the closest one and override values, e.g. `sSphereStyle = white-subtle`
with `sSphereColor = 255,105,180` for a subtle pink sphere.

A common recipe is a small proximity dot — `sShowSphere = wheninside` with `fSphereScale = 0.5` — that
appears only as your hand nears the zone. To hide the visuals entirely, set `sShowIcon` and `sShowSphere`
to `never`.

## Example

```ini
[MyMod_SomeActivation]
tZone = 0,0,0;0,0,0;18
sPrimaryBinding = offhand tap trigger suppress
sSecondaryBinding = offhand longpress trigger suppress
sEntryHaptic = Tick
sPrimaryHaptic = DoubleClick
sSecondaryHaptic = Click
sShowIcon = whenavailable
sIcon = f4cf\activation-icon-ring.dds
sShowSphere = wheninside
sSphereStyle = cyan-subtle
```

A tap of the off-hand trigger while it's near the HMD fires the primary gesture (its trigger hidden
from the game); a long-press fires the secondary. A ring marks the zone whenever the gesture can fire, and
the sphere lights up once your hand is in it.

---

# For mod authors

**A single binding** is `f4cf::vrcf::InputBinding`, evaluated each frame with one
`VRControllers.check(binding)`. Load one from the INI with `ConfigBase::getInputBindingValue(...)`, or
parse a string directly with `parseInputBinding(...)`. The `suppress` flag rides on the binding
(`InputBinding::suppress`); a consumer that wants exclusive use of the button applies it via
`VRControllersSuppress`. Full API, code examples, and the button map are in
[`src/vrcf/README.md`](../src/vrcf/README.md).

**An activation sphere** is two types:

- `f4cf::f4vr::WandActivationConfig` — the authored bundle (zone + bindings + haptics + when each visual shows + the icon and sphere styles).
  Load one whole INI section with `ConfigBase::loadWandActivationConfig(ini, "SectionName", defaults)`.
- `f4cf::f4vr::WandActivationSphere` — the runtime zone. Drive it each frame with `onFrameUpdate(frame,
onActivated)`, composing the per-frame `Frame` from your `WandActivationConfig` (gating a binding off
  for the frame by passing the disabled binding, and re-anchoring the zone if needed). It handles the
  proximity test, owner-keyed suppression, haptics, cooldown, and the visuals. `Frame::showZone` is a
  tuning override that draws the sphere at the zone's true size whatever the config says.

The icon's look is an `f4cf::f4vr::ActivationIconStyle` (texture, tint, size). Icons are drawn by the
framework's primitive overlay (`src/render`), on one layer shared by every sphere, so they need no mesh.

The sphere's look is an `f4cf::f4vr::SphereStyle` (`src/f4vr/SphereStyle.h`): a mesh plus the values set
at runtime on its effect shader — color, alpha, glow, falloff opacities, and the texture. Because the
texture is set by code, the meshes name no mod, and **every mod ships the same files** from the
framework's `mod-template` under its own folder:

- `Meshes\<ModName>\f4cf\activation-sphere.nif` (the one mesh every preset draws on)
- `Textures\<ModName>\f4cf\activation-sphere.dds`, `debug-sphere.dds`
- `Textures\<ModName>\f4cf\activation-icon-hand.dds` (the default icon), `activation-icon-ring.dds`, `activation-icon-circle.dds`

See [`src/f4vr/README.md`](../src/f4vr/README.md) for the `WandActivationSphere` API and an example.

**Telling the player what is bound.** Do not hard-code a button name in your UI - it goes stale the
moment someone edits the INI. `f4cf::vrui::appendBindingPrompt(spans, binding)`
([`src/vrui/BindingPrompt.h`](../src/vrui/BindingPrompt.h)) turns an `InputBinding` into a text span
for a `UITextPanel` row, drawing the controller icon the binding actually resolves to - primary and
offhand mapped to the left or right controller by the player's handedness, and buttons named the way
the controller prints them (the runtime's `A` is `X` on the left one). A binding no shipped icon
covers - a chord, a touch, the system button - falls back to words (`bindingLabel`), and
`samePrompt(a, b)` tells whether two bindings read the same, for a caller listing several at once.
The icons are the `Textures\<ModName>\f4cf\bindings\` set that `mod-template` ships; a mod that
did not copy them simply gets the words.
