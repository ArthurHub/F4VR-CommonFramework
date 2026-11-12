# Activation spheres

A gesture in a mod built on F4VR-CommonFramework is a spot you reach a hand to, such as your chest, your
head or your other hand, and a button you press there. The spot is an **activation sphere**, a zone that
moves with what it is attached to. While your hand is in it:

- a vibration tells you it is there,
- the gesture's buttons are taken from the game, so the trigger doesn't also fire your weapon,
- pressing one fires the gesture,
- an icon, a sphere, the icons of its buttons, or any of them together, can mark the zone.

Each gesture has a section of its own in the mod's config file
(`Documents\My Games\Fallout4VR\Mods_Config\<ModName>\<ModName>.toml`). The section's name is up to the
mod, and the keys in it are the same in every mod. A key left out keeps the mod's default, and a change
applies as soon as you save the file.

## Quick start

```toml
[MyMod_HeadActivation]
tZone = { pos = [0, 0, 0], scale = 18 }
kPrimaryBinding = { hand = "offhand", type = "tap", button = "trigger", suppress = true }
kSecondaryBinding = { hand = "offhand", type = "longpress", button = "trigger", suppress = true }
sEntryHaptic = "Tick"
sShowIcon = "wheninside"
sShowSphere = "never"
```

- **Hide the icon:** `sShowIcon = "never"`. The vibration still tells you when your hand is in the zone.
- **See the zone:** `sShowSphere = "always"` draws it, at its real size while `fSphereScale` is `1`.
- **Move or resize it:** `tZone`'s `pos` moves it from what it is attached to, and `scale` is its
  diameter, in game units (one is about 1.4 cm).
- **Change the button:** `kPrimaryBinding` and `kSecondaryBinding` are [bindings](input-binding.md).
  Keep `suppress = true` in them.
- **Turn the gesture off:** set both bindings to `{ type = "none" }`. Its icon and sphere go with it,
  unless they are set to `always`.
- **Stop the vibration:** `sEntryHaptic = "none"` as your hand enters, `sPrimaryHaptic = "none"` and
  `sSecondaryHaptic = "none"` when the gesture fires.

## Zone and bindings

| Key                 | What it is                                                                                  |
| ------------------- | ------------------------------------------------------------------------------------------- |
| `tZone`             | `{ pos = [x, y, z], scale = diameter }`. The zone is a sphere, so a `rot` does nothing      |
| `tZonePA`           | The zone in power armor, for a mod whose zone moves there. Left out, `tZone`                |
| `kPrimaryBinding`   | The gesture's [binding](input-binding.md)                                                   |
| `kSecondaryBinding` | A second one, often a long press of the same button. `{ type = "none" }` for no second one |

## Vibration

| Key                | Plays                               |
| ------------------ | ----------------------------------- |
| `sEntryHaptic`     | Once, as a hand enters the zone     |
| `sPrimaryHaptic`   | When `kPrimaryBinding` fires        |
| `sSecondaryHaptic` | When `kSecondaryBinding` fires      |

Each is `none` or one of: `Tick`, `Click`, `DoubleClick`, `TripleClick`, `Success`, `Warning`, `Error`,
`Notification`, `Start`, `Stop`, `RampUp`, `RampDown`, `Heartbeat1`, `Heartbeat2`, `Heartbeat3`, `Buzz`,
`MidBuzz`, `LongBuzz`.

## When the icon and sphere show

`sShowIcon`, `sShowSphere` and `sShowBindingIcon` each take one of:

| Value           | Shows                                                                                   |
| --------------- | --------------------------------------------------------------------------------------- |
| `never`         | Never. The sphere's and the binding icon's default                                      |
| `always`        | All the time, which helps while you place the zone                                      |
| `wheninside`    | While your hand is in the zone. The icon's default                                      |
| `whenavailable` | While the gesture can do something: one that needs a drawn weapon, only while one is drawn |

They are only looks. The gesture works the same with both hidden, and a smaller sphere doesn't make a
smaller zone.

