#!/usr/bin/env python3
"""Extract the full structure of a Bethesda NIF file into JSON for analysis.

Uses PyNifly (https://github.com/BadDogSkyrim/PyNifly), which is the Python
wrapper around the same nifly C++ library used by BodySlide / Outfit Studio.
Supports Skyrim LE/SE, Fallout 3 / NV / 4 / 76.

PyNifly is GPL-3.0 and is *not* vendored into this MIT-licensed repo. Instead
the script fetches the latest release on first run and extracts the runtime
(NiflyDLL.dll + the `pyn` Python package) into nif-tools/.pynifly/, which is
gitignored. Re-run with --refresh-runtime to update.

Usage:
    python nif_to_json.py path/to/mesh.nif
    python nif_to_json.py path/to/mesh.nif -o out.json
    python nif_to_json.py path/to/mesh.nif --geometry      # include vert/tri/uv arrays
    python nif_to_json.py path/to/dir -r                   # batch
    python nif_to_json.py --refresh-runtime                # re-download PyNifly
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUNTIME_DIR = HERE / ".pynifly"
CACHE_DIR = HERE / ".cache"
RELEASE_URL = "https://github.com/BadDogSkyrim/PyNifly/releases/download/V25.14/io_scene_nifly.zip"
ZIP_NAME = "io_scene_nifly.zip"


# --------------------------------------------------------------------------------------
# Runtime fetch
# --------------------------------------------------------------------------------------

def ensure_runtime(force: bool = False) -> None:
    """Download and extract the PyNifly runtime into RUNTIME_DIR if missing."""
    pyn_init = RUNTIME_DIR / "pyn" / "pynifly.py"
    dll = RUNTIME_DIR / "NiflyDLL.dll"
    if not force and pyn_init.is_file() and dll.is_file():
        return

    print(f"fetching PyNifly runtime from {RELEASE_URL}", file=sys.stderr)
    CACHE_DIR.mkdir(exist_ok=True)
    zip_path = CACHE_DIR / ZIP_NAME
    if force or not zip_path.is_file():
        with urllib.request.urlopen(RELEASE_URL) as resp, open(zip_path, "wb") as out:
            shutil.copyfileobj(resp, out)

    if RUNTIME_DIR.exists():
        shutil.rmtree(RUNTIME_DIR, ignore_errors=True)
    RUNTIME_DIR.mkdir()

    extracted = 0
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():
            nf = name.replace("\\", "/")
            if not nf.startswith("io_scene_nifly/"):
                continue
            rel = nf[len("io_scene_nifly/"):]
            if not (rel.startswith("pyn/") or rel == "NiflyDLL.dll"):
                continue
            if "/__pycache__/" in rel or rel.endswith("/"):
                continue
            out_path = RUNTIME_DIR / rel
            out_path.parent.mkdir(parents=True, exist_ok=True)
            with z.open(name) as fin, open(out_path, "wb") as fout:
                shutil.copyfileobj(fin, fout)
            extracted += 1
    print(f"extracted {extracted} files to {RUNTIME_DIR}", file=sys.stderr)


def load_pynifly():
    """Add runtime to sys.path and import pyn.pynifly."""
    sys.path.insert(0, str(RUNTIME_DIR))
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(RUNTIME_DIR))
        os.add_dll_directory(str(RUNTIME_DIR / "pyn"))
    os.environ["PATH"] = str(RUNTIME_DIR) + os.pathsep + os.environ.get("PATH", "")
    from pyn import pynifly  # noqa: WPS433  (deferred import after path setup)
    return pynifly


# --------------------------------------------------------------------------------------
# Serialization helpers
# --------------------------------------------------------------------------------------

def _to_jsonable(v):
    """Recursively convert ctypes / pynifly objects into JSON-friendly Python values."""
    if v is None or isinstance(v, (bool, int, float, str)):
        return v
    if isinstance(v, bytes):
        # NIF strings are null-terminated bytes; bare bytes become decoded strings.
        s = v.rstrip(b"\x00")
        try:
            return s.decode("utf-8")
        except UnicodeDecodeError:
            return list(v)
    if isinstance(v, (list, tuple)):
        return [_to_jsonable(x) for x in v]
    if isinstance(v, dict):
        return {str(k): _to_jsonable(val) for k, val in v.items()}
    if isinstance(v, ctypes.Array):
        return [_to_jsonable(x) for x in v]
    if isinstance(v, ctypes.Structure):
        return struct_to_dict(v)
    return repr(v)


def struct_to_dict(s) -> dict:
    """Convert a ctypes.Structure to a dict using its declared _fields_."""
    if s is None:
        return None
    out = {}
    for name, _ in getattr(s, "_fields_", []):
        try:
            out[name] = _to_jsonable(getattr(s, name))
        except Exception as e:
            out[name] = f"<err {e}>"
    return out


def transform_to_dict(t) -> dict | None:
    if t is None:
        return None
    return {
        "translation": list(t.translation),
        "rotation": [list(r) for r in t.rotation],
        "scale": float(t.scale),
    }


# --------------------------------------------------------------------------------------
# NIF -> dict
# --------------------------------------------------------------------------------------

def serialize_node(node, children_by_parent: dict[int, list[int]]) -> dict:
    out = {
        "id": node.id,
        "name": node.name,
        "type": node.blockname,
        "flags": getattr(node, "flags", None),
        "parent_id": node.parent.id if node.parent else None,
        "child_ids": children_by_parent.get(node.id, []),
        "transform": transform_to_dict(getattr(node, "transform", None)),
        "global_transform": transform_to_dict(getattr(node, "global_transform", None)),
        "properties": struct_to_dict(getattr(node, "properties", None)),
    }
    coll = getattr(node, "collision_object", None)
    if coll is not None:
        out["collision_object_id"] = coll.id
    ctrl = getattr(node, "controller", None)
    if ctrl is not None:
        out["controller_id"] = getattr(ctrl, "id", None)
    return out


def serialize_shader(shader) -> dict | None:
    if shader is None:
        return None
    return {
        "type": type(shader).__name__,
        "block_name": getattr(shader, "blockname", None),
        "name": getattr(shader, "name", None),
        "id": getattr(shader, "id", None),
        "properties": struct_to_dict(getattr(shader, "properties", None)),
    }


def serialize_alpha(alpha) -> dict | None:
    if alpha is None:
        return None
    return {
        "id": alpha.id,
        "type": alpha.blockname,
        "properties": struct_to_dict(getattr(alpha, "properties", None)),
    }


def serialize_shape(shape, include_geometry: bool) -> dict:
    out = {
        "id": shape.id,
        "name": shape.name,
        "type": shape.blockname,
        "flags": getattr(shape, "flags", None),
        "parent_id": shape.parent.id if shape.parent else None,
        "transform": transform_to_dict(getattr(shape, "transform", None)),
        "global_transform": transform_to_dict(getattr(shape, "global_transform", None)),
        "properties": struct_to_dict(getattr(shape, "properties", None)),
        "vert_count": len(shape.verts) if shape.verts else 0,
        "tri_count": len(shape.tris) if shape.tris else 0,
        "uv_count": len(shape.uvs) if shape.uvs else 0,
        "normal_count": len(shape.normals) if shape.normals else 0,
        "color_count": len(shape.colors) if shape.colors else 0,
        "bone_names": list(shape.bone_names) if shape.bone_names else [],
        "bone_count": len(shape.bone_names) if shape.bone_names else 0,
        "textures": _to_jsonable(getattr(shape, "textures", {}) or {}),
        "shader_block_name": getattr(shape, "shader_block_name", None),
        "has_alpha_property": bool(getattr(shape, "has_alpha_property", False)),
        "has_skin_instance": bool(getattr(shape, "has_skin_instance", False)),
        "is_head_part": bool(getattr(shape, "is_head_part", False)),
        "segment_file": getattr(shape, "segment_file", "") or "",
        "shader": serialize_shader(getattr(shape, "shader", None)),
        "alpha_property": serialize_alpha(getattr(shape, "alpha_property", None)),
    }

    coll = getattr(shape, "collision_object", None)
    if coll is not None:
        out["collision_object"] = {
            "id": getattr(coll, "id", None),
            "type": getattr(coll, "blockname", type(coll).__name__),
        }

    bw = getattr(shape, "bone_weights", None)
    if bw:
        out["bone_weight_summary"] = {
            bone: {"vert_count": len(weights)}
            for bone, weights in bw.items()
        }

    if include_geometry:
        out["verts"] = [list(v) for v in (shape.verts or [])]
        out["tris"] = [list(t) for t in (shape.tris or [])]
        out["uvs"] = [list(u) for u in (shape.uvs or [])]
        out["normals"] = [list(n) for n in (shape.normals or [])]
        out["colors"] = [list(c) for c in (shape.colors or [])]
        if bw:
            out["bone_weights"] = {
                bone: [list(w) for w in weights]
                for bone, weights in bw.items()
            }

    return out


def nif_to_dict(nif, *, include_geometry: bool = False) -> dict:
    nodes = list(nif.nodes.values())
    shapes_by_id = {s.id: s for s in nif.shapes}

    children_by_parent: dict[int, list[int]] = {}
    for n in nodes:
        if n.parent is not None:
            children_by_parent.setdefault(n.parent.id, []).append(n.id)

    type_counts: dict[str, int] = {}
    for n in nodes:
        type_counts[n.blockname] = type_counts.get(n.blockname, 0) + 1

    serialized_nodes = []
    for n in nodes:
        if n.id in shapes_by_id:
            # Shapes get the rich shape serializer; skip them in the bare nodes list.
            continue
        serialized_nodes.append(serialize_node(n, children_by_parent))

    serialized_shapes = [
        serialize_shape(s, include_geometry=include_geometry) for s in nif.shapes
    ]

    return {
        "header": {
            "game": nif.game,
            "filepath": str(nif.filepath),
            "root_name": nif.rootName,
            "root_id": nif.rootNode.id if nif.rootNode else None,
            "max_string_len": nif.max_string_len,
            "has_extra_data": bool(nif.has_extra_data),
            "node_count": len(nodes),
            "shape_count": len(nif.shapes),
            "block_type_counts": dict(sorted(type_counts.items(), key=lambda kv: -kv[1])),
        },
        "nodes": serialized_nodes,
        "shapes": serialized_shapes,
    }


# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------

def process_file(pynifly, path: Path, output: Path | None, *, indent: int, to_stdout: bool, include_geometry: bool) -> bool:
    try:
        nif = pynifly.NifFile(str(path))
        meta = {"file": str(path), "file_size": path.stat().st_size}
        meta.update(nif_to_dict(nif, include_geometry=include_geometry))
    except Exception as e:
        print(f"error: {path}: {e}", file=sys.stderr)
        return False

    payload = json.dumps(meta, indent=indent, ensure_ascii=False)
    if to_stdout:
        print(payload)
    else:
        out = output or path.with_suffix(path.suffix + ".json")
        out.write_text(payload, encoding="utf-8")
        print(f"wrote {out}")
    return True


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="Extract full NIF structure to JSON via PyNifly.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("input", type=Path, nargs="?", help="path to .nif file or directory")
    p.add_argument("-o", "--output", type=Path, help="output JSON path (single-file mode only)")
    p.add_argument("-r", "--recursive", action="store_true", help="recurse into directories")
    p.add_argument("-g", "--geometry", action="store_true", help="include full vert/tri/uv/normal/color/weight arrays")
    p.add_argument("--indent", type=int, default=2, help="JSON indent (default: 2)")
    p.add_argument("--stdout", action="store_true", help="print JSON to stdout instead of writing files")
    p.add_argument("--refresh-runtime", action="store_true", help="re-download the PyNifly runtime")
    args = p.parse_args(argv)

    ensure_runtime(force=args.refresh_runtime)
    if args.refresh_runtime and args.input is None:
        return 0

    if args.input is None:
        p.error("input path is required (unless using --refresh-runtime alone)")

    if not args.input.exists():
        print(f"error: {args.input} not found", file=sys.stderr)
        return 1

    pynifly = load_pynifly()

    if args.input.is_dir():
        if args.output:
            print("error: --output is not supported in directory mode", file=sys.stderr)
            return 1
        pattern = "**/*.nif" if args.recursive else "*.nif"
        files = sorted(args.input.glob(pattern))
        if not files:
            print(f"no .nif files found in {args.input}", file=sys.stderr)
            return 1
        ok = 0
        for f in files:
            if process_file(pynifly, f, None, indent=args.indent, to_stdout=args.stdout, include_geometry=args.geometry):
                ok += 1
        print(f"processed {ok}/{len(files)} files", file=sys.stderr)
        return 0 if ok == len(files) else 1

    return 0 if process_file(
        pynifly, args.input, args.output,
        indent=args.indent, to_stdout=args.stdout, include_geometry=args.geometry,
    ) else 1


if __name__ == "__main__":
    sys.exit(main())
