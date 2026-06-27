# QNX ceftests browser-paint SIGSEGV from system allocation/free mismatch

## Failure signature

Stage: test  
Category: runtime-assumption  
Scope: `ceftests`, `AxViewportCollapseTest` browser-paint path on QNX x86_64

After the earlier browser-startup fixes, the `AxViewportCollapseTest` suite still
reproduces a hard crash in the browser paint stack. The narrow repro is:

```bash
./tools/qnx_run_test.sh --ceftests 'AxViewportCollapseTest.CollapseDefault'
```

Observed failure before the fix:

```text
[ RUN      ] AxViewportCollapseTest.CollapseDefault
__PI_QNX_EXIT__:139
```

Host cross-`gdb` backtrace:

```text
#0  FromFirstSuperPage()
#1  FreeInlineInUnknownRoot()
#2  PartitionAllocFunctionsInternal::Free()
#3  kPartitionAllocDispatch.free_function
#4  ShimFree()
#5  free()
#6  sk_free()
#7  ~SkTDArray()
#8  ~SkEdgeBuilder()
#9  aaa_fill_path()
#22 GetBitmap()
#27 GetPaintImage()
#29 OnPaint()
#55 PaintFromPaintRoot()
#62 DoPainting()
#63 BeginMainFrame()
```

The crash happens while painting browser chrome (Skia AA path fill inside
`ImageView::OnPaint`) and **not** in the browser-widget startup path.

## Root cause

The pointer passed into `free()` is **not** a PartitionAlloc-managed pointer.
In the crash core, the freed buffer address is:

```text
rdi = 0x5800bb3980
```

PartitionAlloc's QNX pool setup in the same core shows:

```text
regular_pool_base_address_ = 0x6800000000
brp_pool_base_address_     = 0x6c00000000
```

So the pointer being freed is outside every PA-managed pool.

That means the buffer is a **system allocation** (Skia's `SkTDArray` buffer,
allocated through `sk_malloc_flags()` / `malloc()`), but the QNX allocator shim
was treating every `free()` as PartitionAlloc-managed and immediately routing it
into `PartitionRoot::FreeInlineInUnknownRoot()`. QNX did **not** have the
system-deallocation escape hatch that Apple/CastAndroid use.

So the crash is:

```text
system malloc() allocation
  → sk_free()
  → shimmed free()
  → PartitionAlloc::FreeInlineInUnknownRoot()
  → FromFirstSuperPage()
  → SIGSEGV
```

## Fix

Teach the QNX allocator shim to forward non-PartitionAlloc pointers to the
underlying libc deallocator (`__free()`):

- add QNX to `MightNeedToHandleSystemDeallocation()`
- add a QNX branch in `MaybeHandleSystemDeallocation()`
- declare and call libc `__free()` for non-PA pointers

Managed patch:

```text
base/allocator/partition_allocator/src/partition_alloc/shim/
  allocator_shim_default_dispatch_to_partition_alloc.cc
```

## Verification

Rebuilt:

```bash
./out/qnx_release/ninja_qnx.sh ceftests
```

Then reran the narrow crash repro:

```bash
./tools/qnx_run_test.sh --cmd "cd /mnt/nfs/out/qnx_release && ./ceftests --ozone-platform=headless --disable-gpu --disable-gpu-compositing --gtest_filter='AxViewportCollapseTest.CollapseDefault'"
```

Result after the fix:

```text
__PI_QNX_EXIT__:0
```

The `FromFirstSuperPage()` SIGSEGV is gone for this repro.

## Notes

- This crash is separate from the earlier QNX `-mcmodel=medium` allocator
  metadata codegen bug.
- Some `AxViewportCollapseTest` variants may still time out waiting for AX-tree
  completion under QEMU; that is a separate test-environment issue, not the
  allocator crash documented here.
