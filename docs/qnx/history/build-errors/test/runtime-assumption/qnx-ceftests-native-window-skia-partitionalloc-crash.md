# QNX ceftests native-window SIGSEGV in `PartitionAlloc::Free` (LLVM codegen bug)

## Failure signature

Stage: test  
Category: runtime-assumption  
Scope: `ceftests`, all native-window (non-OSR) browser tests on QNX x86_64

Every native-window browser test (e.g. `BrowserSettingsTest.JavaScriptDisabled`,
`FrameTest.SingleNav`, `AxViewportCollapseTest.CollapseDefault`) crashes with
SIGSEGV (exit 139) during the browser compositor paint cycle:

```text
[ RUN      ] BrowserSettingsTest.JavaScriptDisabled
__PI_QNX_EXIT__:139
```

Core/backtrace (from host cross-gdb):

```text
#0  FromFirstSuperPage (extent_entry=...)
    at ../../base/allocator/partition_allocator/src/partition_alloc/
    partition_page.h:2077
#1  FreeInlineInUnknownRoot (slot_span=..., slot_start=...)
    at ../../base/allocator/partition_allocator/src/partition_alloc/
    partition_alloc-inl.h:248
#2  PartitionAllocFunctionsInternal::Free (...)
#3  kPartitionAllocDispatch.free_function
#4  ShimFree (...) at shim_alloc_functions.h
#5  free (__ptr=...) at allocator_shim_override_libc_symbols.h
#6  sk_free (...) at SkMemory_malloc.cpp:50
#7  ...
#8  SingleThreadProxy::BeginMainFrame (...) at single_thread_proxy.cc:1147
```

The crash reliably occurs during:

1. **Image compositing**: `LayerTreeHost::PaintContent` → `View::PaintFromPaintRoot`
   → `ImageView::OnPaint` → `ImageSkia::GetRepresentation` → **sync `PaintRecord`
   playback**
2. **Skia AA path fill**: `SkCanvas::drawPath` → `SkScan_AAAPath::aaa_fill_path`
   → `~SkEdgeBuilder` → `~SkTDArray` → `sk_free` → `free`
3. **PartitionAlloc**: `FreeInlineInUnknownRoot` → `FromFirstSuperPage`
   → **SIGSEGV** reading `extent_entry->root` (all zeros)

## Root cause

### Immediate cause

`FromFirstSuperPage()` reads a zeroed-out extent entry from the super page
data area instead of the metadata region. This happens because the metadata
offset (`PartitionAddressSpace::offsets_to_metadata_[pool_handle]`) is read
as **zero at runtime**, causing the metadata lookup to fall back to the super
page's data area.

### Why the offset reads as zero

The `offsets_to_metadata_[]` array is a global variable placed in the BSS
segment. On QNX, ASLR loads the binary at a virtual address above 4 GB
(e.g. `0x450f8a0660`, ≈ 29.7 GB). The array's runtime address becomes
`0x450f8a0660` which exceeds the 32-bit addressable range.

LLVM's code generator selects a 32-bit LEA instruction (`lea %edx` instead
of `lea %rdx`) when computing the address of `offsets_to_metadata_[]`, because
without explicit guidance it assumes the address fits in 32 bits (the default
code model `-mcmodel=small`). The `%edx` destination truncates the upper 32
bits, producing `0x000000000f8a0660` — an unmapped address.

Reading from that unmapped address returns zero, so every call reads
`metadata_offset = 0`. PartitionAlloc then looks up metadata at the super
page base address instead of the metadata region, finds all-zero extent
entries, and faults on `extent_entry->root`.

```asm
; BAD (default -mcmodel=small, typical LLVM x86_64 output)
lea    0x8eb5e40(%rip),%edx      ; %edx = lower 32 bits → TRUNCATION
mov    (%rdx),%rax               ; reads from 0x0f8a0660 → zero

; GOOD (with -mcmodel=medium)
lea    0x8eb5e40(%rip),%rdx      ; %rdx = full 64-bit address
mov    (%rdx),%rax               ; reads correct metadata offset
```

### Why this is a codegen bug

- The code model `-mcmodel=small` (the Clang/LLVM default for x86_64) **should
  not** permit 32-bit register selection for global variable access when the
  variable is in a relocatable PIC/PIE binary. PIE code is position-independent
  and uses RIP-relative addressing, which is inherently 64-bit.
- The QNX toolchain's bundled LLVM (Chromium's `third_party/llvm-build/`,
  version `llvmorg-23-init-5669`) has a subtle register-allocation decision
  where the `lea` output register is chosen as `%edx` instead of `%rdx`,
  despite RA-visible saves/restores of the full 64-bit `%rdx` elsewhere.
- This is reproducible: every build without `-mcmodel=medium` crashes
  identically.
- The upstream report `qnx-ports/llvm-project` (based on LLVM 21.1.3) does
  **not** carry a fix for this x86_64 lea codegen behaviour.

### Relevant PartitionAlloc configuration

QNX uses `MOVE_METADATA_OUT_OF_GIGACAGE` (inherited from the 64-bit
non-Android default), which stores extent entries and slot-span metadata in a
**separate metadata region** outside the GigaCage. The region is located at
`super_page + offsets_to_metadata_[pool_handle]`. When the offset reads as
zero, the metadata region overlaps the start of the super page data area,
all extent entries read as zero, and `FromFirstSuperPage` faults.

Other QNX-specific PartitionAlloc differences (not directly related):
- `PA_CONFIG_THREAD_CACHE_SUPPORTED() = 0`
- `DecommittedMemoryIsAlwaysZeroed() = false`
- `PA_CONFIG_HAS_LINUX_KERNEL()` = false
- PKEY isolation not available