## The icon

A small image at the zone's center that turns to face you. It is drawn over the world, so it shows
inside your body or on a prop.

| Key          | What it is                                                                                                                                  |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `sIcon`      | A `.dds` in the Data folder. `Textures\<ModName>\f4cf\` has `activation-icon-hand.dds` (the default), `activation-icon-ring.dds` and `activation-icon-circle.dds` |
| `cIconColor` | The tint, `{ rgb = [r, g, b], alpha = a }`, each 0 to 255                                                                                   |
| `fIconSize`  | The size of its longer side, in game units, `1` by default                                                                                 |

An icon of your own works best as a white shape on a transparent background, so the tint colors it.

## The binding icon

The icons of the buttons that fire the gesture, such as the left trigger with a clock for a long press, in a
row that faces you. Shown beside the icon, it says which button to press. With `sShowIcon = "never"` and the
row at the zone's center, it takes the icon's place.

| Key                 | What it is                                                                                                                                                  |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sShowBindingIcon`  | When it shows, see above. `never` by default                                                                                                               |
| `sBindingIcons`     | Which bindings: `inside` (the default: those of the hand in the zone), `all`, `primary` or `secondary`. A binding the gesture can't use right now never shows |
| `tBindingIcon`      | `{ pos = [x, y, z], scale = size }`: `x` right, `y` up and `z` toward your eyes from the zone's center, and `scale` each icon's size, in game units           |
| `cBindingIconColor` | The tint, `{ rgb = [r, g, b], alpha = a }`, each 0 to 255. White by default, as the icons are drawn                                                       |

With a positive `x` the row starts there and grows to the right, away from the icon; with a negative `x` it
ends there; at `0` it is centered. A button with no icon, such as the system button, shows nothing.

## The sphere

`sSphereStyle` picks a preset, and the other keys change single values of it.

| Key                  | What it is                                                                                                   |
| -------------------- | ------------------------------------------------------------------------------------------------------------ |
| `sSphereStyle`       | A preset, see below                                                                                          |
| `cSphereColor`       | `{ rgb = [r, g, b], alpha = a }`, each 0 to 255. The presets' `alpha` is 27 to 40                            |
| `fSphereGlow`        | Brightness, 0 to 1. The presets are 0.4 to 0.5, `debug` is 1                                                 |
| `aSphereFalloff`     | The opacity from the middle to the edge, `[center, rim]`, such as `[0.045, 1.0]`. Equal values draw it evenly |
| `fSphereScale`       | The drawn size against the zone: `1` matches it, `0.5` is a marker half its size                             |
| `sSphereOrientation` | `body` (the default: turns with your body), `hmd` (turns with your head) or `world`. Only `debug` shows it  |
| `sSphereTexture`     | A `.dds` in the Data folder. Left out, the preset's. `none`, the mesh's own                                  |
| `sSphereNif`         | A `.nif` in the Data folder. Left out, `Meshes\<ModName>\f4cf\activation-sphere.nif`                         |

The presets:

- **`<color>-<strength>`:** a glowing rim. The colors are `white`, `gray`, `cyan`, `green`, `purple`,
  `red`, `amber` and `gold`, the strengths `full`, `medium`, `subtle` and `low`. `white-subtle` is the
  default.
- **`debug`:** an evenly lit grid.

Start from the closest preset and change one value: `sSphereStyle = "white-subtle"` with
`cSphereColor = { rgb = [255, 105, 180] }` is a subtle pink. A small dot that shows as your hand arrives
is `sShowSphere = "wheninside"` with `fSphereScale = 0.5`.

## For mod authors

A gesture's section is an `f4vr::WandActivationConfig`, loaded with `loadWandActivationConfig`, and its
zone an `f4vr::WandActivationSphere`, driven each frame. Their API, and the mesh and textures every mod
ships for them: [`src/f4vr/README.md`](../src/f4vr/README.md#activation-spheres).
