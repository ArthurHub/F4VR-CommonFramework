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
| [`PerfMonitor.h`](PerfMonitor.h) | `PerfMonitor` — low-overhead runtime profiler for a hot-path function/scope. Collects count/avg/p95/p99/min/max/busy% and logs a summary every N seconds (default 30, configurable per instance). Gated on debug logging: zero work when debug is off. Single-threaded; use the RAII `scope()` timer. |

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

// Profile a hot function: one static monitor + an RAII scope timer.
// Collects/logs only when the logger is at debug level (iLogLevel <= 1); otherwise scope() is a no-op.
// Logs e.g. "[Perf] onFrameUpdate: n=897 (90/s) avg=0.412ms p95=0.900ms p99=1.800ms min=0.180ms max=3.900ms busy=3.7% over 10.0s"
void onFrameUpdate()
{
    static common::PerfMonitor perf("onFrameUpdate");       // default 30s window
    static common::PerfMonitor perfFast("onFrameUpdate", 5s); // per-instance interval
    const auto timer = perf.scope();
    // ... work being measured ...
}
```

## Notes

- `Quaternion` and `MatrixUtils` operate on CommonLibF4VR's `RE::NiPoint3` / `RE::NiMatrix3` /
  `RE::NiTransform`, so include `RE/Fallout.h` (already in the [PCH](../PCH.h)) before use.
- Angle helpers come in radians and `...Degrees` variants — pick the matching pair to avoid
  unit bugs. INI transforms (`ConfigBase`) store rotations in **degrees**.
- `slerp` math is adapted from VRIK (credit: prog).
