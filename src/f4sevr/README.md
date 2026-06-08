# `f4sevr/` — F4SE VR SDK & Papyrus interop

Namespace: `F4SEVR`

A trimmed port of the Fallout 4 VR F4SE SDK focused on the **Papyrus virtual machine**: registering
native C++ functions so Papyrus scripts can call them, and marshalling values/arguments across the
boundary. Unlike the rest of the framework, this code lives under `namespace F4SEVR` (not
`f4cf::f4sevr`) because it mirrors the upstream SDK types.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Files

| File | Contents |
|------|----------|
| [`PapyrusNativeFunctions.h`](PapyrusNativeFunctions.h) | `NativeFunctionN` templates that wrap a C++ function as a Papyrus native (the `RegisterFunction` machinery). |
| [`PapyrusVM.h`](PapyrusVM.h) | Virtual machine access (`getGameVM`, `VirtualMachine`, identifiers). |
| [`PapyrusValue.h`](PapyrusValue.h) / [`PapyrusArgs.h`](PapyrusArgs.h) | `VMValue` / `VMVariable` / `VMArray` value types and argument packing (`getArgs`, `addArgument`). |
| [`PapyrusStruct.h`](PapyrusStruct.h) | Papyrus struct marshalling. |
| [`PapyrusInterfaces.h`](PapyrusInterfaces.h) | F4SE Papyrus interface plumbing. |
| [`Common.h`](Common.h) | SDK macros (`CALL_MEMBER_FN`, `DEFINE_MEMBER_FN`, static-heap operators) shared by the above. |
| [`PapyrusUtils.h`](PapyrusUtils.h) | Helpers to *call* Papyrus from C++: `SendCustomEvent`, `CallGlobalFunctionNoWait`, `CallFunctionNoWait`, plus `getArgs(...)` builders. |

## Registering a native function

Native functions are registered during the F4SE load phase. The framework gives you a hook for it —
register from your mod's `onModLoaded` (or via
[`f4cf::f4vr::registerPapyrusNativeFunctions`](../f4vr/F4VRUtils.h)):

```cpp
#include "f4sevr/PapyrusNativeFunctions.h"
#include "f4sevr/PapyrusVM.h"

bool registerPapyrus(F4SEVR::VirtualMachine* vm)
{
    vm->RegisterFunction(
        new F4SEVR::NativeFunction0<F4SEVR::StaticFunctionTag, float>(
            "GetSomething", "MyModScript", [](F4SEVR::StaticFunctionTag*) -> float {
                return 42.0f;
            }, vm));
    return true;
}
```

## Calling Papyrus from C++

For the inverse direction — invoking Papyrus from native code — prefer
[`f4cf::f4vr::PapyrusGatewayBase`](../f4vr/PapyrusGatewayBase.h), which wraps the registration
handshake and the `CallFunctionNoWait` plumbing from [`PapyrusUtils.h`](PapyrusUtils.h) into a
reusable base class.

## Notes

- Offsets here (e.g. `CallGlobalFunctionNoWait_Internal`) are Fallout 4 VR 1.2.72 RVAs. Authority:
  the f4sevr 0.6.21 headers documented as `f4sevr_0_6_21_RE_REFERENCE.md` in the modding reference
  library.
- This is interop code mirroring an external SDK — match the existing `F4SEVR` naming and struct
  layouts exactly when extending it; offsets and vtable order are load-bearing.
