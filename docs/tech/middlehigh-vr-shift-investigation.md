# MiddleHighProcessData VR Layout Investigation

**Date:** 2026-04-25
**Outcome:** Commit `f5bddb9` — added 8-byte VR padding at offset **0x268** of `MiddleHighProcessData` (between `animationVariableCache` and `subGraphIdleManagerRoots`). Struct size grows from 0x4C0 (flat) to 0x4C8 (VR). Everything from `subGraphIdleManagerRoots` onward shifts by +8 in VR.

## The disagreement

Three sources said three different things about the bytes from offset 0x280–0x2A0 of `Actor::AIProcess::middleHigh`:

| Source | offset 0x280 | offset 0x288 | offset 0x290 |
|--------|-------------|-------------|-------------|
| **Original f4sevr SDK** (`f4sevr_0_6_21`) | (still in unk00 array) | `lock` (8B) | `EquipData* equipData` (8B pointer) |
| **Bundled f4sevr** (`src/f4sevr/Forms.h`) | (still in unk00 array) | `lock` (8B) | `EquipData* equipData` (8B pointer) |
| **CommonLibF4VR** (pre-fix) | `BSSpinLock equippedItemsLock` (8B) | `BSTArray<EquippedItem> equippedItems` (24B) | (continuation of BSTArray) |

Bundled f4sevr's static_assert (`offsetof(equipData) == 0x290`) was 8 bytes higher than the original SDK's (`0x288`), implying a VR-specific shift the original SDK author hadn't seen. But bundled f4sevr's *type* interpretation (single pointer to EquipData) disagreed with CommonLib's (BSTArray of EquippedItem). Couldn't resolve without disassembly.

## IDA workflow that resolved it

### Step 1 — find a small AIProcess accessor

Filtered the IDA Functions window (`Shift+F3`) on `EquippedItems`. First hit was a tiny ~5-line function:

```c
__int64 AIProcess::GetEquippedItems(__int64 a1, __int64 a2)  // @ 0x140CF1750
{
  /* ... lock + refcount stuff ... */
  result = FUN_140ecf0c0(a1);   // <-- helper that returns the array
  if (result)
    BSTArray<EquippedItem>::CopyTo<...>(result, a2);
  /* ... */
}
```

### Step 2 — F5 the inner getter

`FUN_140ecf0c0` is the bare offset accessor:

```c
__int64 FUN_140ecf0c0(__int64 a1)
{
  __int64 result = *(_QWORD *)(a1 + 8);   // a1 = AIProcess*; a1+8 = middleHigh
  if (result)
    result += 656;                         // 656 == 0x290
  return result;
}
```

The companion `FUN_140ecf0a0` (lock getter) does the same with `result += 648` (0x288).

So in VR: lock @ 0x288, BSTArray<EquippedItem> @ 0x290. CommonLib's *type* was right, its *offset* was off by 8.

### Step 3 — verify the shift cascades

Filtered Functions on `cloth`. Found `AIProcess::AccessClothExtraDataCache @ 0x140ED7DA0`. F5:

```c
v2 = *(_QWORD *)(a1 + 8);
if (!v2 || (v2 += 680) == 0)   // 680 == 0x2A8
  ...
```

CommonLib had `clothExtraDataCache @ 0x2A0`. IDA shows it at 0x2A8. The +8 shift cascades. Total struct size grows from 0x4C0 to 0x4C8.

### Step 4 — locate the precise insertion point via the constructor

