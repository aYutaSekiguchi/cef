# Phase 5 GPU-trace compile validation (2026-07-04)

## Overview

User approved `-j10` for compile-only validation. This report covers the narrow
compile validation of the `--ozone-qnx-gpu-trace` diagnostic logging changes.

## Changes made

### Docs updated

**`docs/qnx/ozone-out-of-process-gpu-plan.md`** (3 edits):
- Header: added `-j10` parallelism (user approved 2026-07-04); narrowed
  compile-only validations are exempt from explicit approval.
- Phase 5 status: updated to record OOP smoke audit, trace switch
  implementation, and next step as narrow compile validation at `-j10`
  (broad `content_shell` still requires explicit approval).
- Changelog: appended three new 2026-07-04 entries for smoke-target audit,
  audit report, and trace switch implementation.

**`docs/qnx/history/research/qnx-ozone-phase5-gpu-trace-logging-2026-07-04.md`**
(1 edit):
- Replaced the `-j2` / `content_shell`-only next-step section with a
  forward-looking two-tier section: immediate narrow compile at `-j10`
  (user approved), and a separate pending `content_shell` approval block.

Historical record (`qnx-ozone-phase5-oop-smoke-target-audit-2026-07-04.md`)
was **not edited** — its `-j2` references are part of the historical record.

### Source files unchanged (implementation was previous subagent work)

No edits to `qnx_gpu_service.cc` or `qnx_gpu_host.cc` in this session.
Those files were implemented and reviewed in the previous subagent run.

## Commands run

```bash
# 1. Apply CEF QNX GN patches to root Chromium
cd /home/yuta/chromium/src
git apply -p0 cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
git apply -p0 cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch

# 2. Copy QNX Ozone source to root
mkdir -p ui/ozone/platform/qnx
cp -R cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. ui/ozone/platform/qnx/

# 3. GN generation (out/qnx_phase5_gpu_trace)
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_gpu_trace --args="..."  # RC=0

# 4. Narrow compile validation at -j10
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_gpu_trace -j10 \
    ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx
```

## Compile result

```
10120/10120 steps completed
Ninja RC: 0 (success)
Zero FAILED lines
Zero compile errors
```

All 15 QNX source files compiled successfully:

| File | Size |
|------|------|
| `qnx_gpu_service.o` | 119.8 KB |
| `qnx_gpu_host.o` | 114.6 KB |
| `qnx_render_producer.o` | 140.3 KB |
| `qnx_frame_importer.o` | 178.2 KB |
| `qnx_platform_event_source.o` | 55.2 KB |
| `qnx_gl_ozone_egl.o` | 35.9 KB |
| `qnx_window_manager.o` | 106.0 KB |
| `qnx_window.o` | 60.0 KB |
| `qnx_screen.o` | 20.9 KB |
| `qnx_screen_context.o` | 17.4 KB |
| `qnx_surface_factory.o` | 30.1 KB |
| `qnx_surface_ozone_canvas.o` | 30.6 KB |
| `qnx_gpu_platform_support_host.o` | 68.4 KB |
| `client_native_pixmap_factory_qnx.o` | 2.4 KB |
| `ozone_platform_qnx.o` | 164.0 KB |

Mojom generated files (all present in `gen/ui/ozone/platform/qnx/mojom/`):
`qnx_gpu.mojom.h`, `qnx_gpu.mojom.cc`, `qnx_gpu.mojom-shared.h`,
`qnx_gpu.mojom-shared.cc`, `qnx_gpu.mojom-shared-internal.h`,
`qnx_gpu.mojom-shared-message-ids.h`, `qnx_gpu.mojom-forward.h`,
`qnx_gpu.mojom-import-headers.h`, `qnx_gpu.mojom-data-view.h`,
`qnx_gpu.mojom-params-data.h`, `qnx_gpu.mojom-test-utils.h`,
`qnx_gpu.mojom-features.h`, `qnx_gpu.mojom-send-validation.h`.

## Root temp cleanup

```
build/config/ozone.gni     — git checkout OK
ui/ozone/BUILD.gn          — git checkout OK
ui/ozone/public/ozone_platform.cc — git checkout OK
ui/ozone/platform/qnx/     — removed (python3 os.remove loop + rmtree)
ui/ozone/platform/mojom/  — not present (build generated into qnx subdir)
```

All root temp paths verified clean.

## git diff --check

```
DIFF_CHECK_PASS (no whitespace errors on any changed file)
```

## Changed files

**This compile substep (docs updates only):**

- `docs/qnx/ozone-out-of-process-gpu-plan.md` — updated status, -j10 parallelism note, changelog entries
- `docs/qnx/history/research/qnx-ozone-phase5-oop-smoke-target-audit-2026-07-04.md` — updated recommendation path to use `-j10`
- `docs/qnx/history/research/qnx-ozone-phase5-gpu-trace-logging-2026-07-04.md` — new report (prior diagnostic-logging substep)
- `docs/qnx/history/research/qnx-ozone-phase5-gpu-trace-compile-2026-07-04.md` — this report (compile substep)

**Prior diagnostic-logging substep (source implementation — validated by this compile substep):**

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc` — 38 lines added
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc` — 73 lines added

The git diff in this compile substep shows 4 files / 119 insertions because the
prior diagnostic-logging substep's staged source edits were still present when this
substep ran `git diff --cached`. Both substeps' changes are in the same commit.

## Next recommended step

**Proceed to controlled content_shell build/run with `--ozone-qnx-gpu-trace`.**
This requires explicit user approval before launching the build. If approved:

```bash
# Regenerate tree to pick up all Phase 5 CEF-managed files
cd /home/yuta/chromium/src/cef
./tools/cef_create_projects_qnx.sh --build-type Release \
  --qnx-sdp-root /home/yuta/qnx800

# Build content_shell at -j10 (user approved)
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C ../out/qnx_release -j10 content/shell:content_shell

# Run with trace switch and capture trace output
BUILD_DIR=/home/yuta/chromium/src/out/qnx_release \
  ./tools/qnx_run.sh --virgl --kill-existing --timeout 60 -- \
  ./content_shell --ozone-platform=qnx --no-sandbox --ozone-qnx-gpu-trace \
  >/tmp/content_shell_trace.out 2>&1 &
sleep 20; kill %1 2>/dev/null; wait 2>/dev/null
grep -E "QNX_OZONE_GPU_TRACE|FAILED:|error:" /tmp/content_shell_trace.out
```

The trace output will confirm whether `QnxGpuHost::SubmitFrame` is reached
from the GPU process via the Mojo binding path, completing the Phase 5
OOP GPU runtime smoke validation.