### Interception chain

```
sk_free(SkTDArray data)
  → free(ptr)                        [allocator_shim_override_libc_symbols.h]
    → ShimFree(ptr, ...)             [shim_alloc_functions.h]
      → chain_head->free_function()  [allocator_shim.cc]
        → DelegatedFreeFn()
          → kPartitionAllocDispatch.free_function
            = PartitionAllocFunctionsInternal::Free()
            → FreeInlineInUnknownRoot()
              → FromFirstSuperPage()
                → SIGSEGV            [extent_entry->root is all zeros]
```

Apple / CastAndroid systems include an `IsManagedByPartitionAlloc()` check that
forwards non-PA allocations to the system `free()`. QNX/Linux path **lacks
this check** (`MightNeedToHandleSystemDeallocation()` is constexpr false).

## Fix

Add `-mcmodel=medium` to the QNX toolchain compiler flags in
`build/toolchain/qnx/BUILD.gn`:

```python
extra_cflags = "${_target_flags} ... -mcmodel=medium"
extra_cppflags = "${_target_flags} ... -mcmodel=medium"
extra_cxxflags = "${_target_flags} ... -mcmodel=medium"
```

The source file is in the CEF new_files directory:
`cef/patch/qnx/chromium/new_files/build/toolchain/qnx/BUILD.gn`

`-mcmodel=medium` tells the compiler that the text and data segments may
exceed 4 GB independently, which forces LLVM to always generate 64-bit LEA
instructions (`lea` with `%r**` destination registers).

### Alternative approaches considered

| Approach | Reason rejected |
|----------|-----------------|
| Upstream qnx-ports/llvm-project fix | LLVM version mismatch (21.1.3 vs Chromium's 23.0.0git); QNX patches focus on libcxx/target, not x86_64 lea codegen |
| `-mcmodel=large` | Overkill — also disables RIP-relative addressing for calls, causing performance regression |
| Disable ASLR on QNX | Not practical for CI; individual test runs could still hit the high-load address |
| Modify PartitionAlloc to avoid external metadata | Extensive and risky change for the QNX-only case |

## Files changed

```
cef/patch/qnx/chromium/new_files/build/toolchain/qnx/BUILD.gn  (+ -mcmodel=medium)
cef/patch/patches/qnx/chromium/ui_views_BUILD_stubs_is_qnx.patch  (fix: single hunk)
cef/patch/patches/qnx/chromium/chrome_browser_ui_views_stubs_is_qnx.patch  (fix: hunk header)
cef/tools/qnx_tests/modules/ceftests.py  (+ headless per_test_args)
```

## Verification

### Static analysis (binary disassembly)

The current `ceftests` binary still shows the `-mcmodel=medium` effect: all 16
references to `PartitionAddressSpace::offsets_to_metadata_[]` use 64-bit
registers (`%rsi`, `%rcx`, `%rdx`, `%rbp`, `%rdi`). No `lea %edx`
truncation is present.

```asm
; example — confirmed in ceftests ELF binary
822d06: 48 8d 15 53 45 14 00    lea    0x144553(%rip),%rdx   # %rdx ✅
834026: 48 8d 3d 33 32 13 00    lea    0x133233(%rip),%rdi   # %rdi ✅
82f660: 48 8d 2d f9 7b 13 00    lea    0x137bf9(%rip),%rbp   # %rbp ✅
```

### Runtime (QNX QEMU)

The fix removed the earlier browser-startup crashes for narrow tests such as
`BrowserSettingsTest.JavaScriptDisabled` and `FrameTest.SingleNav`, but the
`AxViewportCollapseTest.CollapseDefault` path still reproduces a SIGSEGV in the
browser paint stack:

```text
#0  FromFirstSuperPage()
#4  Free()
#5  ~SkTDArray()
#6  ~SkEdgeBuilder()
#7  aaa_fill_path()
#22 GetBitmap()
#27 GetPaintImage()
#29 OnPaint()
#55 PaintFromPaintRoot()
#62 DoPainting()
#63 BeginMainFrame()
```

The crash now happens while painting browser chrome (`ImageView::OnPaint` →
Skia AA path fill), before the accessibility callback completes. That means the
remaining blocker is no longer the browser-widget startup path; it is a
browser-paint allocator/free ownership failure that still lands in
`PartitionAlloc::FreeInlineInUnknownRoot()` on QNX.

| Test | Result |
|------|--------|
| `AxViewportCollapseTest.CollapseDefault` | exit 139 (SIGSEGV) |
| `BrowserSettingsTest.JavaScriptDisabled` | PASS (exit 0) |
| `FrameTest.SingleNav` | PASS (exit 0) |
| `NavigationTest.LoadSameOriginLoadURL` | PASS (exit 0) |
| `VersionTest.*` | PASS |
| `OSRTest.Paint` | PASS |
| `OSRTest.AccessibilityEnable/Disable` | PASS |

## Remaining issues

- **OSR focus/cursor/mouse timing tests** (`OSRTest.TakeFocus`, `OSRTest.Cursor`,
  `OSRTest.MouseMove`, and their `2x` variants) time out after 10000 ms under
  QEMU, with `cc/tiles/tile_manager.cc` tile memory warnings. These are
  **pre-existing QEMU timing issues**, not related to the PartitionAlloc codegen
  crash. Root cause: the emulated GPU-less environment has limited tile memory,
  causing compositor stalls that delay the synthetic input event delivery.
- The `webrtc/rtc_base/cpu_info.cc:73` "No function to get number of cores"
  ERROR is harmless and expected on QNX.
- The `base/files/file_path_watcher_inotify.cc:929` inotify warning is harmless
  under QEMU.
