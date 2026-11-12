# Controller bindings

A binding is one of a mod's controls: the hand, the button, and how you press it. Every mod built on
F4VR-CommonFramework writes its bindings the same way, in its config file. Gestures, where you reach a
hand to a spot and press there, are in [Activation spheres](activation-spheres.md).

- **File:** `Documents\My Games\Fallout4VR\Mods_Config\<ModName>\<ModName>.toml`. Which bindings it has,
  and their defaults, is up to the mod.
- **Live:** save the file while the game runs and the change applies at once.
- **Mistakes:** a binding that can't be read is logged with what is wrong, and the mod uses its default
  for it. A typo that breaks the whole file is logged with its line, and every setting keeps its value
  until it is fixed.

## Quick start

```toml
kOpenMenu = { hand = "offhand", type = "longpress", button = "grip" }
```

- **Change the hand:** `primary` (your weapon hand), `offhand` (the other one), or `any`.
- **Change the press:** `tap`, `press` (the moment it goes down), `longpress` (held 0.6 s), or `double`.
- **Change the button:** `trigger`, `grip`, `a` (A / X), `b` (B / Y), or `thumbstick` (pressed in).
- **Hold it longer:** add `duration = 1.2`.
- **Turn it off:** `kOpenMenu = { type = "none" }`. Deleting the line brings back the mod's default
  instead.

Words can be in any case. `primary` and `offhand` follow the game's left-handed mode.

## The binding

| Key          | What it is                                                                                    |
| ------------ | --------------------------------------------------------------------------------------------- |
| `hand`       | Which controller, see [Hand](#hand). Every type but `none` needs it                           |
| `type`       | How it is pressed, see [Type](#type)                                                          |
| `button`     | Which button, see [Button](#button). Every type but `axis` and `none` needs it                |
| `duration`   | Seconds. What it means depends on the type                                                    |
| `direction`  | For `axis`: which way the stick is pushed, see [Thumbstick](#thumbstick)                      |
| `axis`       | For `axis`: `thumbstick` when left out, or `trigger` / `grip`                                 |
| `threshold`  | For `axis`: how far it is pushed, 0 to 1, `0.85` when left out                                |
| `accelerate` | For `axis`: `false` keeps a held push repeating at one pace                                   |
| `modifier`   | A button held down at the same time, see [Chords](#chords)                                    |
| `suppress`   | `true` hides the button from the game, see [Suppressing the game input](#suppressing-the-game-input) |

A misspelled key or word, a key the type doesn't take, or a value of the wrong kind makes the binding
invalid. The log says which, and what fits there: `'thumbstik' is not a button, one of trigger, grip, a,
b, thumbstick, menu, system`.

### Hand

| Word             | Controller                                |
| ---------------- | ----------------------------------------- |
| `primary`        | Your weapon hand                          |
| `offhand`        | The other hand                            |
| `left` / `right` | That controller, whichever hand you use   |
| `any`            | Either: the hand that presses it fires it |

Use `primary` and `offhand` over `left` and `right`, so the binding follows left-handed mode. A mod can
narrow `any` to the hand that matters: a menu's Back to the hand that points at the menu, a gesture to
the hand in its zone.

### Type

| Word        | Fires                                  | `duration`                                       |
| ----------- | -------------------------------------- | ------------------------------------------------ |
| `tap`       | On a quick press and release (< 0.3 s) |                                                  |
| `press`     | The moment the button goes down        |                                                  |
| `release`   | The moment it comes up                 | The longest hold that still counts (any)         |
| `longpress` | Once, when held long enough            | How long to hold (`0.6`)                         |
| `double`    | On the second of two quick presses     | The longest gap between them (`0.4`)             |
| `hold`      | Every frame while held                 | How long it is held before it starts (`0`)       |
| `touch`     | While the button is touched, not pressed |                                                |
| `axis`      | When the stick is pushed               | Not used, see [Thumbstick](#thumbstick)          |
| `none`      | Never: the binding is off              |                                                  |

`click` also means `press`, and `holddown` means `hold`.

### Button

| Word         | Button                               |
| ------------ | ------------------------------------ |
| `trigger`    | Trigger                              |
| `grip`       | Grip                                 |
| `a`          | A / X, the lower face button         |
| `b`          | B / Y, the upper face button (`menu`) |
| `thumbstick` | The thumbstick, pressed in           |
| `system`     | System button                        |

### Thumbstick

```toml
kScrollUp = { hand = "primary", type = "axis", direction = "up" }
kAdjust = { hand = "primary", type = "axis", direction = "updown" }
```

- **Direction:** `up`, `down`, `left` or `right`. For the stick moved rather than pushed one way: `any`,
  `updown` or `rightleft`, whose prompt shows the stick with arrows around it.
- **Threshold:** how far the stick has to go, 0 to 1. `0.85` when left out.
- **Held, it repeats:** a second step after about a third of a second, then faster the longer you hold
  it: twice as fast after 1 s, four times after 2.5 s. `accelerate = false` keeps one pace, for an action
  that must not run away, such as switching tabs. Letting go, or pushing another way, starts over.
- **Trigger or grip as an axis:** `axis = "trigger"` reads how far it is squeezed, not the button:
  `{ hand = "right", type = "axis", axis = "trigger", direction = "up", threshold = 0.7 }`.

### Chords

A `modifier` is a button that has to be held down while the binding fires. It is on the binding's own
hand, unless it names a `hand`:

```toml
kFastTravel = { hand = "primary", type = "press", button = "trigger", modifier = { hand = "offhand", button = "grip" } }
```

`modifier = { button = "grip" }` holds the grip of the same hand.

### Suppressing the game input

`suppress = true` hides the button from the game while the mod uses the binding, so the button doesn't
also do its normal action. It is off by default, and works where the mod applies it: today, an
[activation sphere](activation-spheres.md), while your hand is in its zone. The trigger that grabs a
flashlight then doesn't also fire your weapon.

### From an INI file

A mod that moved its config from an INI file reads your old line (`sOpenMenu = offhand longpress grip`)
into a table the first time it starts, so a changed binding is kept.

## For mod authors

A binding is `vrcf::InputBinding`, read from the config with `getInputBindingValue` and checked each
frame with `VRControllers.check(binding)`: [`src/vrcf/README.md`](../src/vrcf/README.md#config-driven-bindings).
Show it to the player with `vrui::appendBindingPrompt`, never a hard-coded button name:
[`src/vrui/README.md`](../src/vrui/README.md).
