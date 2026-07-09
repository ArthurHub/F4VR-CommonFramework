<!--
  README template for a mod built on the F4VR Common Framework.
  Fill in the TODOs and the <angle-bracket> placeholders, delete this comment, and
  delete any sections you don't need. "MyMod" / "My Mod" are replaced automatically
  when you follow the rename step in the "Creating a new mod" guide.
-->

# My Mod

> TODO: one-line tagline — what My Mod does, for Fallout 4 VR.

TODO: a short paragraph on what the mod does and why a player wants it.

Built on the [F4VR Common Framework](https://github.com/ArthurHub/F4VR-CommonFramework).

## Features

- TODO
- TODO
- TODO

## Requirements

- [Fallout 4 VR](https://store.steampowered.com/app/611660/)
- [Fallout 4 VR Script Extender (F4SEVR)](https://f4se.silverlock.org/)
- TODO: any other required mods (e.g. [FRIK](https://github.com/rollingrock/Fallout-4-VR-Body))

## Installation

**With a mod manager (recommended):** install the release archive with Mod Organizer 2 or Vortex and
enable it. Launch the game through F4SEVR (`f4sevr_loader.exe`).

**Manual:** extract the archive into your `Fallout 4 VR` folder so the plugin lands at
`Data\F4SE\Plugins\MyMod.dll`, then launch through F4SEVR.

## Configuration

Settings live in an INI created on first run at:

```
%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\MyMod\MyMod.ini
```

Edits apply **live** while the game is running — no restart needed. Put user overrides that should
survive updates in `MyMod_Custom.ini` in the same folder.

- Controller-binding syntax: [input-binding guide](https://github.com/ArthurHub/F4VR-CommonFramework/blob/main/docs/input-binding.md)
- Logging / debug keys: [debug-config guide](https://github.com/ArthurHub/F4VR-CommonFramework/blob/main/docs/debug-config.md)

TODO: document your mod's own settings here.

## Building from source

This mod is built with the [F4VR Common Framework](https://github.com/ArthurHub/F4VR-CommonFramework).
See the framework's [Creating a new mod](https://github.com/ArthurHub/F4VR-CommonFramework/blob/main/docs/creating-a-new-mod.md)
guide for the full toolchain (VS 2022/2026, CMake 4.2+, `VCPKG_ROOT`). Quick version:

```sh
git clone --recurse-submodules <your-repo-url>
cd MyMod
cmake --preset default      # generates build/MyMod.sln
```

Open the solution and build. A Release build packages a versioned `.7z` under `build/package/`.

## Support

TODO: link your issue tracker / Nexus posts / Discord.

- [Nexus Mods](https://www.nexusmods.com/fallout4/mods/<ID>)
- [Report a bug](https://github.com/<org>/MyMod/issues)

## Credits

- Built on the [F4VR Common Framework](https://github.com/ArthurHub/F4VR-CommonFramework) by Arthur T.
- TODO: other authors / assets you built on.

## License

TODO: state your license. This template ships under [GPL-3.0-or-later](LICENSE) — keep it or replace
`LICENSE` with your own.
