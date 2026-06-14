# Common UI textures

The shared button sprites for this mod's VR UI — one PNG per button. They cover the
standard actions (back, exit, save, reset), the button frames the framework's widgets
reuse (`btn-empty` for plain buttons, `btn-border` / `btn-border-2` for toggles), and the
config-menu buttons (advanced/misc config, misc options). Re-skin any of them by editing
the PNG and re-running the pack command below.

## Pack command

Bin-packs every PNG in this folder into a single `ui-common.DDS` atlas plus one
ready-to-use `<button>.nif` per sprite, written as a deployable game-folder tree
(`Textures\MyMod\ui-common.DDS` + `Meshes\MyMod\ui-common\<button>.nif`):

```
python nif-tools\vrui_atlas.py pack --name ui-common --texture-subpath MyMod mod-template\data\resources\common --output mod-template\data\mod
```

or from your mod repo, writing straight into your mod's data folder (replace MyMod subpath!):

```
python external\F4VR-CommonFramework\nif-tools\vrui_atlas.py pack --name ui-common --texture-subpath MyMod external\F4VR-CommonFramework\mod-template\data\resources\common --output data\mod
```

`--texture-subpath MyMod` is both the in-game texture path baked into every nif
(`Textures\MyMod\ui-common.DDS`) and the subfolder the files are written under — the atlas
to `Textures\MyMod\`, the nifs to `Meshes\MyMod\ui-common\`. Change `MyMod` to your mod's name and
point `--output` at your mod's data folder so the `Textures\` and `Meshes\` trees drop
straight in. Full options and the reverse (`unpack`) are in
[nif-tools/README.md](../../../../../nif-tools/README.md).
