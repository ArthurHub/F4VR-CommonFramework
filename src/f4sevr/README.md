# `f4sevr/` — legacy F4SE VR SDK Papyrus layer (deprecated)

Namespace: `F4SEVR`

A trimmed port of the Fallout 4 VR F4SE SDK's **Papyrus virtual machine** layer: `NativeFunctionN`
registration, `VMValue` / `VMArray` marshalling, and calls into Papyrus through raw RVAs. The framework
no longer uses it: Papyrus goes through CommonLibF4VR (see [`f4vr/` Papyrus](../f4vr/README.md#papyrus)).
It stays only for mods not yet moved off it, and will be removed once none use it. Don't use it in new
code.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Migrating to CommonLibF4VR

| `F4SEVR` | CommonLibF4VR |
|----------|---------------|
| `getGameVM()->m_virtualMachine` | `RE::GameVM::GetSingleton()->GetVM()` |
| `vm->RegisterFunction(new NativeFunctionN("Fn", "Script", fn, vm))` | `vm->BindNativeMethod("Script", "Fn", fn)` |
| `StaticFunctionTag*` first parameter | `std::monostate` |
| `BSFixedString` parameter | `RE::BSFixedString` or `std::string` |
| `VMArray<T>` parameter | `std::vector<T>` |
| `VMObject*` (a Papyrus `ScriptObject`) | `RE::BSTSmartPointer<RE::BSScript::Object>`; its name is `obj->GetTypeInfo()->GetName()`, its handle `obj->GetHandle()` |
| `execPapyrusGlobalFunction("Debug", "Notification", text)` | `f4vr::callPapyrusGlobalFunction("Debug", "Notification", text)` |
| `execPapyrusFunction(handle, script, "Fn", getArgs(a, b))` | `vm->DispatchMethodCall(handle, script, "Fn", nullptr, a, b)` |
| `VMArray<VMVariable>` / `getArgs` / `addArgument` | the arguments themselves, passed to the call; a list built at runtime, packed into a `BSScrapArray<Variable>` for `DispatchStaticCallPacked` / `DispatchMethodCallPacked` |
| `GetObjectIdentifier(handle, script, 1, &id, 0)` | `FindBoundObject(handle, script, true, object, false)`, the handle from `GetObjectHandlePolicy().GetHandleForObject(type, form)` |
| `GetVMPropertyInfo(...).index`, `GetPropertyValueByIndex` | `ObjectTypeInfo::GetPropertyIndex(name)` (through `GetParent()`), `GetVariableValue(object, index, value)` |
| `SetPropertyValue(&id, name, &value, &unk)` | `SetPropertyValue(object, name, value, nullptr)` |
| `PapyrusGatewayBase::executePapyrusScript("Fn", arguments)` | `executePapyrusScript("Fn", a, b)` |

Calls into Papyrus need the CommonLibF4VR that passes Fallout 4 VR its own argument callable
(`0db6c81` or later): before it, `DispatchStaticCall` / `DispatchMethodCall` crashed on VR.

## Files

| File | Contents |
|------|----------|
| [`PapyrusNativeFunctions.h`](PapyrusNativeFunctions.h) | `NativeFunctionN` templates that wrap a C++ function as a Papyrus native. |
| [`PapyrusVM.h`](PapyrusVM.h) | Virtual machine access (`getGameVM`, `VirtualMachine`, identifiers). |
| [`PapyrusValue.h`](PapyrusValue.h) / [`PapyrusArgs.h`](PapyrusArgs.h) | `VMValue` / `VMVariable` / `VMArray` value types and argument packing. |
| [`PapyrusStruct.h`](PapyrusStruct.h) | Papyrus struct marshalling. |
| [`PapyrusInterfaces.h`](PapyrusInterfaces.h) | F4SE Papyrus interface plumbing. |
| [`PapyrusUtils.h`](PapyrusUtils.h) | Calls into Papyrus through the RVAs of `CallGlobalFunctionNoWait` / `CallFunctionNoWait` / `SendCustomEvent`, and `getArgs(...)`. |
| [`Common.h`](Common.h) | SDK macros (`CALL_MEMBER_FN`, `DEFINE_MEMBER_FN`, static-heap operators), and `StringCache`, which [`BSFlattenedBoneTree.h`](../f4vr/BSFlattenedBoneTree.h) still uses. |

Offsets here are Fallout 4 VR 1.2.72 RVAs, from the f4sevr 0.6.21 headers.
