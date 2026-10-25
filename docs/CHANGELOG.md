# Changelog

The notable changes in each version of F4VR-CommonFramework. Only the big things are listed:
new modules and capabilities, and changes a consuming mod has to act on. Small fixes, refactors
and tooling tweaks are left to the git history.

Versions follow `set(VERSION ...)` in the root [`CMakeLists.txt`](../CMakeLists.txt). While the
major version is `0`, a minor bump may break the API; the **Upgrading** notes say what to change.

## 0.5.0 — unreleased

- **Mods Config VR client**: `ModBase` connects every mod as a client of [Mods Config VR](https://github.com/ArthurHub/F4VR-Mods-Config)'s
  native API at `kPostLoad`, under `Settings::name`, when the menu is installed, so the mod's config spec is
  matched to it. A mod adds its functions, states, choice options, quick launch entries and menu-event
  handlers to `mods_config::api::ModsConfigVRClient::get()` of the vendored client header, in C++, at any
  time, and opens the menu at its page; `Settings::modsConfigClient = false` opts out, and
  `Settings::modsConfigMinVersion` refuses a Mods Config VR whose API is older than the mod needs. **Upgrading:** a mod that registers a client of its own
  under its name opts out, or uses the framework's handle.
- **Config subscribers of keys**: `subscribeForConfigChangedEvent` takes a list of config keys, a value or a
  whole section or table, and calls its callback only for a load that changed one of them, with the ones
  that did, whatever made the load: the file, an override or `reload()`. A mod does there what more a value
  needs to take effect.
- **Mods Config VR live preview**: a `live` setting of a mod's config spec that is kept in the mod's own
  config file is previewed with no code of the mod's, as a session override under the owner `ModsConfigVR`,
  which a discard and a Save clear. A mod's own handlers are told each change and each Save too
  (`subscribeToChanges`, `subscribeToSaves`).
- **TOML config**: `ConfigBase` loads a TOML file in place of an INI, through toml++. A save changes only
  the values in the file and keeps its comments and layout (`common::TomlEditor`), and does not write a
  file that already holds them. A value given as text, by a session override, devbench `set` or a save,
  takes the type the file has for the key. A file that is not TOML, as one a player is in the middle of
  editing, is logged with its line and what is wrong at it, and is never written over: the values loaded
  before it stay, and before any were the embedded default's apply. A section and a key are TOML dotted
  keys, so a value in a nested table is saved, overridden and read by its path: `hand.x` is x in
  `[Main.hand]`. **Upgrading:**
  - Ship the default config as `<Mod>.toml` (embedded as RCDATA as before), and pass a `.toml` path.
    A player's `<Mod>.ini` moves to it by itself: on a load with no `<Mod>.toml`, the INI beside it is
    written as `<Mod>.toml` at version 0, each value read as the mod read it, by SimpleIni's getter of
    the type the default has for the key, and is then updated as any older version. Ship the TOML at
    `iVersion = 1` or above, and move what it changed from the INI (a renamed key, a string that is now
    an array) in `updateConfigToLatestVersionCustom` from version 0. The INI is kept as
    `<Mod>_backup_v<N>.ini`, and the version 0 file as `<Mod>_backup_v0.toml`.
  - `loadIniConfigInternal(const CSimpleIniA&)` is `loadConfigInternal(const toml::table&)`: read a value
    as `config[section][key].value_or(default)`, and pass the table to the readers
    (`getTransformValue(config, ...)`, `getInputBindingValue`, `loadWandActivationConfig`, ...).
  - `saveIniConfigInternal` is gone: override `save()` and write the values with one `saveConfigValues`.
    Each save reads and writes the whole file, so save values together: `saveConfigValues` takes the keys
    of one section, or `config::ConfigEntry` values of any sections, in place or as a list made at runtime.
  - `_iniFilePath` and `stopIniConfigFileWatch` are private: a mod that moves to another config file at
    runtime calls `switchConfigFile(path)`, which stops the watch, loads the new file and watches it.
  - Renamed: `saveIniConfigValue(s)` → `saveConfigValue(s)`, `getIniFilePath` → `getConfigFilePath`,
    `loadIniConfig` → `loadConfig`, `subscribeForIniChangedEvent` / `unsubscribeFromIniChangedEvent` →
    `subscribeForConfigChangedEvent` / `unsubscribeFromConfigChangedEvent`, `readIni*Value` → `read*Value`,
    `applyIniConfigWithOverride` → `applyConfigWithOverride`, `INI_SECTION_DEBUG` → `CONFIG_SECTION_DEBUG`,
    `config::IniValue` / `IniKey` / `IniOverride` → `ConfigValue` / `ConfigKey` / `ConfigEntry`, and
    `config::IniReaders` → `ConfigReaders`.
- **`[Debug]` lists are TOML arrays**: `aDumpDataOnce = ["skelly", "perf"]` in place of
  `sDumpDataOnceNames`, and `aDebugDrawDisabledChannels` in place of `sDebugDrawDisabledChannels`.
  `aAddItemsOnce` in place of `sAddItemsOnceNames` is an array of requests, each a table
  (`{ op = "print", category = ["weapons", "armor"], keyword = "laser" }`), so one edit can run
  several operations. A request that is not valid is logged with the key that is wrong, and then
  none of them runs. `ConfigReaders::getStringListValue` reads an array of strings. **Upgrading:**
  - Rename the three keys in the mod's TOML, as the template has them, and bump its `iVersion`:
    the old values are not moved.
  - `consumeDebugAddItemsOnce` returns a `toml::array`, and
    `f4vr::DebugInventory::iterateObjects(std::string_view)` is
    `iterateObjects(const toml::array&)`, with `parseRequest` for one request.
- **Transforms are TOML tables**: a `t` value is `{ pos = [x, y, z], rot = [heading, roll, attitude],
  scale = s }` (rotation in degrees), each key optional, in place of the text
  `"x,y,z;heading,roll,attitude;scale"`, which is no longer read. A misspelled key or an array that is
  not three numbers is logged and the default applies. `DebugAdjuster` saves the table. The INI import
  writes a player's transform text as the table, for a key the shipped TOML has as a transform, so it is
  carried as any value. **Upgrading:** write the transforms of the mod's TOML as tables, as the template
  has them.
- **Bindings are TOML tables**: a controller binding is a `k` value, `{ hand = "offhand", type = "tap",
  button = "grip" }`, with `duration`, `suppress` and `modifier = { button, hand }` optional, and
  `{ hand, type = "axis", direction, axis, threshold }` for a thumbstick or an axis, in place of the line
  `"offhand tap grip"`, which only the INI import reads now. `{ type = "none" }` disables one. Every key and
  word is checked, and the warning names the one that is wrong. `vrcf::inputBindingTomlText` writes a
  binding as its table, for a UI that rebinds. The INI import converts a player's binding line into the
  table, and matches an INI key to a TOML key of another type prefix (`sOpenButton` to `kOpenButton`), so
  a binding renamed with its prefix keeps the player's value. A table saved or migrated keeps the order its
  keys are written in. **Upgrading:**
  - Rename the mod's binding keys from `s` to `k` and write them as tables, as the template has them.
    An activation sphere's are `kPrimaryBinding` / `kSecondaryBinding`, and `[Debug]` has
    `kDebugDrawToggleBinding` (not moved from `sDebugDrawToggleBinding`).
  - `DebugSection::drawToggleBinding` is a `vrcf::InputBinding`.
- **A caller of the time values were text still sets them**: a session override, a save and devbench `set`
  of a binding take its line (`"offhand tap grip"`, `"none"`) and write it as its table, and an override,
  its clear and `getConfigValue` of a key the file does not have reach the one key beside it with another
  type prefix (`sOpenButton` for `kOpenButton`, `config::keyWithOtherPrefix`), so a mod that overrides
  another's binding by its old key and text keeps working.
- **Hand poses are TOML tables**: an `h` value is `{ thumb = [prox, mid, dist, splay], index = [..],
  middle = [..], ring = [..], pinky = [..], palm = [pitch, yaw] }`, a finger or the palm left out being
  zeros, in place of the 22 numbers of `"p,m,d,s;...;pitch,yaw"`, which only the INI import reads now.
  `DebugAdjuster` saves the table. The INI import converts a player's pose into the table, for a key the
  shipped TOML has as a hand pose. **Upgrading:** write the hand poses of the mod's TOML as tables.
- **Colors are TOML tables**: a color is a `c` value, `{ rgb = [r, g, b], alpha = a }`, each a whole
  number in 0..255, read by `getColorValue` in place of the text `"r,g,b,a"`. A key left out keeps the
  default's channels, so `{ alpha = 60 }` changes only the opacity. An activation sphere's look is
  `cSphereColor`, `cIconColor` and `aSphereFalloff = [center, rim]` in place of `sSphereColor`,
  `sIconColor` and `sSphereFalloff`. **Upgrading:** rename those keys in the mod's TOML and write their
  values in the new form: a player's value of an old key is not moved.
- **A binding that is not valid says why**: the warning names the word that is wrong and the words that
  fit there (`'thumbstik' is not a button, one of trigger, grip, ...`). Nothing on a line is skipped any
  more: a duration or threshold that is not a number, a word past the end of the line, and a second
  modifier or suppress make it invalid, where they were dropped, and the mod's default applies.
  **Upgrading:** `vrcf::parseInputBinding` returns `std::expected<InputBinding, std::string>`; code that
  stored it in a `std::optional` takes the value when there is one.
  - `updateIniConfigToLatestVersionCustom` is `updateConfigToLatestVersionCustom(currentVersion,
    latestVersion, const toml::table& old, common::TomlEditor& latest)`. The generic migration carries a
    value only into a key of the same type (an integer into a float too): one whose type changed is left
    to it.
  - `setConfigOverride(s)` return `std::expected<void, std::string>`: a value that is not of the type
    the file has for the key is refused, with the rest of its set, and the error says why. An override is read back as it applies
    (`getConfigValue`, `getConfigOverrides`, devbench `config`): `no` over a bool is `false`.
  - `ConfigBase.h` no longer includes SimpleIni, and with it Windows' `min` / `max` macros: write
    `(std::min)(a, b)`.
- **A binding prompt has an icon for every binding**: the binding icons are drawn again in one style, by
  `tools/gen_binding_icons.py`: the button as the controller prints it, an L or R circle over its top left
  for the controller, and a solid mark over its bottom left for how it is worked (a clock for a long press,
  an arrow down onto a bar for a hold, 2x for a double press, an arrow up off a bar for a release, ripples
  for a touch). A touch, a release and a double press have their icons, the thumbstick is held and long
  pressed, a thumbstick push has its direction's arrow, a pull of the trigger or the grip axis is that
  button, and a chord is its two icons with " + " between them (`vrui::bindingIconPaths`). Every icon is
  80 px tall with the button in its middle, so a prompt draws every button at one scale. Only the system button falls back to words. **Upgrading:** re-copy
  `Textures\MyMod\f4cf\bindings\` from `mod-template/`: the icons a mod has are replaced, and the new ones
  are what `bindingIconPath` now names (`left-thumbstick-up.dds` for a push, where it was
  `left-thumbstick.dds`).
- **TOML editing**: `common::TomlEditor` sets values in the text of a TOML file and keeps every other
  byte (comments, blank lines, key order, how the other values are written), which toml++'s own writer
  doesn't. The framework depends on toml++, compiled once into it: **Upgrading:** add `"tomlplusplus"`
  to your mod's `vcpkg.json`.
- **vrui rotation**: an element takes a rotation relative to its parent (`UIElement::setRotation`),
  and everything under it turns with it. On a root it is relative to the attach node, so a UI no
  longer has to face the way its node faces. The dev layout tunes it live as `Rot`.
- **vrui on the offhand**: `UIManager::attachPresetToOffhandWandTop` and
  `attachPresetToOffhandWandRight` attach a UI to the offhand controller, as the primary wand's top
  and left presets do for the primary hand. `attachPresetToOffhandWrist` lays a UI on the inner
  wrist and keeps it on the forearm as the arm moves, when an arm is drawn at the controller (FRIK).
- **vrui in the world**: `UIManager::attachPresetToWorldAtHMD` puts a UI where the HMD is and leaves
  it there, so it stays in the world while the player walks and turns. `recenterWorldElement` puts
  it where the HMD is now, back in front of the player. Given a distance, the preset does that by
  itself when the player walks further than that from the UI.
- **vrui finds the finger**: the fingertip that presses a button is read from the player's skeleton,
  and either hand can press: a UI on a hand is pressed by the other hand, any other UI by the nearer
  finger, and the hand that presses points and gets the haptic. Without a hand drawn at
  the controller, the controller presses the button and a small gold sphere marks where.
  `[Debug] bVRUIShowFingerTip` shows the sphere on the fingertip too. `f4vr::Skelly` maps the
  skeleton's bones by itself, so its fingertip and bone reads no longer need `initBoneTreeMap`
  called first, and its fingertip is 2.0 units past the last finger bone, up from 1.8.
- **vrui points the hand**: the hand whose finger is near a button is pointed through FRIK's API by
  the framework, under the tag `<mod name>_UI`, so a mod with a vrui UI no longer writes an adapter
  for it. With FRIK's second API table (FRIK 0.79) the pose is set a little above the default
  priority, to win over a pose the mod itself holds on that hand.
- **vrui presses from the front**: a finger interacts with a button only when it comes to it from
  in front of the button's face while the player looks at the button. A hand behind a UI, or near
  a UI the player does not look at, no longer points and cannot push a button, and a hand that
  comes out from behind a UI points only once its finger turns back toward a button. A hand that
  points keeps pointing while its finger is near a button, so it holds the pose between presses.
  A button is pushed only by a finger that crossed its face from the front, so a finger coming up
  from behind a button no longer moves it.
- **vrui updates itself**: `ModBase` runs the UI manager's frame update after the mod's
  `onFrameUpdate()`, while the player is loaded, so a mod no longer calls it. A press handler then
  runs after the mod's frame. A mod that needs the UI updated at a certain point of its frame calls
  `g_uiManager->onFrameUpdate()` there, and the framework skips its own call in that frame.
- **vrui dev layout by a flag**: the dev layout is turned on with `[Debug] bVRUIDevLayout`, with
  nothing for the mod to call, and keeps its lines in `<mod>_DevLayout.ini` beside the mod's INI, a
  section for each attached root. The lines of a detached root stay, so what was tuned on a screen
  is still there after going to another screen and back.
- **imgui pointer**: an ImGui canvas can be operated. `Canvas::setInteractive` and
  `UIImGuiPanel::setInteractive` make a wand's ray the pointer of the canvas and its trigger the
  mouse button, so buttons, checkboxes, sliders and lists work as ImGui does them. One hand owns the
  pointer at a time: the one that pressed last, and the primary hand before either has.
  `imgui::pointer()` is that pointer: `state()` says what it does, and `setHands` limits pointing to
  one hand. While the pointer is on a canvas its ray is drawn, up to a set length and with a fade at
  both ends, and a mark with a border on the canvas, over the panels and not hidden by the world.
  The ray of the other hand is drawn fainter, and with no mark, while it is on a canvas too.
  `setStyle` takes a `PointerStyle`: where the ray is on the hand and the whole look, or the look
  turned off for a mod that draws its own.
- **imgui pointer hides the wand**: while a hand's ray is on an interactive canvas, its whole wand is
  hidden from the game and from other mods, so a pull of the trigger on a canvas fires no weapon. A
  press that began on a canvas stays on that canvas and stays hidden from the game until the trigger
  is released, also when the ray slides off the canvas or the canvas is hidden. A trigger that is
  already down when the ray comes to a canvas stays the game's until it is released.
- **imgui pointer scrolls**: the thumbstick of the hand that owns the pointer is ImGui's mouse wheel,
  so a list or a child window that is longer than its room scrolls while the ray is on it, and the
  player does not move. `PointerStyle::scrollSpeed` is the speed at a full push, in lines of text a
  second.
- **imgui content has ImGui's padding**: the zero padding and border of the canvas window no longer
  reach what the content opens. A combo's list, a popup, a dialog and a bordered child window have
  ImGui's own window padding and border, where they had none, so the content of a bordered child
  moves in by that padding.
- **imgui dedicated panel**: `UIImGuiPanel::setDedicated` and `Canvas::setDedicated` are for the one
  large panel that is a mod's whole UI. It has a texture of its own size in place of a part of the
  shared atlas, so it keeps 48 pixels per vrui unit up to 4096 pixels a side, where a panel in the
  atlas is scaled down past 1024. While it is shown it is ImGui's display, so a combo's list, a
  popup and a dialog that its content opens stay inside the panel, and a dialog is centered on it.
  ImGui has one display, so while a dedicated panel is shown it is the only canvas the mod draws.
  The atlas texture is now created when a canvas is first drawn into it, so a mod with only a
  dedicated panel does not have it.
- **imgui curved panel**: `UIImGuiPanel::setCurveRadius` bends a panel toward the player, around a
  cylinder of that radius whose axis runs along the panel's height in front of its middle
  (`CanvasPlacement::curveRadius` for a canvas placed by hand). With the radius as the distance the
  panel stands at, its sides are as far from the player as its middle and face them, so a wide panel
  reads the same all over. The pointer meets the panel where it is drawn, and the dev layout tunes
  the radius live as `Curve`.
- **vrui in the world moved by the player**: `UIManager::setWorldMoveButton` lets the player move a
  UI that stays in the world. They point at it and hold that button on the hand that points, and the
  UI moves and turns with the hand, kept level, until the button is released. It follows the hand
  smoothly, which steadies the hand's shake: `setWorldMoveSmoothing` is how smoothly. It stays there
  until `recenterWorldElement` or the recenter distance puts it in front of the player again. A UI
  is moved by a ray on it, which an interactive `UIImGuiPanel` tells vrui of by itself. With
  `setWorldMoveHoldDelay` the button takes hold only when it is held that long, so a short press of
  it is the mod's to use.
- **config color**: `ConfigBase::getColorValue` reads a color from the INI, written as `r,g,b` or
  `r,g,b,a` in 0 to 255, as `getTransformValue` reads a transform.
- **config loads one at a time**: a change of the INI on disk is loaded on the file watcher's
  thread and a session override on its caller's, and the two could run at the same time. A load now
  holds a lock from the read of the file to the last value set, so `loadIniConfigInternal` never
  runs twice at once, and the load that read the file last is the one whose values stay.
- **config tells its own save by the file's content**: the file watcher loads the INI only when it
  holds something else than the mod last loaded or saved. Before, the mod's save set a flag for the
  next event of the file, which could stay set after two saves close together, and the next change
  by someone else was then not loaded. A second write within 200 ms of another was dropped too, and
  is now loaded. A write that leaves the file as it was loads nothing.
- **config overrides at preview rates**: `ConfigBase::setConfigOverrides` and `clearConfigOverrides`
  set and clear several session overrides with one reload. A set no longer reads the INI from disk:
  it loads the file as the mod last loaded or saved it, so a value can be set many times a second
  while a setting is previewed. Only a key that becomes overridden is logged at the info level and
  is a `config.override` devbench event. A new value for a key that is already overridden is logged
  at the debug level, with no event. A clear reads the file, as before.
- **config subscribers on the game thread**: the callbacks of
  `ConfigBase::subscribeForIniChangedEvent` are called by `ModBase` at the start of the next frame,
  on the game thread and before the mod's `onFrameUpdate()`, so they can call into the engine. They
  are called once for all the loads of the values since the frame before, and for every kind of
  load: a change of the INI on disk, a session override set or cleared, `reload()`, and the load
  when a game session starts. Before, they were called on the file watcher's thread, and only for
  a change on disk.
- **config overrides have an owner**: a session override is set and cleared under the name of its
  owner, and an owner clears only its own. The devbench tool, another mod through the mod's API and
  the mod itself then override the same config and none removes another's. When two owners override
  the same key, the one that was set last applies, and when that one is cleared the other applies
  again. The devbench tool's owner is `devbench`: its `clear` drops only what its `set` made, and
  `overrides` says who set each.
- **render gradients**: a fill takes a color for each corner and blends between them
  (`PrimitiveDraw::addTriangle` and `addQuad` with a color per corner), so a gradient or a fade is
  one shape. Every vertex carries its color, so a change of color no longer starts a new draw call:
  the lines of a layer go out in one draw, its screen text in one, and its fills and world text in
  one for each texture.
- **render texture from pixels**: `render::Texture::fromPixels` makes a texture from RGBA pixels in
  memory, for an image that is no file the engine can load. It is drawn as a loaded texture is, by
  `PrimitiveDraw::addImage` and by `UIImagePanel::setImage`.
- **imgui images**: the content of a canvas draws a `render::Texture` with `imgui::image`, an item at
  the cursor as `ImGui::Image` is, and `imgui::addImage`, a rectangle of a draw list. The texture can
  be dropped while its panel is shown: the frame that draws it keeps it until it has been drawn.
- **imgui bold and italic**: the canvas font has a bold and an italic face beside its regular one,
  for a run of text in the content: `imgui::canvasFont(FontStyle)`. They are the embedded Roboto
  Black and Roboto Medium Italic, or a mod's own `<ModName>-Bold.ttf` and `<ModName>-Italic.ttf`
  beside its `<ModName>.ttf`.
- **imgui on Dear ImGui 1.92.9**: text of every size is sharp. ImGui rasterizes each size the first
  time it is drawn, at the supersample factor, where it used to resample one raster of the default
  size. Content pushes a size with `ImGui::PushFont(nullptr, size)`. The font texture ImGui keeps
  growing is created on the game thread and written by the render thread before each frame it draws.
- **imgui takes the keys of the game's window**: a focused `ImGui::InputText` in a canvas is typed
  with a physical keyboard, or with a keyboard overlay that sends keys to the game's window. The
  window's keyboard messages are handed to Dear ImGui's Win32 backend, and go on to the game as
  before.
- **imgui shows SteamVR's keyboard while a text field is edited**: what is typed on it is sent to the
  field as ImGui's keys and characters, Done leaves the field with its text, and the close button gives
  it back the text it had. The content calls `ImGui::InputText` as on a screen. While the keyboard is
  shown the pointer presses nothing. A runtime with no keyboard (OpenComposite, xrizer) is logged once,
  and its fields are typed with the keys of the game's window.
- **vcpkg baseline of 2026-09-09**: the framework and the mod template pin vcpkg `5cd4931b`, with
  fmt 12, spdlog 1.17, nlohmann-json 3.12, xbyak 7.28, cpptrace 1.0.4 and Dear ImGui 1.92.9, and
  pin no version in `overrides`. A mod still on the old `b4a3d89` baseline keeps building with its
  `overrides` of `cpptrace` 1.0.0 and `imgui` 1.92.9, and moves to the new one when it chooses.

**Upgrading**

- A mod that moves its `builtin-baseline` to `5cd4931b` may need two changes in its own code. fmt 12
  refuses `%z` and `%Z` for a `std::tm` at compile time (MSVC's has no `tm_gmtoff`): format a local
  time with its offset through `common::toDateTimeString`. nlohmann-json 3.12 converts a `json` to
  `std::optional<json>` by itself, so returning a `json` where an optional is expected is ambiguous:
  construct it, `std::optional<json>(value)`.
- The ImGui layer needs Dear ImGui 1.92.9: move the mod's `builtin-baseline` to `5cd4931b`, as the
  template has it, or on the old `b4a3d89` add `{ "name": "imgui", "version": "1.92.9" }` to its
  `overrides`.
- The ImGui layer needs Dear ImGui's Win32 backend: add `win32-binding` to the features of `imgui`
  in the mod's `vcpkg.json`, beside `dx11-binding`, as the template has it.
- ImGui's own API changed between 1.90 and 1.92 (its
  [changelog](https://github.com/ocornut/imgui/blob/master/docs/CHANGELOG.txt) has each change). For
  content code the usual ones: `ImGui::SetWindowFontScale(f)` becomes
  `ImGui::PushFont(nullptr, size)` and `PopFont()`, `PushFont(font)` takes a size (`0` keeps the
  current one), and a font's `FontSize`, `Ascent`, `Descent` and `FindGlyph` are on
  `font->GetFontBaked(size)`. `imgui::internal::loadCanvasFont` takes no raster scale.
- The embedded Roboto is Google Fonts' build, 2.137. Its line is shorter, and ImGui sizes a font by
  its line, so the text of an ImGui canvas is drawn 12.5% larger at the same `setFontSizePixels`:
  21.3 is now as large as 24 was. The text of vrui keeps its size, and kerns more pairs of letters.
- `vrui::UIModAdapter` is removed: delete the mod's adapter and its
  `g_uiManager->onFrameUpdate(&adapter)` call, which the framework now makes. A mod that keeps the
  call, to have the UI updated at a certain point of its frame, passes no argument.
  `vrui/UIModAdapter.h` is now `vrui/UIFrameUpdateContext.h`. A mod that poses the hands itself
  gives `g_uiManager->setHandPointingHandler(...)` a function that points a hand and releases it,
  in place of the adapter.
- A mod's own pressable element finds its finger with `UIElement::updateFinger`, which also reports
  it for the hand to point, in place of `getInteractionFingerTip` and
  `UIFrameUpdateContext::markAnyPressableCloseToInteraction`.
- `UIUtils::triggerInteractionHeptic` takes the hand to buzz: pass `true` for the primary hand.
- `vrui::UIDebugWidget` is removed. To mark a point in the world use the debug draw overlay
  (`debug::dd().sphere(...)`).
- `UIManager::enableDevLayoutViaConfig` is removed: delete the call, and add `bVRUIDevLayout = true`
  to the INI's `[Debug]` section while tuning. `ConfigBase::debugVRUIProperties` is removed with it,
  and a `[VRUI_DevLayout]` section left in an INI is no longer read.
- `render::FillTriangle` has a color for each corner, `colorA`, `colorB` and `colorC`, in place of
  `color`. `addTriangle` and `addQuad` with one color are unchanged.
- A `subscribeForIniChangedEvent` callback now runs on the game thread, at the start of the next
  frame, and also after a session override and `reload()`. Work it handed to the game thread can be
  done in it. A mod that sets a flag of its own in `loadIniConfigInternal`, to apply a reload in
  `onFrameUpdate()`, can subscribe instead. A mod that does not use `ModBase`'s frame update calls
  `notifySubscribersOfReload()` on its config in its own.
- `ConfigBase.h` is now `config/ConfigBase.h`: change the `#include` in the mod's `Config.h`. The
  class is as it was, in the `f4cf` namespace.
- `ConfigBase::setConfigOverride`, `clearConfigOverride` and `clearAllConfigOverrides` take the
  owner's name as their first argument. A mod's API that sets overrides for its callers passes the
  caller's name. `clearConfigOverride` returns whether the owner had the override, and
  `getConfigOverrides` returns a list of `config::AppliedOverride`, each with its owner, in place
  of a map.

- `f4vr::PlayerNodes` and `getPlayerNodes()`, deprecated in 0.4.0, are removed. Use
  `getVRPlayerNodes()`, which returns CommonLibF4's `RE::VRPlayerNodes`: the same table with
  camelCase member names, some of them corrected (e.g. `HmdNode` → `hmdNode`,
  `SecondaryWandNode` → `secondaryWandNode`, `unk750` → `equippedWeaponNode`).

## 0.4.1 — 2026-10-01

- **`f4cf::devbench`**: every mod gets its own [devbench](https://github.com/ArthurHub/devbench)
  tool, so an AI agent or a script can drive it in the running game over MCP or REST. `ModBase`
  registers it, named after the mod, with generic actions for health, the mod's state as of its
  last frame, session config overrides and every perf site as a tree per thread, read without
  the game thread; a mod adds its own
  actions with `devbench::addAction`, its state with `devbench::setStateProvider`, and publishes
  events with `devbench::emit`; the framework publishes its own for session loads, config reloads
  and overrides, and input suppression changes. Opt out with `Settings::devbenchTool = false`.
- **`f4cf::perf`**: `PerfMonitor` is replaced by sites. `F4CF_PERF_FUNCTION()` and
  `F4CF_PERF_SCOPE("label")` time a function or a block on any thread into a lock-free histogram
  (p50/p95/p99 within ~3%, no sample cap), and each site records the site it ran inside, so they
  nest into a tree with self time. Every read comes with the frame context of the same window:
  the frame interval, the budget at the headset's refresh rate, and the VR compositor's GPU time,
  late starts and reprojected frames. The framework times its own per-frame work, every
  controller-state poll of its own and overlay drawing in the
  Submit hook. Recording is off until `perf::setEnabled(true)`, which the
  devbench tool does on first use; the log level no longer turns it on. Without devbench, the
  `sDumpDataOnceNames` names `perf_reset` and `perf` start a window and log it as a table.
- **GPU time**: GPU sites and `perf::GpuTimer` time the GPU work a mod issues itself, from
  timestamps read back 1-3 frames later without waiting. The Submit host times every overlay layer
  this way, the shared setup and each draw callback by name, shown as a `gpu` tree in the perf
  report and as plots in a Tracy build. The host also names its drawing for frame capture tools
  (RenderDoc, PIX) while one is attached.
- **Tracy**: a `Tracy` build configuration next to Debug and Release, a Release build with the
  [Tracy](https://github.com/wolfpld/tracy) profiler client v0.14.1 in it. Every perf site is also
  a Tracy zone, each frame is marked, the frame interval and the compositor's timing of each frame
  are plots, and devbench events are messages, so the Tracy viewer shows the timeline, frame by
  frame and thread by thread. The client is on demand and listens on localhost only; the other
  configurations never compile it in. The mod template builds it with its `tracy` build preset.

**Upgrading**

- `common::PerfMonitor` is gone: replace `static PerfMonitor perf("Name");` and
  `const auto timer = perf.scope();` with `F4CF_PERF_FUNCTION();` (or `F4CF_PERF_SCOPE("label")`
  for a block) from `perf/Perf.h`. Debug logging no longer turns timing on and nothing is logged
  any more; read the sites through the mod's devbench tool (`perf` action).
- The `Tracy` configuration gets only what your `CMakeLists.txt` gives it: change the Release-only
  `$<CONFIG:RELEASE>` settings to `$<CONFIG:Release,Tracy>`, and repeat the `/Ob2` to `/Ob3` fix for
  `CMAKE_CXX_FLAGS_TRACY`, as the mod template does. Add a `tracy` build preset
  (`"configuration": "Tracy"`) to build it. Configuring now downloads Tracy's source once;
  `-DF4CF_WITH_TRACY=OFF` drops the configuration and the download.

## 0.4.0 — 2026-09-25

**Rendering in VR, built into the framework.**

- **`f4cf::render`**: one shared OpenVR `Submit` hook that every framework overlay draws through,
  with lines, filled triangles, game-texture images and text in an embedded Roboto
  distance-field font. It can capture the engine's scene depth so overlays are hidden behind world
  geometry instead of always drawing on top ([design](tech/scene-depth-occlusion.md)).
- **`f4cf::debug`**: an in-world debug-draw overlay (shapes, labels, and a watch table in front of
  the HMD) for seeing what a mod is doing live ([design](tech/debug-draw-overlay.md)).
- **vrui panels**: UI elements drawn by the primitive renderer instead of one NIF per button:
  `UITextPanel`, `UIImagePanel`, `UIButtonPanel`, `UIToggleButtonPanel` and
  `UIMultiStateToggleButtonPanel`. They size themselves to their content, wrap text, share one
  panel style, and can show controller prompts generated from the actual input binding.
- **`f4cf::imgui`**: Dear ImGui windows drawn as world-space panels that fit into vrui layouts
  (`UIImGuiPanel`). Controlled by the `F4CF_WITH_IMGUI_UI` build option (on by default).
- **Activation spheres**: `WandActivationSphere` is now driven by a `WandActivationConfig` loaded
  from one INI section: a zone with an optional power-armor variant, two bindings, suppression you
  opt into per binding, configurable haptics, and a visual shown never / always / when inside /
  when available. The visual is a single mesh styled at runtime (preset + per-value overrides),
  and a zone can also be marked by an icon.
- **Player turning**: `f4vr::PlayerRotation` snap/smooth-turns the player the way the game's VR
  comfort settings say, even where vanilla turning is taken away ([design](tech/vr-player-rotation.md)).
- **Game-state helpers**: actor range and projectile queries, Havok collision layers, actor
  perception / combat / light-level access, live light refresh entry points, and
  `getVRPlayerNodes()` typed by CommonLibF4.
- **Input**: `VRControllers.setControllerStateAdjuster()` lets a mod correct the polled controller
  state when another mod remaps it for every reader (e.g. ROCK's trigger remap).

**Upgrading**

- Assets the framework ships now live in an `f4cf` folder inside the mod's own folders (sphere
  visuals, binding prompt icons, shared vrui icons). Re-copy them from `mod-template/`.
- The per-look sphere NIF variants are replaced by one mesh plus an `sSphereStyle` preset.
- A mod using `f4cf::imgui` must declare `imgui` (feature `dx11-binding`) in its own `vcpkg.json`.
  The mod template already does.
- **Deprecated, removed in 0.5.0:** `f4vr::PlayerNodes` and `getPlayerNodes()`. Use
  `getVRPlayerNodes()`, which returns CommonLibF4's `RE::VRPlayerNodes`. That struct is the same
  table, but its member names are camelCase and some are corrected (e.g. `HmdNode` → `hmdNode`,
  `SecondaryWandNode` → `secondaryWandNode`, `unk750` → `equippedWeaponNode`).

## 0.3.0 — 2026-07-02

**Configurable input, haptics, and in-game tuning.**

- **License**: relicensed from MIT to **GPL-3.0-or-later**, and `mod-template/` along with it.
- **CommonLibF4VR types throughout**: the framework no longer uses F4SEVR's `Forms.h`. All
  game types are CommonLibF4VR `RE::` types.
- **Input bindings**: `InputBinding` + `VRControllersManager::check()` evaluate a binding
  described as data (hand, tap / press / long-press / double-press, button or axis, cross-hand
  modifier), parsed from a readable INI string ([guide](input-binding.md)).
- **Input suppression**: `VRControllersSuppressor` hides chosen buttons and axes from the game
  and other mods while a gesture owns them.
- **Haptics**: `VRControllersHaptic` with a library of named haptic patterns.
- **Interaction and state helpers**: `WandActivationSphere` (a proximity zone for gestures) and
  `EquippedWeaponHandler` (tracks equipped-weapon changes).
- **Config**: `NiTransform` and hand-pose INI values, INI change subscriptions, and
  session-only value overrides.
- **Debugging**: `DebugAdjuster` for tuning transforms, hand poses and INI values in-game with the
  controllers, `DebugInventory` for adding items in bulk, `PerfMonitor` for profiling hot paths,
  and a guide to the `[Debug]` settings ([guide](debug-config.md)).
- **vrui**: elements sized from their real texture dimensions, a disabled state, and atlas
  pack/unpack tools in `nif-tools`.
- **Main loop**: early and late frame-update hooks, so a mod that moves the body (FRIK) can run
  before the others.
- **Build**: moved to Visual Studio 2026, with CI build and maintenance workflows.

## 0.2.0 — 2025-11-27

**A reusable library with a mod template.**

- Built on **CommonLibF4VR** instead of CommonLibF4.
- A CMake target that a mod adds with `add_subdirectory(...)`, rather than a DLL.
- **`ModBase`**: the shared plugin lifecycle (`onModLoaded` / `onGameLoaded` /
  `onGameSessionLoaded` / `onFrameUpdate`) on a common main-loop hook, with exception handling.
- **`mod-template/`**: a starting point for a new mod.
- **vrui**: a multi-state toggle button, and layout values read from config so a UI can be tuned
  without rebuilding.

**Upgrading**

- Everything moved under the `f4cf` namespace (`f4cf::f4vr`, `f4cf::vrcf`, `f4cf::vrui`), and
  the controller and UI code moved to `vrcf/` and `vrui/`.

## 0.1.0 — 2025-09-09

- First version: the common code built up across FRIK and other mods (`common`, `f4vr`,
  `f4sevr`, `ui`), collected into one repository.
