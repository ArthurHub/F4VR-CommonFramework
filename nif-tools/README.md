# nif-tools

Python utilities for working with Fallout 4 VR NIF meshes.

## `nif_to_json.py` — NIF structure dump

```
python nif_to_json.py path/to/mesh.nif            # -> mesh.nif.json
python nif_to_json.py path/to/mesh.nif --stdout --geometry   # include vert/tri/uv arrays
python nif_to_json.py path/to/dir -r              # batch a directory
```

Fetches the [PyNifly](https://github.com/BadDogSkyrim/PyNifly) runtime into `.pynifly/`
on first run (GPL-3.0, git-ignored, not vendored). Re-run with `--refresh-runtime` to
update it.
