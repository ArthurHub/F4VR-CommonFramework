# `vrcf/` — VR Controller Framework

Namespace: `f4cf::vrcf`

Wraps OpenVR to give mods clean, debounced controller input — and a way to hide that input from the
game when the mod wants a button for itself. OpenVR is resolved via vcpkg, falling back to the
vendored headers in [`external/openvr/`](../../external/openvr/).

> Part of the [F4VR Common Framework](../README.md) source tree.

## Components

| Class | Global | Purpose |
|-------|--------|---------|
| [`VRControllersManager`](VRControllersManager.h) | `VRControllers` | Per-frame snapshot of both controllers: buttons, triggers, grip, thumbsticks, heading, haptics. |
| [`VRControllersSuppressor`](VRControllersSuppressor.h) | `VRControllersSuppress` | Hides physical buttons/axes from the game (and other mods) while your DLL still reads the raw press. |

Both are driven for you by `ModBase` each frame — you only call the read/suppress methods.

## Hands & buttons

`Hand` is logical: `Primary` / `Offhand` map to the player's dominant / non-dominant hand and respect
left-handed mode automatically. `Right` / `Left` are physical. Most methods also accept a raw
`vr::ETrackedControllerRole`. Buttons are OpenVR `vr::EVRButtonId` (see `VRButtonId` for the F4VR
button map).

## Reading input

```cpp
#include "vrcf/VRControllersManager.h"
using namespace f4cf::vrcf;

// In onFrameUpdate():
if (VRControllers.isPressed(Hand::Primary, vr::k_EButton_SteamVR_Trigger)) { /* just pressed */ }
if (VRControllers.isLongPressed(Hand::Offhand, vr::k_EButton_Grip)) { /* held past threshold */ }
if (VRControllers.isDoublePressed(Hand::Primary, vr::k_EButton_A)) { /* two taps */ }

NiPoint2-like axis = VRControllers.getThumbstickValue(Hand::Primary);   // .x / .y in [-1, 1]
float yaw = VRControllers.getControllerRelativeHeading(Hand::Primary);  // wand-directional movement

VRControllers.triggerHaptic(Hand::Primary, 0.1f /*sec*/, 0.3f /*intensity*/);
```

Press-state helpers (all debounced):

| Method | Fires when |
|--------|-----------|
| `isPressed` | the frame the button goes down |
| `isPressHeldDown(…, minHold)` | held this frame (optionally past a minimum duration) |
| `isReleased(…, maxHold)` / `isReleasedShort` | the frame it goes up (optionally only if held briefly) |
| `isLongPressed(…, duration)` | held past a threshold (fires once) |
| `isDoublePressed(…, maxInterval)` | second of two presses within the interval |
| `isTouching` | capacitive touch without a press |

Thumbstick/axis gestures: `isThumbstickPressed(hand, Direction)` and `getThumbstickPressedDirection`
turn analog deflection into discrete up/down/left/right events with a threshold + cooldown.

## Config-driven bindings

When the *key binding itself* should be configurable (any hand, any button, any kind of press), don't
hard-code a specific `isPressed`/`isLongPressed`/… call. Describe the binding as data with
[`InputBinding`](VRControllersManager.h) and evaluate it with a single `VRControllers.check(binding)`.

```cpp
#include "vrcf/VRControllersManager.h"
using namespace f4cf::vrcf;

InputBinding openMenu;
openMenu.hand = Hand::Offhand;
openMenu.type = ActivationType::LongPress;   // Press / HoldDown / Release / DoublePress / Touch / AxisDirection
openMenu.button = vr::k_EButton_Grip;
openMenu.duration = 0.6f;                     // meaning depends on `type`; 0 = per-type default
openMenu.modifier = InputModifier{ vr::k_EButton_SteamVR_Trigger };       // optional chord on the same hand
// openMenu.modifier = InputModifier{ vr::k_EButton_Grip, Hand::Offhand }; // ...or pin it to a specific hand

// In onFrameUpdate():
if (VRControllers.check(openMenu)) { /* binding triggered */ }
```

`check()` dispatches by `type` to the matching read method above; for `AxisDirection` it uses the
`axis` + `direction` + `threshold` + `cooldown` fields instead of `button`. The optional `modifier`
must be held down for the binding to fire — on the binding's own hand by default, or on the
`InputModifier::hand` you specify.

To load bindings from INI, [`InputBindingParser.h`](InputBindingParser.h) parses a forgiving,
case-insensitive line into an `InputBinding` (token helpers `parseHand` / `parseActivationType` /
`parseButton` / `parseAxis` / `parseDirection` are also exposed). If your config derives from
`ConfigBase`, prefer its `getInputBindingValue(ini, section, key, default)` helper — it reads the key,
falls back to your default, and logs a warning on a malformed value (same pattern as
`getTransformValue` / `getHandPoseValue`):

```cpp
// In your Config's loadIniConfigInternal(const CSimpleIniA& ini):
openMenuBinding = getInputBindingValue(ini, "Controls", "sOpenMenu",
    InputBinding{ Hand::Offhand, ActivationType::LongPress, vr::k_EButton_Grip });

// Or parse a string directly anywhere else:
#include "vrcf/InputBindingParser.h"
auto chord = parseInputBinding("primary press trigger +offhand:grip"); // -> std::optional<InputBinding>
```

Format: `"<hand> <type> <button> [duration] [+[hand:]modifier]"`. Examples: `"primary press trigger"`,
`"left double a"`, `"primary thumbstick up"`, `"right axis trigger up 0.7"`,
`"offhand longpress grip 0.6 +trigger"`. See the header doc comment for the full grammar and aliases.

## Suppressing input

`VRControllersSuppressor` hooks `IVRSystem::GetControllerState[WithPose]` (vtable slots 34/35). The
game and other mods see the filtered state; your DLL keeps reading the real hardware via a
caller-module check — so you can detect a press and decide, per frame, whether to swallow it.

It is **owner-keyed**: every call carries a `std::string_view key`. A key only undoes its own
suppression, and the effective mask is the *union* of all owners, so independent subsystems never
fight over a button.

```cpp
#include "vrcf/VRControllersSuppressor.h"
using namespace f4cf::vrcf;

// Claim the primary trigger for your mod (game stops seeing it; you still do):
VRControllersSuppress.suppress("MyMod", Hand::Primary, vr::k_EButton_SteamVR_Trigger);

// ... later, give it back:
VRControllersSuppress.release("MyMod", Hand::Primary, vr::k_EButton_SteamVR_Trigger);
// or drop everything this owner suppressed:
VRControllersSuppress.release("MyMod");
```

Convenience: `suppressAll` / `setAllSuppressed` / `setAllAxesSuppressed` (per-hand and both-hands),
`isSuppressed(hand, button)` (aggregate) vs `isSuppressedBy(key, …)`, and `reset()` to wipe every
owner (used by `ModBase` on save reload).

### Rules — read before using

- **Main thread only** for `suppress` / `release` / `reset`. The hook runs on the OpenVR polling
  thread and reads only the atomic per-hand mask; all owner bookkeeping is main-thread.
- **Never call F4SE / CommonLibF4VR from the hook.** Decide on the main thread, flip a key.
- Suppression is **persistent** until released — it does not auto-clear per frame.
- Analog-backed buttons (trigger / grip / thumbstick-click) auto-suppress their backing `rAxis` too,
  so the game can't re-derive the press.

Deep dive: `knowledge-base/commonframework_vr_input_suppression.md` in the modding reference library.
