# `vrcf/` — VR Controller Framework

Namespace: `f4cf::vrcf`

Wraps OpenVR to give mods clean, debounced controller input — and a way to hide that input from the
game when the mod wants a button for itself. OpenVR is resolved via vcpkg, falling back to the
vendored headers in [`external/openvr/`](../../external/openvr/).

> Part of the [F4VR Common Framework](../README.md) source tree.

## Components

| Class | Global | Purpose |
|-------|--------|---------|
| [`VRControllersManager`](VRControllersManager.h) | `VRControllers` | Per-frame snapshot of both controllers: buttons, triggers, grip, thumbsticks, heading. |
| [`VRControllersSuppressor`](VRControllersSuppressor.h) | `VRControllersSuppress` | Hides physical buttons/axes from the game (and other mods) while your DLL still reads the raw press. |
| [`VRControllersHaptic`](VRControllersHaptic.h) | `VRHaptics` | Haptic feedback: sustained buzzes, a library of named patterns (double-click, ramps, heartbeat, ...), custom sequences. |

All three are driven for you by `ModBase` each frame — you only call the read/suppress/trigger methods.

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
```

Press-state helpers (all debounced):

| Method | Fires when |
|--------|-----------|
| `isPressed` | the frame the button goes down |
| `isPressHeldDown(…, minHold)` | held this frame (optionally past a minimum duration) |
| `isReleased(…, maxHold)` / `isTap` | the frame it goes up (`isTap` = only if held briefly, < 0.3s) |
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
openMenu.type = ActivationType::LongPress;   // Press / Tap / HoldDown / Release / DoublePress / Touch / AxisDirection / Disabled
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

To **disable** a binding, set its value to `none`, `off`, `disabled`, or leave it empty — it parses to a
`Disabled` binding whose `check()` always returns `false`, so the input never fires (no warning is logged).
Note the difference from a *missing* key: an absent key falls back to the code default, while a present-but-empty
value is an explicit "off". Example: `sOpenMenu = none`.

## Haptic feedback

`VRHaptics` plays haptic feedback on either controller — from a simple buzz to shaped patterns so
different operations get a distinct feel. The pattern library is loosely modeled on the Apple Watch
haptic vocabulary.

```cpp
#include "vrcf/VRControllersHaptic.h"
using namespace f4cf::vrcf;

// Simple: constant intensity (0..1) sustained for a duration.
VRHaptics.trigger(Hand::Primary, 0.1f /*sec*/, 0.3f /*intensity*/);

// Named pattern from the library; optional scale softens/strengthens the whole pattern.
VRHaptics.trigger(Hand::Primary, HapticPattern::Success);
VRHaptics.trigger(Hand::Offhand, HapticPattern::Tick, 0.6f);

// Custom pattern: keyframed segments { duration sec, start intensity, end intensity }.
// Intensity is lerped across each segment; zero-intensity segments are silent gaps.
constexpr HapticSegment chargeUp[] = { { 0.4f, 0.1f, 1.0f }, { 0.1f, 0.0f, 0.0f }, { 0.05f, 1.0f, 1.0f } };
VRHaptics.trigger(Hand::Primary, chargeUp);

VRHaptics.stop(Hand::Primary);   // cut playback; stopAll() for both hands
VRHaptics.isPlaying(Hand::Primary);
```

Patterns: `Tick` / `Click` / `DoubleClick` / `TripleClick` (taps), `Success` / `Warning` / `Error` /
`Notification` (outcome signals), `Start` / `Stop` / `RampUp` / `RampDown` (continuous-operation
shaping), `Heartbeat1` / `Heartbeat2` / `Heartbeat3`, `Buzz` / `MidBuzz` / `LongBuzz`. See
[`VRControllersHaptic.h`](VRControllersHaptic.h) for
the intended use of each; `VRControllersHaptic::getPattern(p)` exposes a pattern's keyframes if you
want to tweak one.

A new trigger on a hand replaces whatever was playing on it. Main thread only.
`VRControllers.triggerHaptic(...)` still works and forwards to `VRHaptics.trigger(...)`.

> Why patterns need a per-frame engine: the only haptic API the game's legacy `IVRSystem` exposes is
> `TriggerHapticPulse(device, axis, microseconds)` — a one-shot pulse capped at ~4ms with a 5ms
> re-trigger lockout. The microseconds argument is effectively *intensity* (duty cycle within a
> frame), not duration. `VRHaptics` synthesizes duration, ramps, and multi-tap patterns by re-firing
> a pulse every frame at the pattern's current intensity, scaled to the measured frame delta so an
> intensity feels the same at 70, 90, or 120fps.

## Suppressing input

`VRControllersSuppressor` hooks `IVRSystem::GetControllerState[WithPose]` (vtable slots 34/35). The
game and other mods see the filtered state; your DLL keeps reading the real hardware because
`VRControllersManager` wraps its own poll in a `SelfControllerReadScope` (a thread-local marker the
hook recognizes) — so you can detect a press and decide, per frame, whether to swallow it. The marker
is used instead of a return-address/caller-module check because that breaks when another mod hooks the
same vtable slots outside yours: every call then reaches your hook through the other mod's trampoline,
so the return address always points at *it*, not you.

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
