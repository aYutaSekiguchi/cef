# QNX ceftests handoff: AxViewportCollapseTest.CollapseDefault

Date: 2026-06-27

## Goal
Continue isolating the QNX `ceftests` failure for `AxViewportCollapseTest.CollapseDefault`.

## Current conclusion
Resolved on 2026-06-28: the failure was an allocator-domain mismatch, not Skia analytic edge arena corruption.

Skia's `sk_realloc_throw()` in `libcef.so` called default-visibility `realloc()`, which could be interposed by the main `ceftests` executable's allocator shim. The resulting `SkTDArray` storage was later freed by `libcef.so`'s `sk_free()` / `base::UncheckedFree()`, using a different PartitionAlloc state. The QNX fallback then classified the pointer as non-PartitionAlloc and forwarded it to libc `__free()`, causing the abort.

The durable fix is `cef/patch/patches/qnx/chromium/skia_ext_memory_new_handler_qnx.patch`, which makes QNX `sk_realloc_throw()` call `allocator_shim::UncheckedRealloc()` in the current image.

## Verified behavior
- `AxViewportCollapseTest.CollapseDefault` still ends with `exit 134` on QNX.
- `ApiVersionTest.*:AxViewportCollapseTest.CollapseDefault` also ends with `exit 134`.
- `BrowserSettingsTest.JavaScriptDisabled` shows the same family of crash behavior.
- The failure is now consistently observed in the Skia AAA path, not in browser startup.

## Most recent stack / trace observations
The current instrumented logs show:
- `AAAFillPath enter`
- `AAAFillPath using MaskAdditiveBlitter`
- `aaa_fill_path before buildEdges`
- `aaa_fill_path after buildEdges: count=52`
- `aaa_fill_path before sort_edges`
- `aaa_fill_path after sort_edges`
- `aaa_fill_path before aaa_walk_edges`
- `aaa_fill_path rle branch done`
- `SkAnalyticEdgeBuilder dtor`
- `SkEdgeBuilder dtor body`
- `SkArenaAlloc dtor`

The crash happens during `SkArenaAlloc` teardown while walking allocator blocks and freeing the analytic edge arena.

## Root cause found
The decisive probe was:

```text
SkAnalyticEdgeBuilder dtor: fEdgeList=4800c4cc80 fList.size=52
SkEdgeBuilder dtor body: fEdgeList=4800c4cc80 fList.size=52
QNX PA fallback __free object=4800c4cc80
```

`fEdgeList` was `SkTDArray` storage allocated via `sk_realloc_throw()`. Because `realloc()` crossed from `libcef.so` into the executable's allocator shim, the later `base::UncheckedFree()` in `libcef.so` could not free it as a local PartitionAlloc pointer.

## Important logs / addresses
Most recent useful log shape:
- `buildEdges count=52`
- `sort_edges edge=64000ed7c8 last=640029c0d0`
- `SkArenaAlloc dtor: this=... dtorCursor=640029c011 cursor=640029c958 end=640029ca00`
- `NextBlock` chain eventually reaches the first block in the arena and then exits the allocator path

## Instrumentation cleanup
The temporary Skia debug prints were removed after confirming the root cause. The QNX allocator fallback print was also removed; only the durable `sk_realloc_throw()` fix remains.

## Verification after fix
- `./out/qnx_release/ninja_qnx.sh ceftests` succeeded.
- Disassembly confirms `sk_realloc_throw()` now calls `allocator_shim::UncheckedRealloc()` directly.
- QNX headless runs now pass with real gtest exit codes:
  - `AxViewportCollapseTest.CollapseDefault`: `EXIT_CODE:0`
  - `BrowserSettingsTest.JavaScriptDisabled`: `EXIT_CODE:0`

See the structured resolved note:

- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-skia-realloc-cross-dso-allocator.md`

## Resume command hints
- Re-run the single test:
  - `./ceftests --ozone-platform=headless --disable-gpu --disable-gpu-compositing --gtest_filter="AxViewportCollapseTest.CollapseDefault"`
- The latest useful failure mode is the post-test allocator abort in `SkArenaAlloc`.
- If using the live trace, the most recent log file was `out/qnx_release/ax_trace9.log` / later runs with similar output.