The constructor (`FUN_140ed8370`, xref'd by `AIProcess::MoveToHigh` / `MoveToMiddleHigh`) reveals the exact insertion offset by its init order. The relevant fragment:

```c
*(_QWORD *)(a1 + 608) = 0;     // 0x260 (animationVariableCache)
*(_QWORD *)(a1 + 616) = 0;     // 0x268 ← VR-only mystery qword
FUN_141b93950(a1 + 624);       // 0x270 (subGraphIdleManagerRoots data+allocator)
FUN_141b93680(a1 + 640);       // 0x280 (subGraphIdleManagerRoots count)
FUN_141b93950(a1 + 656);       // 0x290 (equippedItems data+allocator)
```

Pattern: `FUN_141b93950(X)` initializes a BSTArray's data+allocator (16 bytes); `FUN_141b93680(X+16)` initializes its uint32 count. So the BSTArray at 0x270 is `subGraphIdleManagerRoots` (in VR; flat has it at 0x268). The single qword write at 0x268 is the VR-only mystery field.

## Final layout

| Offset (VR) | Field | Notes |
|-------------|-------|-------|
| 0x260 | `BSAnimationGraphVariableCache* animationVariableCache` | unchanged from flat |
| **0x268** | **`vrPostAnimVarCachePadding[8]`** | **VR-only mystery qword (POD; non-owning)** |
| 0x270 | `BSTArray<SubGraphIdleRootData> subGraphIdleManagerRoots` | shifted from flat 0x268 |
| 0x288 | `BSSpinLock equippedItemsLock` | shifted from flat 0x280 |
| 0x290 | `BSTArray<EquippedItem> equippedItems` | shifted from flat 0x288 |
| 0x2A8 | `BSTArray<BSClothExtraData*> clothExtraDataCache` | shifted from flat 0x2A0 |
| ... | (all subsequent fields shift by +8) | |
| 0x4C8 | end of struct | flat ends at 0x4C0 |

## What the 8 mystery bytes at 0x268 are

**Functionally dead in the shipping VR build.** Best to leave as opaque padding.

| Evidence | Implication |
|---|---|
| Constructor (`FUN_140ed8370`) inits via `*(_QWORD *)(a1 + 616) = 0` | Single 8-byte field, init to all-zero |
| Destructor (`~MiddleHighProcessData @ 0x140ED8D10`) doesn't touch offset 0x268 | Non-owning POD — no refcount, no `delete`, no atomic ops |
| Hex-Rays pseudo-code search for `+ 616` and `+ 0x268` across the whole binary returns only the ctor init line | No reader and no second writer at the C-level |
| Raw-disasm search for `+268h` returned ~910 hits, dominated by unrelated `[rsp+...]` stack-frame offsets and `+0x268` accesses on other structs; no AIProcess→middleHigh-shaped access surfaced in sampling | Likely no reader exists, but exhaustive proof would require tagging which struct each `[reg+0x268]` belongs to |

Plausible explanations for an inited-but-unread field:

- **Vestigial** — feature was developed during VR work, code path was cut before shipping, layout reservation kept for ABI compat.
- **Reserved** — placeholder for a feature meant to land in a later patch.
- **Read indirectly** — accessed via assembly the decompiler didn't surface (e.g., a function-pointer call, an inlined visitor, a memcpy target).

**Decision: declare as `std::byte vrPostAnimVarCachePadding[8]`.** Naming it any specific type would be guessing. The padding is layout-correct regardless of what the bytes mean.

## The fix (commit f5bddb9)

```cpp
BSAnimationGraphVariableCache* animationVariableCache;    // 260
#ifdef ENABLE_FALLOUT_VR
    std::byte vrPostAnimVarCachePadding[0x8];             // 268 (VR only)
#endif
BSTArray<SubGraphIdleRootData>  subGraphIdleManagerRoots; // flat: 268  vr: 270
BSSpinLock                      equippedItemsLock;        // flat: 280  vr: 288
BSTArray<EquippedItem>          equippedItems;            // flat: 288  vr: 290
// ... rest unchanged ...

#ifdef ENABLE_FALLOUT_VR
    static_assert(sizeof(MiddleHighProcessData) == 0x4C8);
    static_assert(offsetof(MiddleHighProcessData, subGraphIdleManagerRoots) == 0x270);
    static_assert(offsetof(MiddleHighProcessData, equippedItemsLock) == 0x288);
    static_assert(offsetof(MiddleHighProcessData, equippedItems) == 0x290);
    static_assert(offsetof(MiddleHighProcessData, clothExtraDataCache) == 0x2A8);
#else
    static_assert(sizeof(MiddleHighProcessData) == 0x4C0);
#endif
```

Same `#ifdef ENABLE_FALLOUT_VR` pattern as commit `760b8b5` (PlayerCharacter VR layout fix).

## Recipe for future similar investigations

When CommonLib and bundled f4sevr disagree on a struct's layout in a way that matters:

1. **Check if either is internally inconsistent.** Bundled f4sevr's MiddleProcess had its own field-offset comments contradicting its own `static_assert`s — that's a strong signal something was retrofitted post-hoc.

2. **Find a small game-function getter.** Filter the IDA Functions list (`Shift+F3`) on the field or struct name. Look for one-liners that just add a constant offset to a pointer. F5 (Hex-Rays) reads them in seconds.

3. **Verify with two or three different fields.** A single offset proof isn't enough to know whether the shift cascades or gets absorbed. Pick one field before the disputed area and one after.

4. **Read the constructor to pin down the precise insertion point.** Downstream offset asserts only constrain the byte range *between* asserts, not where the padding sits within that range. The constructor's init order makes the insertion point exact. F5 the constructor (often xref'd from `MoveToHigh`/`MoveToMiddleHigh`-style functions) and find the first qword-write at an unexpected offset.

5. **Don't try to identify the mystery bytes' *type*.** Treat as `std::byte` padding. The point of the fix is layout correctness; field naming is a separate, optional task.

6. **Gate the change on `#ifdef ENABLE_FALLOUT_VR`** so flat builds aren't broken. Update the sizeof assertion to be VR-conditional. Add VR-only offsetof asserts for every IDA-verified offset, **including the *first shifted field*** — that's what guarantees the padding is in the right slot, not just that the downstream layout happens to line up.

## What dead-ended (don't repeat these)

- **Searching binary for immediate `0x288` or `0x280`.** ~700 hits, dominated by unrelated `BSTLocklessQueue` template instantiations. Unworkable.
- **Searching for `lock cmpxchg [reg+0x280]`.** Same problem — lockless queue patterns dominate.
- **Looking up `MiddleHighProcessData` in IDA Local Types.** Not in the type database (the IDB only has function names, not full struct layouts).
- **F5'ing `ActorEquipManager::EquipObject`.** Touches lots of state but delegates to `Equip<...>` for the actual middleHigh access. The accessors don't appear in the outer function.
