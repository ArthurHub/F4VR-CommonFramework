# `config/` — TOML config with hot-reload

Namespace: `f4cf` (`ConfigBase`) and `f4cf::config` (the parts it is made of)

`ConfigBase` loads the mod's TOML file (toml++), watches it and loads it again when it changes,
saves values into it without touching its comments or layout, moves a user's file to the version
the mod ships, and holds session overrides that preview a value without writing it. The starter
code (a `Config` class, a read, a subscriber, an override) is in the
[`src/` quick start](../README.md#the-config-derive-from-configbase); this page holds the rules.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`ConfigBase`](ConfigBase.h) | The base class a mod's config derives from: load, watch, save, overrides, subscribers, migration. |
| [`ConfigReaders`](ConfigReaders.h) | The readers of the values that are text or tables: `getTransformValue`, `getHandPoseValue`, `getInputBindingValue`, `getColorValue`, `getStringListValue`, `loadWandActivationConfig`. `ConfigBase` derives from it, so a mod calls them by name. |
| [`OffsetsFiles`](OffsetsFiles.h) | Loading and saving the offsets JSON files (`loadOffsetsFromFilesystem`, `saveOffsetsToJsonFile`, ...). `ConfigBase` derives from it too. |
| [`DebugSection`](DebugSection.h) | The values of the `[Debug]` section, read as `config->debug`, and `DebugAdjustTarget`. |
| [`ConfigFile`](ConfigFile.h) | The file as the mod last loaded or saved it, which decides whether a change on disk is someone else's. |
| [`ConfigValue`](ConfigValue.h) | A typed value, and the type a value given as text takes. |
| [`Overrides`](Overrides.h) | The session overrides with their owners, under their own lock. |
| [`ReloadSubscribers`](ReloadSubscribers.h) | The subscribers to a reload, and which keys a load changed. |
| [`ConfigMigration`](ConfigMigration.h) | The move of a user's values into a newer config, and the INI import. |
| [`ValueParsers`](ValueParsers.h), [`OffsetsJson`](OffsetsJson.h) | The forms of a transform, hand pose, color and list of names, and the offsets JSON. |

Everything but `ConfigBase`, `ConfigReaders`, `OffsetsFiles` and `DebugSection` is plain std and
toml++, unit tested in `tests/`; those four are tested in `tests/linked/`. toml++ is compiled once,
into the framework (`common/TomlImpl.cpp`), and `TOML_HEADER_ONLY=0` is a PUBLIC define, so a mod
uses the framework's toml++ as it is.

## The file

- The path is `%USERPROFILE%\Documents\My Games\Fallout4VR\Mods_Config\{ModName}\{ModName}.toml`.
- The shipped default is embedded in the DLL as RCDATA (resource 101, `IDR_CONFIG`) and written on
  first run.
- A file that is not TOML (a person in the middle of editing it, a string without its quotes) is
  logged with the line and what is wrong, and never written over: the values loaded before stay,
  and before any were, the embedded default's apply. Its other file-watch events are not parsed again
  until it changes, and a save refuses it.
- `switchConfigFile(path)` moves the config to another file at runtime (FRIK's Fallout London VR
  config): it stops the watch, loads the new file as `load()` does (created, imported from its INI or
  updated as needed) and watches it. Its values are not merged over the file before, and the session
  overrides stay.

## Reading values

`config[SECTION]["key"].value_or(default)` gives the default for a key that is missing or of
another type; toml++ reads an integer as a float and as a bool. In the shipped file a float has its
decimal point, a string its quotes, and a Windows path single quotes (no backslash escapes).

A key's type prefix names its form, read with its reader:

| Prefix | Form | Reader |
|--------|------|--------|
| `a` | an array; of strings for a list of names | `getStringListValue` |
| `t` | a transform, `{ pos = [x, y, z], rot = [heading, roll, attitude], scale = s }`, rotation in degrees; a missing key is no move, no rotation or a scale of 1 | `getTransformValue` |
| `c` | a color, `{ rgb = [r, g, b], alpha = a }` in 0..255; a missing key keeps the default's channels | `getColorValue` |
| `h` | a hand pose, `{ thumb = [prox, mid, dist, splay], index, middle, ring, pinky, palm = [pitch, yaw] }`; a missing finger is zeros | `getHandPoseValue` |
| `k` | a controller binding, `{ hand = "offhand", type = "tap", button = "grip" }` ([`docs/input-binding.md`](../../docs/input-binding.md)) | `getInputBindingValue` |

A section and a key are TOML dotted keys (`config::ConfigKey`): `saveConfigValue("Main", "hand.x", 1.5f)`
saves `x` in `[Main.hand]` or in Main's inline table `hand`. Overrides, `getConfigValue`, the readers
and devbench take them the same way, and a name with a dot in it is in quotes (`"\"my.key\""`).

The `[Debug]` section's keys (log level and pattern, flow flags, one-shot dumps and inventory
requests, debug draw, scene depth) are in [`docs/debug-config.md`](../../docs/debug-config.md).

