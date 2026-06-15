# nif-tools

Python utilities for working with Fallout 4 VR NIF meshes used by the
[F4VR Common Framework](../README.md) VR UI system.

| Tool | Purpose |
|------|---------|
| [`vrui_atlas.py`](vrui_atlas.py) | **Pack** discrete button/label images into one atlas DDS + ready-to-use button NIFs, and **unpack** an atlas back into per-button images. |
| [`nif_to_json.py`](nif_to_json.py) | Dump the full structure of any Bethesda NIF to JSON for analysis (uses PyNifly). |

---

## `vrui_atlas.py` — VR UI atlas packer/unpacker

A VRUI button renders from two files:

- a **`.nif` mesh** — a single quad with a `BSEffectShaderProperty` pointing at a shared
  atlas texture, a UV rectangle selecting this button's region, and a root node named
  `VRUI (W:<w> H:<h>)` carrying the element's size in framework units. The framework reads
  that size from the name at runtime (see [`src/vrui/UIUtils.cpp`](../src/vrui/UIUtils.cpp)).
- a **`.DDS` atlas** — one texture holding every button/label image.

Authoring those by hand (one combined DDS, one hand-tuned NIF per region) is tedious.
This tool does both directions.

### Requirements

```
pip install Pillow
```

That's the only dependency — the NIF read/write is pure Python and does **not** need the
PyNifly runtime that `nif_to_json.py` downloads.

### Pack: images → atlas + NIFs

Put each button/label in a folder as its own image (**PNG recommended** — see *Quality*
below), then:

```
python vrui_atlas.py pack sprites_folder --texture-subpath MyMod --name ui-common
```

This writes a **deployable game-folder tree** (under `-o`/`--output`, or the input folder by
default):

- `Textures\MyMod\ui-common.DDS` — the bin-packed atlas (BC3/DXT5).
- `Meshes\MyMod\ui-common\<sprite>.nif` — one button mesh per image, with the correct UV
  rectangle, size (`W:<w> H:<h>` in the root name + quad geometry), and texture path
  `Textures\MyMod\ui-common.DDS`.

Pass `--manifest` to also write a `<name>.atlas.json` describing the packing (atlas size,
per-sprite pixel rect, ratio). It's informational only — `unpack` doesn't need it.

**`--texture-subpath` is required.** It's the subfolder under `Textures\` — usually your mod's
name — and the atlas is written to `Textures\<texture-subpath>\<name>.DDS` while the nifs go in
their own `Meshes\<texture-subpath>\<name>\` folder (deeper subpaths like `MyMod\Feature` are
fine too). Point `--output` at your mod's data folder (e.g. `-o data\mod`) and the `Textures\`
and `Meshes\` trees drop straight in, matching the paths your mod passes to `UIButton` /
`UIWidget`. Re-running `pack` is safe — it ignores the atlas it previously wrote.

Options (all have sensible defaults):

| Flag | Default | Meaning |
|------|---------|---------|
| `--texture-subpath` | **required** | Subfolder under `Textures\` (usually the mod name) → `Textures\<texture-subpath>\<name>.DDS`. |
| `--name` | input folder name | Atlas base filename. |
| `-o`, `--output` | input folder | Where to write the atlas + nifs. |
| `--format` | `BC3` | Atlas encoding: `BC3` (DXT5, alpha), `BC1` (DXT1, 1-bit alpha), or `RGBA` (uncompressed, lossless, 4× larger). |
| `--padding` | `2` | Transparent gutter between sprites, in px (prevents bilinear bleeding). |
| `--max-size` | `4096` | Largest allowed atlas dimension. |
| `--no-pot` | off | Tight 4px-aligned atlas instead of a square power-of-two. |
| `--manifest` | off | Also write a `<name>.atlas.json` describing the packing. |
| `--template` | embedded | Override the button template NIF (advanced). |

### Size: pixels → units

Each quad's **size follows the sprite's pixel size**, at **100 px = 1 unit** (so a 200×200
sprite → a 2×2-unit quad). That width and height — rounded to 3 digits — are baked into both
the quad geometry and the `VRUI (W:<w> H:<h>)` root-node name, with the quad centred on the
origin; the framework reads the size from the name at runtime. A sprite drawn larger — e.g. a
toggle border meant to sit *around* its button (210 px vs 200 px → 2.1 vs 2.0 units) — yields
a larger quad concentric with the rest.

### Unpack: atlas → images

The reverse, in one argument:

```
python vrui_atlas.py unpack folder\ui-common.DDS
```

Finds the nifs in `pack`'s `Meshes\<subpath>\<name>\` folder (when the DDS is under a
`Textures\` tree), and falls back to the DDS's own folder — so it works whether the files
are split into `Textures\`/`Meshes\` or everything (DDS + nifs) sits in one folder. It uses
only the nifs whose texture path points at **that** DDS (so a folder shared by several
atlases just works), crops each one's region, and writes `<sprite>.png` beside the DDS. `-o`
chooses a different output folder; `--format dds` (with optional `--dds-format`) emits DDS
instead of PNG.

### Fallout-compatible output

The atlas is written in the dimensions and DDS layout the game expects:

- **Square power-of-two** (the smallest 64/128/256/… square the sprites fit in), capped at
  `--max-size`. `--no-pot` gives a tight, 4px-aligned non-square atlas instead. (FO4 accepts
  non-square power-of-two 2D textures too, but square is the safe, conventional default.)
- **No mipmaps** — a packed atlas must not have mipmaps, or adjacent sprites bleed into
  each other at lower mip levels. The header declares a single surface (`mipMapCount = 1`).
- A **DDS header matching known-good FO4 UI textures** byte-for-byte (for BC3: `flags
  0xA1007`, correct `linearSize`, `DXT5` fourcc). Pillow's own header is non-conformant
  (wrong `linearSize`, missing flags), so the tool writes the header itself and uses only
  Pillow's encoded block data.

### Quality

The atlas is encoded **once** from your source images, so if you author lossless PNGs the
result is a single BC3 generation — identical in quality to hand-building a BC3 atlas
(mean per-pixel error ~0.1, confined to antialiased text/border edges). Feeding already
DXT-compressed DDS sprites back through `pack` adds a second BC3 generation; prefer PNG
sources, or use `--format RGBA` for a lossless atlas.

### How it works

`pack` clones an embedded 676-byte FO4 button template and patches only the per-button
fields — UV rectangle, vertex positions, bounding sphere, the `W:<w> H:<h>` root name, and the texture
path. Every other byte (notably the `BSEffectShaderProperty` and `NiAlphaProperty`
settings) is preserved exactly, which is why the buttons render identically to
hand-authored ones. `unpack` validates that exact single-quad signature before trusting
the byte offsets and skips anything that doesn't match.

---

## `nif_to_json.py` — NIF structure dump

```
python nif_to_json.py path/to/mesh.nif            # -> mesh.nif.json
python nif_to_json.py path/to/mesh.nif --stdout --geometry   # include vert/tri/uv arrays
python nif_to_json.py path/to/dir -r              # batch a directory
```

Fetches the [PyNifly](https://github.com/BadDogSkyrim/PyNifly) runtime into `.pynifly/`
on first run (GPL-3.0, git-ignored, not vendored). Re-run with `--refresh-runtime` to
update it.
