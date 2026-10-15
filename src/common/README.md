# `common/` — math & shared utilities

Namespace: `f4cf::common`

Engine-light helpers that don't depend on the rest of the framework: 3D math, string/file/resource
utilities, and time helpers. Everything here is usable from any other subsystem.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`Quaternion.h`](Quaternion.h) | `Quaternion` class — normalize, conjugate, slerp, angle-axis, vector-to-vector, matrix conversion, and operators. Header-only. |
| [`MatrixUtils.h`](MatrixUtils.h) | `MatrixUtils` — vector math (`vec3Norm`/`vec3Dot`/`vec3Cross`), Euler↔matrix conversion, `NiTransform` builders, relocation/delta/target transforms. |
| [`CommonUtils.h`](CommonUtils.h) | Free functions: float compare, string trim/lower, embedded-resource extraction, filesystem helpers, time (`nowMillis`/`nowNanosec`), and the `Documents\My Games` path resolver. |
| [`TomlEditor.h`](TomlEditor.h) | `TomlEditor` — sets values in the text of a TOML file and keeps every other byte: comments, blank lines, key order, how the other values are written. Plain std and toml++, unit tested. |

## Quick reference

```cpp
#include "common/CommonUtils.h"
#include "common/MatrixUtils.h"
#include "common/Quaternion.h"

using namespace f4cf::common;

// Float-safe comparison (avoid == on floats)
if (fEqual(a, b)) { /* ... */ }

// Build a transform from position + Euler degrees
RE::NiTransform t = MatrixUtils::getTransform(x, y, z, heading, roll, attitude, /*scale*/ 1.0f);

// Smoothly blend two rotations
Quaternion q; q.fromMatrix(node->world.rotate);
q.slerp(0.25f, targetQ);              // 25% toward target
node->local.rotate = q.getMatrix();

// Resolve a path under the user's Documents folder
const auto cfgDir = getRelativePathInDocuments(R"(\My Games\Fallout4VR\Mods_Config)");
```

## Editing a TOML file

toml++ reads TOML, but its writer drops comments and blank lines and sorts the keys, so a file a person
edits by hand can't be written back with it. `TomlEditor` writes by replacing the text of one value where
toml++ read it, and leaves the rest of the text alone:

```cpp
#include "common/TomlEditor.h"

common::TomlEditor editor(fileText, "MyMod.toml");
if (!editor.ok()) {
    logger::warn("Not TOML: {}", editor.error());   // what is wrong, at which line and column
}
editor.set({ "Main", "fScale" }, 1.25f);             // the old value's text replaced, nothing else
editor.set({ "Main", "sButton" }, "primary tap a");
editor.set({ "Main", "colors" }, toml::array{ 255, 128, 0 });
if (!editor.set({ "New", "key" }, true)) {           // a missing section is added at the end
    logger::warn("Not saved: {}", editor.error());
}

// several values: the same result as a set of each, in about the time of one
editor.set({ { { "Main", "fScale" }, 1.25f }, { { "Main", "bFlag" }, true } });
writeFile(path, editor.text());
```

- **Cost:** a set parses the whole text back, about 0.4 ms for a 370-line file. Set several values
  with one `set(edits)`: the existing ones are replaced in one pass and parsed back once (31 values
  in 0.43 ms, where a set of each is 6.7 ms). The parse and both sets are perf sites.
- **Errors** are for a log: a parse error says what is wrong, the line and column, and quotes the
  line (`... at line 12, column 15 of 'MyMod.toml': sButton = offhand tap grip`). The editor logs
  nothing itself; the caller logs `error()`.

- A **missing key** goes on a line after the last value of its section (as a dotted key in a table of
  dotted keys, before the closing brace in an inline table); a **missing section** goes at the end with
  a `[header]`.
- A value the file already holds is left as it is written (`71.000` for 71.0f, `0x10` for 16), so a
  save of unchanged values changes nothing, and the caller can skip writing the file.
- A float is written from its own shortest text (`8.7`, not `8.6999998`), a whole one with `.0` so it
  stays a float. A replaced literal string (`'C:\path'`) stays literal when the new string can be one.
- A value that is rewritten takes this formatting: a multi-line string becomes one line, `0x1F`
  becomes `31`.
- Every edit is parsed back and kept only when the document holds the new value and nothing else
  changed. Otherwise `set` returns false, the text stays, and `error()` says why: a path that is a
  section or goes through a value, an array of tables (`[[name]]`), a value TOML can't hold.

## Notes

- `Quaternion` and `MatrixUtils` operate on CommonLibF4VR's `RE::NiPoint3` / `RE::NiMatrix3` /
  `RE::NiTransform`, so include `RE/Fallout.h` (already in the [PCH](../PCH.h)) before use.
- Angle helpers come in radians and `...Degrees` variants — pick the matching pair to avoid
  unit bugs. INI transforms (`ConfigBase`) store rotations in **degrees**.
- `slerp` math is adapted from VRIK (credit: prog).
