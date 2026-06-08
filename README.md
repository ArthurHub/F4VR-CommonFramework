# F4VR Common Framework

A common framework for Fallout 4 VR F4SE mods using [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).

## Documentation

- [Wiki](https://github.com/ArthurHub/F4VR-CommonFramework/wiki) — step-by-step guide to creating a mod.
- [`src/README.md`](src/README.md) — source-tree overview and how the subsystems fit together. Each
  subsystem folder has its own README with usage details:
  [common](src/common/README.md) ·
  [f4vr](src/f4vr/README.md) ·
  [f4sevr](src/f4sevr/README.md) ·
  [vrcf](src/vrcf/README.md) ·
  [vrui](src/vrui/README.md)

## Mods Built on the Framework

- [FRIK - Full Player Body with IK](https://github.com/rollingrock/Fallout-4-VR-Body)
- [Comfort Swim VR](https://github.com/ArthurHub/F4VR-ComfortSwim)
- [Immersive Flashlight VR](https://github.com/ArthurHub/F4VR-ImmersiveFlashlight)

## Creating Mod using F4VR-CommonFramework

Follow the [wiki](https://github.com/ArthurHub/F4VR-CommonFramework/wiki)

**TL;DR:**

1. Setup git submodule:

```
mkdir external
git submodule add https://github.com/ArthurHub/F4VR-CommonFramework.git external/F4VR-CommonFramework
git submodule update --init --recursive
```

2. Copy all/relevant files from `mod-template` folder.

3. Rename all occurrences of "MyMod" in the template files.

## Building F4VR-CommonFramework

Clone repo and setup CommonLibF4 submodule:

```
git clone https://github.com/ArthurHub/F4VR-CommonFramework.git
cd F4VR-CommonFramework
git submodule update --init --recursive
```

Create VS2022 solution:

```
cmake --preset default
```

- Open `.../Fallout-4-VR-Body/build/ImmersiveHUD.sln` in VS2022.
  - Build and debug in VS as usual
  - Any project changes should be done in `CMakeLists.txt`

## Credit/Thanks

Modding is built on the community.
I couldn't do it without people public code.

- Ryan-rsm-McKenzie-alandtse and other [CommonLib](https://github.com/alandtse/CommonLibVR/tree/vr) contributors
- RollingRock-alandtse-shizof-CylonSurfer for open source mods like FRIK, VirtualHolsters, etc. to learn and adopt code from.
