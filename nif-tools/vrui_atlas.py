#!/usr/bin/env python3
"""Pack / unpack F4VR Common Framework VRUI button textures and meshes.

A VRUI button is rendered in-game from two files:

  * a .nif mesh  -- a single quad with a ``BSEffectShaderProperty`` that points at a
    shared atlas texture, a UV rectangle selecting this button's region of the atlas,
    and a root node named ``VRUI (W:<w> H:<h>)`` carrying the element's size in framework
    units (the framework reads that size from the name at runtime -- see src/vrui/UIUtils.cpp).
  * a .DDS atlas -- one big texture holding every button/label image.

Authoring those by hand (one combined DDS, one hand-tuned NIF per region) is tedious.
This tool automates both directions:

  pack    Given a folder of discrete sprite images (one per button/label, any
          Pillow-readable format -- PNG recommended, DDS also fine), bin-pack them
          into one atlas .DDS and emit one ready-to-use .nif per sprite, each with the
          correct UV rectangle, aspect ratio, and texture path.

  unpack  Given an atlas .DDS and the .nif files that reference it, crop out the region
          each .nif uses and write it back as a standalone image -- the reverse of pack.

The NIFs produced/consumed are the exact FO4 single-quad VRUI button format (4 verts,
half-precision, BSEffectShaderProperty + NiAlphaProperty). pack clones an embedded
template and patches only the UV rectangle, the vertex positions, the bounding sphere, the
root-node size name, and the texture path -- so the effect shader and alpha settings are
preserved byte-for-byte. unpack validates that signature before trusting the byte
offsets, and errors clearly on anything else.

Only dependency: Pillow (``pip install Pillow``). The NIF read/write is pure-Python; it
does not need the PyNifly runtime that nif_to_json.py uses.

Usage:
    python vrui_atlas.py pack   sprites_folder --texture-subpath MyMod --name ui-common
    python vrui_atlas.py unpack folder\\ui-common.DDS
    python vrui_atlas.py pack   --help

pack writes the atlas + nifs into the input folder by default; unpack finds the nifs
beside the DDS, keeps only those whose texture path points at that DDS, and writes the
cropped images alongside it.
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

# --------------------------------------------------------------------------------------
# Embedded NIF template
#
# A single 676-byte FO4 VRUI button mesh (the "back" button from examples/). pack clones
# this and patches the per-button fields; every other byte -- crucially the
# BSEffectShaderProperty and NiAlphaProperty -- is preserved exactly.
# --------------------------------------------------------------------------------------

_TEMPLATE_B64 = (
    "R2FtZWJyeW8gRmlsZSBGb3JtYXQsIFZlcnNpb24gMjAuMi4wLjcKBwACFAEMAAAABAAAAIIAAAABACdOaWZ0b29scyAzZHMgTWF4"
    "IFBsdWdpbnMgMy44LjAuMGZjNmY1ZgABAAEABAAGAAAATmlOb2RlCgAAAEJTVHJpU2hhcGUWAAAAQlNFZmZlY3RTaGFkZXJQcm9w"
    "ZXJ0eQ8AAABOaUFscGhhUHJvcGVydHkAAAEAAgADAFAAAADSAAAAfgAAAA8AAAACAAAADAAAAAwAAABWUlVJIChXL0g6MSkIAAAA"
    "c3F1YXJlOjAAAAAAAAAAAAAAAAD/////DgAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACA"
    "PwAAgD//////AQAAAAEAAAABAAAAAAAAAP////8OAAAAAAAAAAAAAAAAAAAAAACAMwAAgD8AAAAAAACAvwAAgDMAAAAAAAAAAAAA"
    "AAAAAIA/AACAP/////8AAAAAAAAAAAAAAADzBLU//////wIAAAADAAAABQJDAACwAQACAAAABABcAAAAOIAAvAC8OADlH082/39/"
    "/39/AH84AAA8ALw4AE8yUDb/f3//f38AfzgAADwAPDgATzKKMv9/f/9/fwB/OIAAvAA8OADTH4gy/39//39/AH8AAAEAAgACAAMA"
    "AAD/////AAAAAP////8AAACAEAAAAAAAAAAAAAAAAACAPwAAgD8WAAAAVGV4dHVyZXNcdWlfY29tbW9uLkREUwP/AAAAAIA/AACA"
    "PwAAgD8AAAAAAACAPwAAgD8AAIA/AACAPwAAgD8AAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD//////AAAAAP/////tEIABAAAAAAAA"
    "AA=="
)

# --------------------------------------------------------------------------------------
# Expected geometry signature of a VRUI button BSTriShape (validated before patching).
# Byte offsets are relative to the start of the BSTriShape block.
# --------------------------------------------------------------------------------------

_BST_VERTEXDESC_OFF = 100        # uint64 BSVertexDesc
_BST_VERTEXDESC = 0x0001B00000430205  # half-precision: pos + uv + normal + tangent, stride 20
_BST_NUMVERTS_OFF = 112          # uint16
_BST_DATASIZE_OFF = 114          # uint32
_BST_VERTBUF_OFF = 118           # first vertex
_BST_BSPHERE_CENTER_OFF = 72     # 3x float32
_BST_BSPHERE_RADIUS_OFF = 84     # float32

_VERT_STRIDE = 20
_VERT_POS_Y_OFF = 2              # half, within a vertex record (width axis)
_VERT_POS_Z_OFF = 4              # half, within a vertex record (height axis)
_VERT_UV_OFF = 8                 # 2x half (u, v), within a vertex record

_EXPECT_NUMVERTS = 4
_EXPECT_DATASIZE = 92

_HEADER_VERSION = 0x14020007     # 20.2.0.7
_HEADER_BSVERSION = 130          # FO4

# Framework unit scale: this many source pixels map to one VRUI unit, so a 200px sprite
# becomes a 2-unit quad. The framework reads the resulting size from the node name (see
# src/vrui/UIUtils.cpp).
PIXELS_PER_UNIT = 100


# --------------------------------------------------------------------------------------
# Minimal FO4 NIF reader/writer for this exact dialect
# --------------------------------------------------------------------------------------

class NifFormatError(Exception):
    """Raised when a NIF does not match the expected FO4 VRUI button dialect."""


class _Reader:
    def __init__(self, data: bytes):
        self.d = data
        self.o = 0

    def u8(self) -> int:
        v = self.d[self.o]
        self.o += 1
        return v

    def u16(self) -> int:
        v = struct.unpack_from("<H", self.d, self.o)[0]
        self.o += 2
        return v

    def u32(self) -> int:
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def short_string(self) -> bytes:
        """Export-info string: 1-byte length + that many bytes (content includes its null)."""
        n = self.u8()
        s = self.d[self.o:self.o + n]
        self.o += n
        return s

    def sized_string(self) -> bytes:
        """uint32 length + that many bytes, no terminator."""
        n = self.u32()
        s = self.d[self.o:self.o + n]
        self.o += n
        return s

    def take(self, n: int) -> bytes:
        s = self.d[self.o:self.o + n]
        self.o += n
        return s


@dataclass
class NifDialect:
    """A parsed FO4 NIF restricted to the VRUI button dialect.

    Blocks are kept as opaque byte blobs except where this tool needs to edit them.
    serialize() recomputes the block-size array, string count, and max-string-length,
    so variable-length edits (root name, texture path) need no manual bookkeeping.
    """

    header_line: bytes
    version: int
    endian: int
    user_version: int
    bs_version: int
    export_info: list[bytes]        # 4 short strings
    block_types: list[bytes]
    block_type_index: list[int]     # one per block
    strings: list[bytes]            # the string table
    groups: list[int]
    blocks: list[bytearray]         # one bytearray per block
    footer: bytes

    @classmethod
    def parse(cls, data: bytes) -> "NifDialect":
        r = _Reader(data)
        nl = data.index(b"\n")
        header_line = r.take(nl + 1)

        version = r.u32()
        if version != _HEADER_VERSION:
            raise NifFormatError(
                f"unexpected NIF version {version:#x} (need {_HEADER_VERSION:#x}, FO4 20.2.0.7)"
            )
        endian = r.u8()
        user_version = r.u32()
        num_blocks = r.u32()
        bs_version = r.u32()
        if bs_version != _HEADER_BSVERSION:
            raise NifFormatError(f"unexpected BS version {bs_version} (need {_HEADER_BSVERSION}, FO4)")

        export_info = [r.short_string() for _ in range(4)]

        num_block_types = r.u16()
        block_types = [r.sized_string() for _ in range(num_block_types)]
        block_type_index = [r.u16() for _ in range(num_blocks)]
        block_sizes = [r.u32() for _ in range(num_blocks)]

        num_strings = r.u32()
        r.u32()  # max_string_len -- recomputed on write
        strings = [r.sized_string() for _ in range(num_strings)]

        num_groups = r.u32()
        groups = [r.u32() for _ in range(num_groups)]

        blocks = [bytearray(r.take(sz)) for sz in block_sizes]
        footer = r.d[r.o:]

        return cls(
            header_line=header_line,
            version=version,
            endian=endian,
            user_version=user_version,
            bs_version=bs_version,
            export_info=export_info,
            block_types=block_types,
            block_type_index=block_type_index,
            strings=strings,
            groups=groups,
            blocks=blocks,
            footer=footer,
        )

    def serialize(self) -> bytes:
        out = bytearray()
        out += self.header_line
        out += struct.pack("<I", self.version)
        out += struct.pack("<B", self.endian)
        out += struct.pack("<I", self.user_version)
        out += struct.pack("<I", len(self.blocks))
        out += struct.pack("<I", self.bs_version)
        for s in self.export_info:
            out += struct.pack("<B", len(s)) + s
        out += struct.pack("<H", len(self.block_types))
        for bt in self.block_types:
            out += struct.pack("<I", len(bt)) + bt
        for idx in self.block_type_index:
            out += struct.pack("<H", idx)
        for blk in self.blocks:
            out += struct.pack("<I", len(blk))
        max_str = max((len(s) for s in self.strings), default=0)
        out += struct.pack("<I", len(self.strings))
        out += struct.pack("<I", max_str)
        for s in self.strings:
            out += struct.pack("<I", len(s)) + s
        out += struct.pack("<I", len(self.groups))
        for g in self.groups:
            out += struct.pack("<I", g)
        for blk in self.blocks:
            out += blk
        out += self.footer
        return bytes(out)

    # -- block lookups ------------------------------------------------------------------

    def _block_index_of_type(self, type_name: bytes) -> int:
        try:
            tidx = self.block_types.index(type_name)
        except ValueError as exc:
            raise NifFormatError(f"missing block type {type_name!r}") from exc
        for i, bi in enumerate(self.block_type_index):
            if bi == tidx:
                return i
        raise NifFormatError(f"no block of type {type_name!r}")

    @property
    def _tri_block(self) -> bytearray:
        return self.blocks[self._block_index_of_type(b"BSTriShape")]

    @property
    def _shader_block(self) -> bytearray:
        return self.blocks[self._block_index_of_type(b"BSEffectShaderProperty")]

    def validate_vrui_button(self) -> None:
        """Ensure this is the exact single-quad VRUI button format we can safely patch."""
        expected = [b"NiNode", b"BSTriShape", b"BSEffectShaderProperty", b"NiAlphaProperty"]
        for t in expected:
            if t not in self.block_types:
                raise NifFormatError(
                    f"not a VRUI button mesh: missing {t.decode()} "
                    f"(found {[t.decode() for t in self.block_types]})"
                )
        blk = self._tri_block
        vdesc = struct.unpack_from("<Q", blk, _BST_VERTEXDESC_OFF)[0]
        if vdesc != _BST_VERTEXDESC:
            raise NifFormatError(
                f"unexpected BSTriShape vertex format {vdesc:#018x} "
                f"(need {_BST_VERTEXDESC:#018x}); not a standard VRUI button mesh"
            )
        nverts = struct.unpack_from("<H", blk, _BST_NUMVERTS_OFF)[0]
        dsize = struct.unpack_from("<I", blk, _BST_DATASIZE_OFF)[0]
        if nverts != _EXPECT_NUMVERTS or dsize != _EXPECT_DATASIZE:
            raise NifFormatError(
                f"unexpected geometry (verts={nverts}, dataSize={dsize}); "
                "VRUI button meshes are a single 4-vertex quad"
            )

    # -- root node name (the "VRUI (W:<w> H:<h>)" string) -------------------------------

    def _root_name_index(self) -> int:
        # block 0 is the root NiNode; its first uint32 is nameID -> index into string table.
        name_id = struct.unpack_from("<I", self.blocks[0], 0)[0]
        if name_id >= len(self.strings):
            raise NifFormatError("root node name index out of range")
        return name_id

    def get_root_name(self) -> str:
        return self.strings[self._root_name_index()].decode("utf-8", "replace")

    def set_root_name(self, name: str) -> None:
        self.strings[self._root_name_index()] = name.encode("utf-8")

    # -- diffuse / source texture path --------------------------------------------------

    @staticmethod
    def _find_dds_string(blk: bytes) -> tuple[int, int]:
        """Return (offset, length) of the first inline uint32-prefixed string ending in .dds."""
        o = 0
        end = len(blk) - 4
        while o <= end:
            length = struct.unpack_from("<I", blk, o)[0]
            if 1 <= length <= 300 and o + 4 + length <= len(blk):
                s = blk[o + 4:o + 4 + length]
                if s[-4:].lower() == b".dds" and all(32 <= c < 127 for c in s):
                    return o, length
            o += 1
        raise NifFormatError("could not locate source texture string in BSEffectShaderProperty")

    def get_source_texture(self) -> str:
        blk = self._shader_block
        off, length = self._find_dds_string(blk)
        return blk[off + 4:off + 4 + length].decode("utf-8", "replace")

    def set_source_texture(self, path: str) -> None:
        idx = self._block_index_of_type(b"BSEffectShaderProperty")
        blk = self.blocks[idx]
        off, length = self._find_dds_string(blk)
        new = path.encode("utf-8")
        patched = blk[:off] + struct.pack("<I", len(new)) + new + blk[off + 4 + length:]
        self.blocks[idx] = bytearray(patched)

    # -- geometry: UV rectangle, vertex positions, bounding sphere -----------------------

    def get_uvs(self) -> list[tuple[float, float]]:
        blk = self._tri_block
        uvs = []
        for i in range(_EXPECT_NUMVERTS):
            base = _BST_VERTBUF_OFF + _VERT_STRIDE * i + _VERT_UV_OFF
            u, v = struct.unpack_from("<2e", blk, base)
            uvs.append((u, v))
        return uvs

    def get_pos_yz(self) -> list[tuple[float, float]]:
        blk = self._tri_block
        out = []
        for i in range(_EXPECT_NUMVERTS):
            base = _BST_VERTBUF_OFF + _VERT_STRIDE * i
            y = struct.unpack_from("<e", blk, base + _VERT_POS_Y_OFF)[0]
            z = struct.unpack_from("<e", blk, base + _VERT_POS_Z_OFF)[0]
            out.append((y, z))
        return out

    def set_button_geometry(self, u_min: float, u_max: float, v_min: float, v_max: float,
                            half_w: float, half_h: float) -> None:
        """Patch the quad: UV rectangle + vertex positions + bounding sphere.

        v_min is the top of the region (smaller V), v_max the bottom, matching NIF UVs
        where V grows downward. The quad is centred at the origin and spans +/-half_w on
        the width axis (Y) and +/-half_h on the height axis (Z), so a sprite drawn larger --
        e.g. a toggle border meant to sit around its button -- yields a larger quad
        concentric with the smaller ones.
        """
        blk = self._tri_block
        # Vertex order in the template: v0=(left,bottom) v1=(right,bottom) v2=(right,top) v3=(left,top)
        corner_uv = [
            (u_min, v_max),  # v0 left/bottom
            (u_max, v_max),  # v1 right/bottom
            (u_max, v_min),  # v2 right/top
            (u_min, v_min),  # v3 left/top
        ]
        for i, (u, v) in enumerate(corner_uv):
            base = _BST_VERTBUF_OFF + _VERT_STRIDE * i
            struct.pack_into("<2e", blk, base + _VERT_UV_OFF, u, v)
            # Width axis (Y): right corners (v1, v2) -> +half_w, left (v0, v3) -> -half_w.
            struct.pack_into("<e", blk, base + _VERT_POS_Y_OFF, half_w if i in (1, 2) else -half_w)
            # Height axis (Z): top corners (v2, v3) -> +half_h, bottom (v0, v1) -> -half_h.
            struct.pack_into("<e", blk, base + _VERT_POS_Z_OFF, half_h if i in (2, 3) else -half_h)
        # Bounding sphere centred at origin; radius reaches the farthest corner.
        struct.pack_into("<3f", blk, _BST_BSPHERE_CENTER_OFF, 0.0, 0.0, 0.0)
        struct.pack_into("<f", blk, _BST_BSPHERE_RADIUS_OFF, math.sqrt(half_w * half_w + half_h * half_h))


# --------------------------------------------------------------------------------------
# Bin packing (MaxRects best-short-side-fit, with square power-of-two atlas search)
# --------------------------------------------------------------------------------------

@dataclass
class Placement:
    key: str
    x: int
    y: int
    w: int
    h: int


def _round_up(n: int, multiple: int) -> int:
    return ((n + multiple - 1) // multiple) * multiple


@dataclass
class _Rect:
    x: int
    y: int
    w: int
    h: int


def _contains(outer: _Rect, inner: _Rect) -> bool:
    return (outer.x <= inner.x and outer.y <= inner.y
            and outer.x + outer.w >= inner.x + inner.w
            and outer.y + outer.h >= inner.y + inner.h)


def _split_free(r: _Rect, p: _Rect) -> list[_Rect]:
    """Subtract placed rect `p` from free rect `r`, yielding the maximal remaining rects."""
    if p.x >= r.x + r.w or p.x + p.w <= r.x or p.y >= r.y + r.h or p.y + p.h <= r.y:
        return [r]  # no overlap
    out = []
    if p.x > r.x:
        out.append(_Rect(r.x, r.y, p.x - r.x, r.h))                       # left slab
    if p.x + p.w < r.x + r.w:
        out.append(_Rect(p.x + p.w, r.y, r.x + r.w - (p.x + p.w), r.h))   # right slab
    if p.y > r.y:
        out.append(_Rect(r.x, r.y, r.w, p.y - r.y))                       # top slab
    if p.y + p.h < r.y + r.h:
        out.append(_Rect(r.x, p.y + p.h, r.w, r.y + r.h - (p.y + p.h)))   # bottom slab
    return out


def _prune(rects: list[_Rect]) -> list[_Rect]:
    """Drop degenerate rects and any rect fully contained in another."""
    rects = [r for r in rects if r.w > 0 and r.h > 0]
    removed = [False] * len(rects)
    for i in range(len(rects)):
        if removed[i]:
            continue
        for j in range(len(rects)):
            if i != j and not removed[j] and _contains(rects[j], rects[i]):
                removed[i] = True
                break
    return [r for i, r in enumerate(rects) if not removed[i]]


def _maxrects_pack(items: list[tuple[str, int, int]], bin_w: int, bin_h: int, padding: int):
    """MaxRects best-short-side-fit packing of (key, w, h) into a bin_w x bin_h bin.

    Each sprite reserves a `padding` gutter on its right/bottom, and the usable area is
    inset by `padding` on the top/left, so every sprite has >= `padding` of empty space to
    its neighbours and the edges. Returns (placements, used_w, used_h), or None if any
    sprite doesn't fit.
    """
    free = [_Rect(padding, padding, bin_w - padding, bin_h - padding)]
    placements: list[Placement] = []
    used_w = used_h = 0
    for key, w, h in items:
        fw, fh = w + padding, h + padding
        best = None  # (short_side_leftover, long_side_leftover, free_index)
        for i, fr in enumerate(free):
            if fw <= fr.w and fh <= fr.h:
                leftover = (min(fr.w - fw, fr.h - fh), max(fr.w - fw, fr.h - fh))
                if best is None or leftover < (best[0], best[1]):
                    best = (leftover[0], leftover[1], i)
        if best is None:
            return None
        spot = free[best[2]]
        placements.append(Placement(key, spot.x, spot.y, w, h))
        used_w = max(used_w, spot.x + w + padding)
        used_h = max(used_h, spot.y + h + padding)
        placed = _Rect(spot.x, spot.y, fw, fh)
        nxt = []
        for r in free:
            nxt.extend(_split_free(r, placed))
        free = _prune(nxt)
    return placements, used_w, used_h


def pack_atlas(items: list[tuple[str, int, int]], padding: int, max_size: int, pot: bool):
    """Pack (key, w, h) sprites. Returns (atlas_w, atlas_h, list[Placement]).

    Default (pot=True) is a square power-of-two atlas -- the smallest S in {64,128,...}
    the MaxRects placer fits every sprite into. --no-pot instead returns a tight,
    4-pixel-aligned (non-square) atlas.
    """
    ordered = sorted(items, key=lambda it: (-(it[1] * it[2]), -max(it[1], it[2]), it[0]))

    if not pot:
        result = _maxrects_pack(ordered, max_size, max_size, padding)
        if result is None:
            raise SystemExit(
                f"error: sprites do not fit in {max_size}x{max_size} (raise --max-size)")
        placements, used_w, used_h = result
        return _round_up(used_w, 4), _round_up(used_h, 4), placements

    size = 64
    while size <= max_size:
        result = _maxrects_pack(ordered, size, size, padding)
        if result is not None:
            return size, size, result[0]
        size <<= 1
    raise SystemExit(
        f"error: sprites do not fit in a {max_size}x{max_size} square atlas "
        f"(raise --max-size, or use --no-pot for a tight non-square atlas)"
    )


# --------------------------------------------------------------------------------------
# Pillow helpers
# --------------------------------------------------------------------------------------

def _import_pillow():
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit("error: Pillow is required -- install it with: pip install Pillow")
    return Image


_IMAGE_EXTS = {".png", ".dds", ".tga", ".bmp", ".jpg", ".jpeg", ".tif", ".tiff", ".gif", ".webp"}

# DDS header flags / pixel-format constants (see Microsoft DDS_HEADER docs).
_DDSD_CAPS = 0x1
_DDSD_HEIGHT = 0x2
_DDSD_WIDTH = 0x4
_DDSD_PITCH = 0x8
_DDSD_PIXELFORMAT = 0x1000
_DDSD_MIPMAPCOUNT = 0x20000
_DDSD_LINEARSIZE = 0x80000
_DDPF_ALPHAPIXELS = 0x1
_DDPF_FOURCC = 0x4
_DDPF_RGB = 0x40
_DDSCAPS_TEXTURE = 0x1000

# Map our format names to (Pillow pixel_format, fourcc, bytes-per-4x4-block).
_COMPRESSED = {
    "BC3": ("DXT5", b"DXT5", 16),
    "DXT5": ("DXT5", b"DXT5", 16),
    "BC1": ("DXT1", b"DXT1", 8),
    "DXT1": ("DXT1", b"DXT1", 8),
}


def _dds_header(w: int, h: int, *, fourcc: bytes | None, linear_size: int | None,
                pitch: int | None, rgb_bits: int, masks: tuple[int, int, int, int],
                pf_flags: int) -> bytes:
    """Build a 128-byte DDS header matching the conventions of known-good FO4 UI textures.

    Single surface, no mipmaps (atlases must not mip -- adjacent sprites would bleed),
    depth/mipMapCount = 1, and a correct pitchOrLinearSize. Mirrors the example atlas
    header (flags 0xA1007 for compressed) so the game's loader accepts it.
    """
    flags = _DDSD_CAPS | _DDSD_HEIGHT | _DDSD_WIDTH | _DDSD_PIXELFORMAT | _DDSD_MIPMAPCOUNT
    pols = 0
    if linear_size is not None:
        flags |= _DDSD_LINEARSIZE
        pols = linear_size
    elif pitch is not None:
        flags |= _DDSD_PITCH
        pols = pitch

    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<I", hdr, 4, 124)            # dwSize
    struct.pack_into("<I", hdr, 8, flags)
    struct.pack_into("<I", hdr, 12, h)
    struct.pack_into("<I", hdr, 16, w)
    struct.pack_into("<I", hdr, 20, pols)
    struct.pack_into("<I", hdr, 24, 1)             # dwDepth
    struct.pack_into("<I", hdr, 28, 1)             # dwMipMapCount
    # dwReserved1[11] @ 32..76 stays zero
    struct.pack_into("<I", hdr, 76, 32)            # DDS_PIXELFORMAT.dwSize
    struct.pack_into("<I", hdr, 80, pf_flags)
    if fourcc:
        hdr[84:88] = fourcc
    struct.pack_into("<I", hdr, 88, rgb_bits)
    struct.pack_into("<IIII", hdr, 92, *masks)
    struct.pack_into("<I", hdr, 108, _DDSCAPS_TEXTURE)  # dwCaps
    # dwCaps2/3/4 + dwReserved2 @ 112..128 stay zero
    return bytes(hdr)


def _save_dds(image, path: Path, fmt: str) -> None:
    fmt = fmt.upper()
    w, h = image.size
    if fmt == "RGBA":
        # Uncompressed A8R8G8B8 (BGRA byte order matches these masks).
        bgra = image.convert("RGBA").tobytes("raw", "BGRA")
        hdr = _dds_header(w, h, fourcc=None, linear_size=None, pitch=w * 4, rgb_bits=32,
                          masks=(0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000),
                          pf_flags=_DDPF_RGB | _DDPF_ALPHAPIXELS)
        path.write_bytes(hdr + bgra)
        return

    if fmt not in _COMPRESSED:
        raise SystemExit(f"error: unknown atlas format {fmt!r} (use BC3, BC1, or RGBA)")

    pillow_fmt, fourcc, block = _COMPRESSED[fmt]
    buf = io.BytesIO()
    image.save(buf, format="DDS", pixel_format=pillow_fmt)
    payload = buf.getvalue()[128:]  # drop Pillow's header, keep the encoded blocks
    linear_size = ((w + 3) // 4) * ((h + 3) // 4) * block
    if len(payload) < linear_size:
        raise SystemExit(f"error: DDS encoder produced {len(payload)} bytes, expected >= {linear_size}")
    hdr = _dds_header(w, h, fourcc=fourcc, linear_size=linear_size, pitch=None, rgb_bits=0,
                      masks=(0, 0, 0, 0), pf_flags=_DDPF_FOURCC)
    path.write_bytes(hdr + payload[:linear_size])


# --------------------------------------------------------------------------------------
# pack
# --------------------------------------------------------------------------------------

def _gather_sprites(inputs: list[Path], exclude_names: set[str] = frozenset()) -> list[Path]:
    excluded = {n.lower() for n in exclude_names}
    files: list[Path] = []
    for inp in inputs:
        if inp.is_dir():
            files.extend(sorted(
                p for p in inp.iterdir()
                if p.suffix.lower() in _IMAGE_EXTS and p.name.lower() not in excluded
            ))
        elif inp.is_file():
            if inp.name.lower() not in excluded:
                files.append(inp)
        else:
            raise SystemExit(f"error: {inp} not found")
    if not files:
        raise SystemExit("error: no sprite images found")
    # De-dup while preserving order.
    seen, unique = set(), []
    for f in files:
        if f.resolve() not in seen:
            seen.add(f.resolve())
            unique.append(f)
    return unique


def cmd_pack(args: argparse.Namespace) -> int:
    Image = _import_pillow()

    first = args.input[0]
    atlas_name = args.name or (first.name if first.is_dir() else "ui-common")
    out_dir: Path = args.output or (first if first.is_dir() else first.parent)

    # Mirror the in-game Data layout under --output so the tree drops straight into a mod's
    # data folder: the atlas goes in Textures\<subpath>\, and the nifs in their own
    # Meshes\<subpath>\<name>\ folder (so multiple atlases sharing a subpath keep their nifs
    # apart). Same subpath, usually the mod name.
    subpath = _normalize_subpath(args.texture_subpath)
    sub_parts = subpath.split("\\")
    textures_dir = out_dir.joinpath("Textures", *sub_parts)
    meshes_dir = out_dir.joinpath("Meshes", *sub_parts, atlas_name)
    textures_dir.mkdir(parents=True, exist_ok=True)
    meshes_dir.mkdir(parents=True, exist_ok=True)

    atlas_file = textures_dir / f"{atlas_name}.DDS"
    texture_path = f"Textures\\{subpath}\\{atlas_name}.DDS"

    # Exclude the atlas we're about to write so re-running in place doesn't pack it.
    sprite_paths = _gather_sprites(args.input, exclude_names={atlas_file.name})

    sprites = {}
    items = []
    for path in sprite_paths:
        key = path.stem
        if key in sprites:
            raise SystemExit(f"error: duplicate sprite name {key!r} (from {path})")
        img = Image.open(path).convert("RGBA")
        sprites[key] = img
        items.append((key, img.width, img.height))

    atlas_w, atlas_h, placements = pack_atlas(items, args.padding, args.max_size, not args.no_pot)

    atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))
    for p in placements:
        atlas.paste(sprites[p.key], (p.x, p.y))
    _save_dds(atlas, atlas_file, args.format)

    template_bytes = _load_template(args.template)
    manifest_sprites = []

    for p in placements:
        # Real element size in framework units, baked into both the node name (which the
        # framework reads at runtime) and the quad geometry: PIXELS_PER_UNIT px -> 1 unit, so
        # a 200px sprite is a 2-unit quad. The quad is centred on the origin, so a sprite
        # drawn larger (e.g. a toggle border meant to frame its button) yields a larger quad
        # concentric with the rest.
        w_units = round(p.w / PIXELS_PER_UNIT, 3)
        h_units = round(p.h / PIXELS_PER_UNIT, 3)
        u_min = p.x / atlas_w
        u_max = (p.x + p.w) / atlas_w
        v_min = p.y / atlas_h            # top of the region
        v_max = (p.y + p.h) / atlas_h    # bottom of the region

        nif = NifDialect.parse(template_bytes)
        nif.validate_vrui_button()
        nif.set_root_name(f"VRUI (W:{_fmt_num(w_units)} H:{_fmt_num(h_units)})")
        nif.set_source_texture(texture_path)
        nif.set_button_geometry(u_min, u_max, v_min, v_max, w_units / 2, h_units / 2)

        nif_file = meshes_dir / f"{p.key}.nif"
        nif_file.write_bytes(nif.serialize())

        if args.manifest:
            manifest_sprites.append({
                "name": p.key,
                "nif": nif_file.relative_to(out_dir).as_posix(),
                "rect": [p.x, p.y, p.w, p.h],
                "size": [w_units, h_units],
            })

    print(f"packed {len(placements)} sprites into {atlas_w}x{atlas_h} {args.format.upper()} atlas")
    print(f"  texture: {atlas_file}")
    print(f"  meshes:  {len(placements)} nifs in {meshes_dir}")
    print(f"  texture path in nifs: {texture_path}")

    if args.manifest:
        manifest_file = out_dir / f"{atlas_name}.atlas.json"
        manifest_file.write_text(json.dumps({
            "atlas": atlas_file.relative_to(out_dir).as_posix(),
            "atlas_size": [atlas_w, atlas_h],
            "format": args.format.upper(),
            "texture_path": texture_path,
            "padding": args.padding,
            "sprites": manifest_sprites,
        }, indent=2), encoding="utf-8")
        print(f"  manifest: {manifest_file}")
    return 0


def _normalize_subpath(subpath: str) -> str:
    """Clean --texture-subpath into a backslash path used under both Textures\\ and Meshes\\.

    Usually the mod's name (e.g. MyMod); deeper paths like MyMod\\Feature are fine. A
    leading "Textures\\" is stripped if the user includes it.
    """
    sub = subpath.replace("/", "\\").strip("\\").strip()
    if sub.lower().startswith("textures\\"):
        sub = sub[len("textures\\"):]
    if not sub:
        raise SystemExit("error: --texture-subpath must not be empty (use your mod name, e.g. MyMod)")
    return sub


def _fmt_num(value: float) -> str:
    """Format a unit value compactly: integers stay integer, else trimmed decimals."""
    if value == int(value):
        return str(int(value))
    return f"{value:.3f}".rstrip("0").rstrip(".")


def _load_template(template: Path | None) -> bytes:
    if template is not None:
        data = template.read_bytes()
    else:
        data = base64.b64decode(_TEMPLATE_B64)
    # Fail fast if a supplied template is not the expected dialect.
    NifDialect.parse(data).validate_vrui_button()
    return data


# --------------------------------------------------------------------------------------
# unpack
# --------------------------------------------------------------------------------------

def _texture_basename(path: str) -> str:
    """Filename of a NIF texture path, regardless of \\ or / separators."""
    return path.replace("\\", "/").rsplit("/", 1)[-1].lower()


def _nif_search_dirs(dds_path: Path) -> list[Path]:
    """Folders to scan for nifs, in priority order. When the atlas sits under a Textures\\
    tree (as `pack` writes it), prefer the parallel Meshes\\<subpath>\\<name>\\ folder, then
    Meshes\\<subpath>\\. The atlas's own folder is the final fallback -- i.e. everything
    (DDS + nifs) sitting in one folder."""
    dirs = []
    parts = dds_path.parent.parts
    for i in range(len(parts) - 1, -1, -1):
        if parts[i].lower() == "textures":
            meshes_base = Path(*parts[:i], "Meshes", *parts[i + 1:])
            dirs.append(meshes_base / dds_path.stem)  # Meshes\<subpath>\<name>\ (pack layout)
            dirs.append(meshes_base)                  # Meshes\<subpath>\
            break
    dirs.append(dds_path.parent)                      # same folder as the DDS (final fallback)
    seen, out = set(), []
    for d in dirs:
        if d.is_dir() and d.resolve() not in seen:
            seen.add(d.resolve())
            out.append(d)
    return out


def cmd_unpack(args: argparse.Namespace) -> int:
    Image = _import_pillow()

    dds_path: Path = args.atlas
    if not dds_path.is_file():
        raise SystemExit(f"error: {dds_path} not found")

    atlas = Image.open(dds_path).convert("RGBA")
    aw, ah = atlas.size

    out_dir: Path = args.output or dds_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    ext = "." + args.format.lower().lstrip(".")

    target = dds_path.name.lower()
    search_dirs = _nif_search_dirs(dds_path)

    # First folder that holds nifs referencing this atlas wins, so the structured Meshes\
    # layout is preferred and "everything in one folder" is the final fallback.
    matches: list[tuple[Path, list[tuple[float, float]]]] = []
    scanned = 0
    for d in search_dirs:
        found = []
        for path in sorted(d.glob("*.nif")):
            scanned += 1
            try:
                nif = NifDialect.parse(path.read_bytes())
                nif.validate_vrui_button()
                if _texture_basename(nif.get_source_texture()) != target:
                    continue  # belongs to a different atlas
                found.append((path, nif.get_uvs()))
            except NifFormatError:
                continue  # not a VRUI button mesh -- ignore
        if found:
            matches = found
            break

    if not matches:
        dirs_str = ", ".join(str(d) for d in search_dirs)
        print(f"no nifs reference {dds_path.name} (checked {scanned} .nif file(s) in: {dirs_str})",
              file=sys.stderr)
        return 1

    count = 0
    for path, uvs in matches:
        us = [u for u, _ in uvs]
        vs = [v for _, v in uvs]
        left = max(0, round(min(us) * aw))
        right = min(aw, round(max(us) * aw))
        top = max(0, round(min(vs) * ah))
        bottom = min(ah, round(max(vs) * ah))
        if right <= left or bottom <= top:
            print(f"skip {path.name}: degenerate UV rectangle", file=sys.stderr)
            continue

        region = atlas.crop((left, top, right, bottom))
        out_file = out_dir / f"{path.stem}{ext}"
        if ext == ".dds":
            _save_dds(region, out_file, args.dds_format)
        else:
            region.save(out_file)
        count += 1
        print(f"  {path.name} -> {out_file.name}  ({right - left}x{bottom - top})")

    print(f"unpacked {count} sprites from {dds_path.name} ({aw}x{ah}) into {out_dir}")
    return 0


# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="Pack/unpack F4VR Common Framework VRUI button textures and meshes.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="command", required=True)

    pp = sub.add_parser("pack", help="folder of sprites -> atlas DDS + one .nif per sprite")
    pp.add_argument("input", type=Path, nargs="+", help="folder of sprite images (or image files)")
    pp.add_argument("--texture-subpath", required=True,
                    help=r"subfolder under Textures\ for the atlas (required); usually your mod "
                         r"name, e.g. MyMod -> Textures\MyMod\<name>.DDS")
    pp.add_argument("--name", default=None, help="atlas base name (default: input folder name)")
    pp.add_argument("-o", "--output", type=Path, default=None, help="output folder (default: input folder)")
    pp.add_argument("--format", default="BC3", help="atlas format: BC3 (default), BC1, or RGBA")
    pp.add_argument("--padding", type=int, default=2, help="gutter between sprites in px (default: 2)")
    pp.add_argument("--max-size", type=int, default=4096, help="max atlas dimension (default: 4096)")
    pp.add_argument("--no-pot", action="store_true",
                    help="use a tight 4px-aligned atlas instead of a square power-of-two")
    pp.add_argument("--manifest", action="store_true", help="also write a <name>.atlas.json manifest of the packing")
    pp.add_argument("--template", type=Path, default=None, help="override the embedded button template .nif")
    pp.set_defaults(func=cmd_pack)

    up = sub.add_parser("unpack", help="atlas DDS -> one cropped image per nif that uses it")
    up.add_argument("atlas", type=Path, help="atlas .DDS file (nifs are auto-found beside it)")
    up.add_argument("-o", "--output", type=Path, default=None, help="output folder (default: the DDS folder)")
    up.add_argument("--format", default="png", help="output image format: png (default) or dds")
    up.add_argument("--dds-format", default="BC3", help="DDS encoding when --format dds (default: BC3)")
    up.set_defaults(func=cmd_unpack)

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
