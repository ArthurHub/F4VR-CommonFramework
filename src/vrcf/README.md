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
