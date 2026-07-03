# Phase 5 QNX Mojo Interface Fix — 2026-07-03

## Review blockers addressed

The Phase 5 Mojo interface worker (2026-07-03) was rejected by review with three
actionable findings. This substep fixes all three and re-validates the build.

### Blocker fixed: `gfx.mojom.AcceleratedWidget` in `qnx_gpu.mojom`

**File:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`

**Problem:** The schema used raw `uint32 widget` in five places instead of the
required `gfx.mojom.AcceleratedWidget`. The Phase 2 design note explicitly
imports `ui/gfx/mojom/accelerated_widget.mojom` and uses
`gfx.mojom.AcceleratedWidget` for every widget parameter.

**Fix — diff:**

```diff
 module ui.ozone.qnx.mojom;

 // import shortcuts
 import "ui/gfx/geometry/mojom/geometry.mojom";
+import "ui/gfx/mojom/accelerated_widget.mojom";
```

```diff
 struct QnxDmaBufFrame {
-  uint32 widget;
+  gfx.mojom.AcceleratedWidget widget;
```

```diff
 interface QnxGpuHost {
   SubmitFrame(QnxDmaBufFrame frame) => (bool accepted, string diagnostic);
-  ReportProducerLost(uint32 widget, uint32 generation);
+  ReportProducerLost(gfx.mojom.AcceleratedWidget widget, uint32 generation);
 };

 interface QnxGpuControl {
-  AttachWidget(uint32 widget, uint32 generation, gfx.mojom.Size size);
+  AttachWidget(gfx.mojom.AcceleratedWidget widget,
+               uint32 generation,
+               gfx.mojom.Size size);
-  ResizeWidget(uint32 widget, uint32 generation, gfx.mojom.Size size);
+  ResizeWidget(gfx.mojom.AcceleratedWidget widget,
+               uint32 generation,
+               gfx.mojom.Size size);
-  DetachWidget(uint32 widget, uint32 generation);
+  DetachWidget(gfx.mojom.AcceleratedWidget widget, uint32 generation);
 };
```

### High fix: duplicate mojom targets consolidated

**Files:**
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn`

**Problem:** Both BUILD files defined a mojom target over the same `qnx_gpu.mojom`
source. The parent target was `:qnx_gpu_mojom` (local label); the subdir target
was `//ui/ozone/platform/qnx/mojom:mojom` (full path). The QNX source set
depended only on the parent target, leaving the subdir target unused. If both
targets were later pulled into the GN graph they would generate duplicate
`gen/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom.*` outputs.

**Pattern:** Existing Ozone platform (DRM) uses one mojom target in the `mojom/`
subdir, and the parent BUILD.gn depends on the subdir via its GN label.

**Fix — parent BUILD.gn before:**

```gn
deps = [
  ...
  ":qnx_gpu_mojom",          # local label — duplicate
]

# (and at bottom of file:)
mojom("qnx_gpu_mojom") {
  sources = [ "mojom/qnx_gpu.mojom" ]
  public_deps = [ ... ]
}
```

**Fix — parent BUILD.gn after:**

```gn
deps = [
  ...
  "//ui/ozone/platform/qnx/mojom",   # full path to subdir target
]
```

**Subdir `mojom/BUILD.gn` is unchanged** — it already defined the correct
`mojom("mojom")` target that generates from `qnx_gpu.mojom`. The target label
from the root becomes `//ui/ozone/platform/qnx/mojom:mojom`.

### Medium fix: validation commands corrected

The previous report used `../out/qnx_release/args.gn` (wrong path, one level up
from CEF) and `ls gen/ui/ozone/platform/qnx/mojom/` without an output directory
prefix. Both are corrected in this report's validation section below.

---

## Exact changes

### `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`
- Added `import "ui/gfx/mojom/accelerated_widget.mojom";` after the geometry
  import shortcut.
- `QnxDmaBufFrame.widget`: `uint32` → `gfx.mojom.AcceleratedWidget`.
- `QnxGpuHost.ReportProducerLost`: `uint32 widget` → `gfx.mojom.AcceleratedWidget widget`.
- `QnxGpuControl.AttachWidget`: `uint32 widget` → `gfx.mojom.AcceleratedWidget widget`.
- `QnxGpuControl.ResizeWidget`: `uint32 widget` → `gfx.mojom.AcceleratedWidget widget`.
- `QnxGpuControl.DetachWidget`: `uint32 widget` → `gfx.mojom.AcceleratedWidget widget`.
- Added line breaks to multi-argument interface methods for readability.

### `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`
- Replaced dep label `:qnx_gpu_mojom` with `"//ui/ozone/platform/qnx/mojom"`.
- Removed the duplicate `mojom("qnx_gpu_mojom") { ... }` block.

### `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn`
- No changes. Still correctly defines `mojom("mojom")` with `public_deps` covering
  `//mojo/public/mojom/base`, `//ui/gfx/geometry/mojom`, and `//ui/gfx/mojom`.

---

## Validation

### Whitespace check
```bash
cd /home/yuta/chromium/src/cef
git diff --check
# Exit code 0: no whitespace errors in any touched file.
```

### GN generation

Phase 3 CEF-managed patches for `build/config/ozone.gni` and
`ui/ozone/BUILD.gn` were temporarily applied to the root Chromium source tree
(plus `ui/ozone/platform/qnx/` and `ui/ozone/platform/qnx/mojom/` directories
copied from the CEF new_files). After validation, root-source edits were
reverted and the temporary copy was removed.

```bash
# From /home/yuta/chromium/src
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_mojo_fix --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"

# GN output:
#   WARNING: Support for linking against a C++ standard library other
#   than the one in-buildtools/third_party/libc++ is deprecated...
#   Done. Made 33099 targets from 4278 files in 2748ms.
```

### Target discovery

```bash
ninja -C out/qnx_phase5_mojo_fix -t targets | grep "qnx"
# Results (relevant lines):
#   ui/ozone/platform/qnx:qnx                    (phony)
#   ui/ozone/platform/qnx/mojom:mojom            (phony)
#   ui/ozone/platform/qnx/mojom:mojom__build_metadata    (phony)
#   ... (generator / parser / check_deps / message_ids)
```

Only one mojom target exists: `//ui/ozone/platform/qnx/mojom:mojom`.
No duplicate target label is present.

### Mojom build

```bash
ninja -C out/qnx_phase5_mojo_fix ui/ozone/platform/qnx/mojom:mojom
# Result: 649/649 compiled successfully.
```

Generated outputs in `out/qnx_phase5_mojo_fix/gen/ui/ozone/platform/qnx/mojom/`:
- `qnx_gpu.mojom-data-view.h` — includes `ui/gfx/mojom/accelerated_widget.mojom-shared.h`
  and uses `gfx::mojom::AcceleratedWidgetDataView` for the widget field.
- `qnx_gpu.mojom-forward.h`, `qnx_gpu.mojom-shared-internal.h`,
  `qnx_gpu.mojom-params-data.h`, `qnx_gpu.mojom-module`, etc.

### QNX platform build

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_mojo_fix ui/ozone/platform/qnx:qnx
# Result: 798/798 compiled successfully.
```

Compiled objects in `out/qnx_phase5_mojo_fix/obj/ui/ozone/platform/qnx/`:
- `qnx/nx_window_manager.o` (94.1K)
- `qnx/qnx_window.o` (60.0K)
- `qnx/qnx_platform_event_source.o` (53.2K)
- `qnx/ozone_platform_qnx.o` (40.2K)
- `qnx/qnx_screen.o` (20.9K)
- `qnx/qnx_screen_context.o` (16.6K)
- `qnx/client_native_pixmap_factory_qnx.o` (2.4K)
- `mojom/mojom/qnx_gpu.mojom.o` (188.0K)

### Cleanup

```bash
# Root source reverted:
git -C /home/yuta/chromium/src checkout -- ui/ozone/BUILD.gn build/config/ozone.gni
# Temporary QNX directory removed from root source tree.
```

---

## Review blockers — status

| Finding | Severity | Status |
|---------|----------|--------|
| `qnx_gpu.mojom` uses `uint32 widget` instead of `gfx.mojom.AcceleratedWidget` | blocker | ✅ FIXED |
| Duplicate mojom targets in parent and subdir BUILD.gn files | high | ✅ FIXED |
| Validation commands use wrong paths | medium | ✅ FIXED (corrected in this report) |
| Reported retained build artifacts not corroborated | medium | ✅ CLARIFIED: the validation was run during the Phase 5 worker; artifacts were cleaned by later `git clean`. This report re-runs validation from scratch and retains artifacts in `out/qnx_phase5_mojo_fix` for inspection. |

---

## Phase 5 Mojo substep — acceptance assessment

**Review blockers are resolved.** The mojom schema now:
1. Imports `ui/gfx/mojom/accelerated_widget.mojom`.
2. Uses `gfx.mojom.AcceleratedWidget` for every widget ID field/parameter
   (`QnxDmaBufFrame.widget`, `ReportProducerLost`, `AttachWidget`, `ResizeWidget`,
   `DetachWidget`).
3. Follows the existing Ozone DRM pattern for mojom target layout: one mojom
   target in the `mojom/` subdir, parent BUILD.gn depends on it via the full GN
   label.
4. Generates and compiles without errors.

**The Phase 5 Mojo interface substep can now be accepted.** Remaining Phase 5
work is the GPU-side QNX render producer implementation and DMAbuf runtime
code — these are explicitly deferred substeps and are not part of this fix.

---

## Remaining Phase 5 work (not in scope of this fix)

| Item | Status |
|------|--------|
| GPU-side `QnxSurfaceFactoryOzone` and `QnxGLOzoneEGL` stubs | not started |
| `QnxRenderProducer` class — creates EGL/GLES render producer per widget ID/generation | not started |
| `QnxDmaBufFrame` C++ wrapper and DMAbuf export via `eglExportDMABUFImageMESA` | not started |
| Browser-side mojo binding: `QnxGpuControl` client implementation in `OzonePlatformQnx` | not started |
| GPU-side mojo binding: `QnxGpuHost` server implementation in GPU process | not started |
| Frame submission ACK / error handling | not started |
| GPU restart / reconnect and generation tracking | not started |
| Phase 5 acceptance: out-of-process GPU smoke path creates GPU-side render resources | not started |
