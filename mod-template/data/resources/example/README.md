# Example UI textures

Sample sprites showing the kinds of elements you can build for a VR UI: buttons
(`btn-example-*`), message panels (`msg-example-*`), and titles (`title-example-*`). They
double as a demo of mixed sprite sizes for the packer. Use them as a starting point —
replace them with your own art, or delete this folder, for a real mod.

## Pack command

Bin-packs every PNG in this folder into a single `ui-example.DDS` atlas plus one
`<sprite>.nif` per image, written as a deployable game-folder tree
(`Textures\MyMod\ui-example.DDS` + `Meshes\MyMod\ui-example\<sprite>.nif`):

```
python nif-tools\vrui_atlas.py pack --name ui-example --texture-subpath MyMod mod-template\data\resources\example --output mod-template\data\mod
```

Each PNG's file name becomes its `.nif` name — the same name you pass to `UIButton` /
`UIWidget` in code. `--texture-subpath MyMod` is both the texture path baked into every nif
(`Textures\MyMod\ui-example.DDS`) and the subfolder the atlas/nifs are written under; set it
to your mod's name, and use `--output` to write straight into your mod's data folder. Full
options and the reverse (`unpack`) are in [nif-tools/README.md](../../../../../nif-tools/README.md).
