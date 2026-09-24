# VR player rotation — turning the player where the game won't

`f4cf::f4vr::PlayerRotation` turns the player the way the player's own VR comfort settings say, from
anywhere — including the states the game itself refuses to turn in.

Files: [`PlayerRotation.h`](../../src/f4vr/PlayerRotation.h) /
[`PlayerRotation.cpp`](../../src/f4vr/PlayerRotation.cpp), with the three engine addresses it uses in
[`F4VROffsets.h`](../../src/f4vr/F4VROffsets.h).

Everything below was read off `Fallout4VR.exe` **1.2.72.0** and is quoted as image-relative offsets
(`REL::Offset`, base `0x140000000`). None of it has an address-library row, and none of it is in
CommonLibF4VR — the bundled headers have no VR turning surface at all.

## Why a mod needs this

Vanilla turning is a *player-controls input handler*: the thumbstick handlers at `0xFC8887` /
`0xFC8A62` call the VR turn handler at `0xFCF5E0` when the stick is pushed past 0.5. Anything that
takes the player controls away — a dialogue scene, most menus — takes turning with it, and the player
is pinned facing wherever they were. A mod can still see the stick, because it reads OpenVR directly
rather than waiting for the game to hand it an input event.

Turning in VR rotates the **VR world (room) transform**, not the actor. The actor's heading follows
the HMD, which is why there is no `SetHeading` anywhere in this story.

## The settings the game turns by

`Fallout4Prefs.ini` `[VR]`, read straight off the value of the static `Setting` objects the engine
parses them into (value at `Setting+0x8`) — the same trick `isLeftHandedMode()` uses, and far cheaper
than an `INIPrefSettingCollection` lookup by name. The in-game settings menu writes these very
addresses (e.g. `0xBD9D5A` for the rotation type), so a change made in the menu is picked up live with
no reload.

| Setting | Value offset | Meaning |
| --- | --- | --- |
| `iRotationType:VR` | `0x37D5E78` | turning style, see below |
| `fRotationAngle:VR` | `0x37D5E90` | snap angle in degrees; the menu offers 30 / 45 / 60 / 90 |
| `fRotationSpeed:VR` | `0x37D5EA8` | smooth turn, degrees/second; the menu slider lerps `fRotationSpeedMin` (`0x37D5EC0`, 50) → `fRotationSpeedMax` (`0x37D5ED8`, 200) |
| `fAngleSnapSmoothingSpeed:VR` | `0x37CFD98` | how fast a smoothed snap travels, degrees/second (360) |

`iRotationType` — decoded from the branch structure of the turn handler, not from a symbol:

| Value | `VRRotationType` | Behaviour |
| --- | --- | --- |
| 0 | `None` | turning off; the handler computes the angle and calls nothing |
| 1 | `SnapSmoothed` | snap by `fRotationAngle`, interpolated there at `fAngleSnapSmoothingSpeed` |
| 2 | `SnapInstant` | snap in one frame, with a comfort fade |
| 3 | `Smooth` | continuous turn at `fRotationSpeed` while the stick is held |

## What the engine does, and why the framework does it differently

The turn handler reads the current yaw, adds `±fRotationAngle · DEG2RAD`, and then, by style:

- **type 1** → `0xEFA920(PlayerCharacter*, float targetYawRad)` parks the normalized target yaw in
  `PlayerCharacter+0x8C8` and sets flag bits `0x60` at `+0x12A4`.
- **type 2** → `0xEFA980(PlayerCharacter*, float targetYawRad)` writes the yaw immediately, runs the
  fade over the two camera objects (`0x5AC8EB0` / `0x5AC72B8`), and sets flag `0x40`.
- **type 3** → integrates the yaw itself every frame and writes it directly.

A **per-frame applier**, `0xEF7180(PlayerCharacter*, float deltaTime, bool skipRotation)` called from
the player update, finishes a type-1 snap: while `+0x12A4 & 0x20` it steps the yaw toward `+0x8C8` at
`fAngleSnapSmoothingSpeed`, clearing the bit once within `1e-4` of the target.

**The flags are the catch.** Bit `0x40` is the once-per-flick latch, and it is cleared in exactly two
places (`0xFC88A3`, `0xFC8A75`) — both inside the vanilla thumbstick handler, when the stick
re-centres. Both engine setters return early when it is set. So calling them from a state where that
handler is out of the loop, which is precisely the state a mod turns the player for, works once and
is then silently dropped forever.

Hence `PlayerRotation` skips those entry points and drives the transform itself, through the same
get/set pair the engine's own smooth turn uses:

- `VRWorld_GetEulerAngles` (`0x1C11B00`) — Euler decomposition of the rotation matrix at
  `g_vrWorldData` (`0x59429C0`) `+0x210`, a SIMD-padded 3×3 (three rows of four floats). The yaw comes
  back in the first out-param, in radians.
- `VRWorld_SetYaw` (`0x1BA7780`) — rebuilds the world rotation from a single yaw and writes it back.

Use that pair and only that pair: a `get → add → set` round trip through it is sign-convention safe.
A second decomposition exists at `0x1C0FED0` (the snap path's), with a *different* Euler convention.

Two further notes on reaching into these structures:

- `PlayerCharacter+0x8C8` and `+0x12A4` are **VR layout**, and the bundled CommonLibF4 header now
  agrees rather than contradicting them: both land inside its VR-only blocks (`0x8C8` in
  `vrPlayerStateFront`, `0x12A4` in `vrPlayerStateBack`), so neither collides with a named field
  any more — the `pipboyAnimSubGraph` that used to sit on `0x8C8` is at VR `0xD38`. Nothing in the
  framework reads these two; they are documented so the next person recognises them in a disassembly.
- `g_vrWorldData` is the global other VR mods call `vrDataStruct`.

## Using it

```cpp
// Once per frame, from wherever the turn should be available:
const auto thumbstick = vrcf::VRControllers.getThumbstickValue(vrcf::Hand::Primary);
f4vr::PlayerRotation::turnByThumbstick(thumbstick.x);
```

`turnByThumbstick()` is the whole feature in one call: a snap per flick for the snap styles (with the
stick latch, so holding the stick does not spin), a continuous turn for the smooth style, nothing at
all when turning is off, and it advances an in-flight smoothed snap on the way through. Feed it the
raw axis; the threshold defaults to the 0.5 the vanilla handler asks for.

For anything that is not a thumbstick, `snapTurn(right)` / `smoothTurn(right, dt)` apply the
configured style directly, and `getYaw()` / `setYaw()` / `rotateBy()` bypass the style entirely.
`cancel()` drops an in-flight snap and un-latches the stick — worth calling when whatever was driving
the turn goes away mid-snap (the menu closed, a save was loaded). A caller driving the turn by some
other route still needs `onFrameUpdate()` each frame for a smoothed snap to finish.

Main thread only.

## Two deliberate departures from vanilla

- **No comfort fade on the instant-snap style.** The fade lives inside the engine setter this code
  deliberately does not call, so type 2 snaps without the blink and reads a little harsher.
- **Frame delta is measured here** with `steady_clock`, not taken from the engine. The global the
  engine's own smooth turn multiplies by (`0x5BBC050`) is read in hundreds of places and behaves like
  a frame delta, but it was never positively identified, and a wrong guess would surface as a
  subtly wrong turn speed rather than as an error. A hitch over 100 ms yields no turn rather than a
  lurch.