## Saving

- Saves (`saveConfigValue(s)`) go through `common::TomlEditor`: only the values change in the file,
  its comments and layout stay. A value the file already holds is not written or logged, and a file
  that holds them all is not written.
- A value given as text is written in the type the file has for the key (`"1.250"` over a float is
  the float 1.25), and not written when it is not of that type. A one-shot array is cleared by saving
  its new TOML text, `saveConfigValue(section, key, "[]")`.
- Each save reads and writes the whole file, so a mod saves several values with one
  `saveConfigValues` (the keys of one section, or `config::ConfigEntry` values of any sections), in
  its `save()` override, rather than a `saveConfigValue` per value.
- The mod's own save is not loaded back. The watcher loads the file only when its content is not
  what the mod last loaded or saved, so a write that does not go through `ConfigBase` is taken as
  someone else's and loaded. A read that loads no values (`getConfigValue`, the DebugAdjuster's field
  mode) uses the parse of that kept content, made once after each load or save, and reads no disk.

## Loads and their threads

- A change on disk is loaded on the watcher's thread, and a session override on the caller's, one at
  a time: `_loadMutex` is held from the read of the file to the end of `loadConfigInternal()`. That is
  not the game thread, so `loadConfigInternal()` only reads values.
- To react to a load, a mod subscribes (`subscribeForConfigChangedEvent`). A load only marks that it
  ran, and `ModBase` calls the subscribers on the game thread at the start of the next frame, once
  for all the loads since the frame before: a change on disk, an override, `reload()`, and the load
  when a game session starts.
- A subscriber can name config keys (a value, or a whole section or table) and is then called only
  for a load that changed one of them, with the ones that did. Each load is compared with the one
  before, and the first changes nothing.

## Session overrides

- `setConfigOverride(s)` puts a value in place of the file's until `clearConfigOverride(s)`, and is
  never written to the file; several go with one reload. A set loads the kept content of the file and
  reads no disk, so it can run many times a second for a preview; a clear reads the file.
- `reload = false` on a set or a clear records the change with no reload and tells no subscriber: for a
  caller whose members have the values already, as a mod's own UI that hands its edits to Mods Config
  FO4 VR. That costs a few lookups, so it can run every frame.
- Every override has an owner, the first argument of each call, and an owner clears only its own.
  The devbench tool's is `devbench`, Mods Config FO4 VR's live preview is `ModsConfigFO4VR`, and a
  mod's API passes its caller's name. With two owners on one key the one set last applies, and the
  other again when it is cleared.
- A set takes each value in the type the file has for its key and keeps it as it applies, which
  `getConfigValue` and `getConfigOverrides` give back (`no` over a bool is `false`, see
  `config::appliedText`). A value not of the type, a key that is not a TOML key, or one under a value
  that is not a table is refused with the rest of its set, and the call's `std::expected` error says
  why.
- Overrides are kept by `ConfigKey::canonical()`, so two spellings of one key's path are one
  override. Only a key its owner did not override before is logged at info and is a
  `config.override` devbench event.

## Version migration

The `[Debug] iVersion` key versions the file. When the shipped config's value is higher than the
user's, `ConfigBase` writes the shipped config with the user's values for the keys it still has with
a value of the same type (`config::migrateValues`), then calls `updateConfigToLatestVersionCustom`
for what changed type or name. The old file is kept as `{name}_backup_v{N}.toml`. Bump `iVersion`
whenever the shipped file changes.

## INI import

A mod that moved its config from INI to TOML keeps its players' values. On a load with no
`{name}.toml` and a `{name}.ini` beside it, the INI is written as `{name}.toml` at version 0, and the
version migration takes it to the default's version; `updateConfigToLatestVersionCustom(0, ...)` is
where a mod moves what its TOML config changed from the INI (`config::iniToToml`, and
`config::importIniFile` for the files).

- A key the default has is of the default's type, read by SimpleIni's getter of that type, so it is
  what the mod read from the INI ("yes" a bool, "0x10" an integer). Names match with no regard to
  case, as SimpleIni matched them.
- A transform is the table of the INI's `x,y,z;heading,roll,attitude;scale`; a binding and a hand
  pose are the tables of the INI's binding line and of a pose's 22 numbers
  (`vrcf::inputBindingTomlText`, `config::handPoseTomlText`).
- An INI key the default has with another type prefix only (`sOpenButton` for `kOpenButton`) is read
  as that key. Any other value is a string.
- The INI is kept as `{name}_backup_v{N}.ini`, `N` the INI's own version, and the version 0 file as
  the update's `{name}_backup_v0.toml`.
