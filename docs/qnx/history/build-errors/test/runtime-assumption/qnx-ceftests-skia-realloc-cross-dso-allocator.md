# QNX ceftests Skia realloc cross-DSO allocator mismatch

- Date: 2026-06-28
- Signature: `SkEdgeBuilder dtor body: fEdgeList=...` followed by QNX allocator abort / `EXIT_CODE:134`
- Stage: test
- Category: runtime-assumption
- Scope: ceftests, Skia, PartitionAlloc allocator shim, libcef.so

## Symptoms

`AxViewportCollapseTest.CollapseDefault` and `BrowserSettingsTest.JavaScriptDisabled` aborted on QNX headless runs after Skia AAA path rendering. The visible failure looked like heap corruption during teardown of Skia edge data structures.

A narrow probe showed the abort occurred when freeing `SkTDArray` storage used by `SkEdgeBuilder::fList`:

```text
SkAnalyticEdgeBuilder dtor: fEdgeList=4800c4cc80 fList.size=52
SkEdgeBuilder dtor body: fEdgeList=4800c4cc80 fList.size=52
QNX PA fallback __free object=4800c4cc80
EXIT_CODE:134
```

## Root cause

Chromium's Skia memory override uses `base::UncheckedMalloc()` / `base::UncheckedFree()` for malloc/free style operations, but `sk_realloc_throw()` still called default-visibility `realloc()`.

On QNX `ceftests`, Skia code is present in both the main executable and `libcef.so`. A PLT call to `realloc()` from `libcef.so` can be interposed by the executable's allocator shim, allocating storage with the executable's PartitionAlloc state. Later `sk_free()` in `libcef.so` calls `base::UncheckedFree()` directly, using `libcef.so`'s PartitionAlloc state. The pointer is then classified as not managed by that image and forwarded to QNX libc `__free()`, causing the abort.

This was an allocator-domain mismatch, not Skia analytic edge list corruption.

## Fix pattern

For QNX, keep `sk_realloc_throw()` inside the current image's allocator shim by calling `allocator_shim::UncheckedRealloc()` directly. This matches `sk_free()` using the same image's `base::UncheckedFree()` / allocator chain.

## Applied change

Added QNX-specific handling in `skia/ext/SkMemory_new_handler.cpp`:

- include `partition_alloc/shim/allocator_shim.h` for QNX
- use `allocator_shim::UncheckedRealloc(addr, size)` in `sk_realloc_throw()` under `BUILDFLAG(IS_QNX)`

CEF-managed patch:

- `cef/patch/patches/qnx/chromium/skia_ext_memory_new_handler_qnx.patch`

## Verification

Rebuilt:

```bash
./out/qnx_release/ninja_qnx.sh ceftests
```

Confirmed `sk_realloc_throw()` now calls the current image's allocator shim directly:

```text
sk_realloc_throw(void*, unsigned long):
  callq allocator_shim::UncheckedRealloc(void*, unsigned long)
```

QNX headless tests now pass with real gtest exit codes:

```text
AX_EXIT_CODE:0
BROWSER_JS_EXIT_CODE:0
[  PASSED  ] 1 test.  # AxViewportCollapseTest.CollapseDefault
[  PASSED  ] 1 test.  # BrowserSettingsTest.JavaScriptDisabled
```

Command shape:

```bash
./cef/tools/qnx_run_test.sh --cmd 'cd /mnt/nfs/out/qnx_release && \
  ./ceftests --ozone-platform=headless --disable-gpu --disable-gpu-compositing \
    --gtest_filter="AxViewportCollapseTest.CollapseDefault" && \
  ./ceftests --ozone-platform=headless --disable-gpu --disable-gpu-compositing \
    --gtest_filter="BrowserSettingsTest.JavaScriptDisabled"'
```

## Files touched

- `skia/ext/SkMemory_new_handler.cpp`
- `cef/patch/patches/qnx/chromium/skia_ext_memory_new_handler_qnx.patch`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-axviewportcollapse-handoff-2026-06-27.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-native-window-skia-partitionalloc-crash.md`
