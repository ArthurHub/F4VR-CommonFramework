# Common UI textures

The shared button sprites for this mod's VR UI — one PNG per button. They cover the
standard actions (back, exit, save, reset), the button frames the framework's widgets
reuse (`btn-empty` for plain buttons, `btn-border` / `btn-border-2` for toggles), and the
config-menu buttons (advanced/misc config, misc options). Re-skin any of them by editing
the PNG and re-running the pack command below.

`debug-sphere` and `activation-sphere` are not buttons but **custom meshes** (the spheres the
activation-sphere visuals clone). `pack` pairs meshes and textures by name — exact, or up to a
`@` suffix — and reuses the mesh instead of a flat quad, keeping its geometry, repointing its
texture at the atlas, and remapping its UVs into the paired texture's region. The two folders
show both directions:

- **One mesh, many textures** — `debug-sphere.nif` reused by `debug-sphere.png` and
  `debug-sphere@strong.png`, so those two PNGs are two skins of the one sphere.
- **One texture, many meshes** — the `activation-sphere@<color>-<strength>.nif` meshes all
  share `activation-sphere.png`; each mesh is kept and emits its own output nif.

(`@` rather than `#` since `#` is an INI comment.) See the packer's
[custom mesh override](../../../../nif-tools/README.md#custom-mesh-override).

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
[nif-tools/README.md](../../../../nif-tools/README.md).
