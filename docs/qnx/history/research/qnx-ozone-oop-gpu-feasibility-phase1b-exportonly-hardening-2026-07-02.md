# QNX Ozone OOP-GPU Phase 1B-exportonly source hardening

Date: 2026-07-02
Scope: source-hardening microtask only; compile only; no QEMU run.

## Goal

Make the existing export-only probe safe to run by:
1. Correcting the malformed `eglCreateDRMImageMESA` Path A attribute list.
2. Preventing default fallthrough into the known-risk pbuffer Path B.

## Files changed

| File | Change |
|------|--------|
| `tools/qnx_probes/qnx_dmabuf_export_only_probe.c` | Full rewrite (~800 lines) |
| `tools/qnx_probes/README.md` | Updated Path B status description |

## Exact changes made

### `tools/qnx_probes/qnx_dmabuf_export_only_probe.c`

**Change 1 — Path A attribute list (was: malformed, now: correct)**

Before (malformed — `0x31D2` is `EGL_DRM_BUFFER_FORMAT_ARGB32_MESA` *value*, used as a key):

```c
EGLint drm_attrs[] = {
    0x31D2, /* EGL_DRM_BUFFER_FORMAT_ARGB32_MESA — WRONG: value as key */
    64, 64,  /* width/height as bare values */
    0x31D4, /* EGL_DRM_BUFFER_STRIDE_MESA — WRONG: stride is optional */
    64 * 4,
    0x31D1, /* EGL_DRM_BUFFER_USE_MESA — WRONG: value as key */
    0x31D0, /* WRONG: 0x31D0 is EGL_DRM_BUFFER_FORMAT_MESA key, used as value */
    0x31D3, /* placeholder */
    0,
    EGL_NONE
};
```

After (correct key/value pairs per QNX `eglext.h`):

```c
EGLint drm_attrs[] = {
    /* key */ EGL_DRM_BUFFER_FORMAT_MESA,        /* 0x31D0 */
    /* val */ EGL_DRM_BUFFER_FORMAT_ARGB32_MESA, /* 0x31D2 */
    /* key */ EGL_DRM_BUFFER_USE_MESA,           /* 0x31D1 */
    /* val */ (EGL_DRM_BUFFER_USE_SCANOUT_MESA | EGL_DRM_BUFFER_USE_SHARE_MESA),
    /* key */ EGL_WIDTH,  /* 0x3057 */
    /* val */ 64,
    /* key */ EGL_HEIGHT, /* 0x3058 */
    /* val */ 64,
    EGL_NONE
};
```

Constant definitions verified against `$QNX_TARGET/usr/include/EGL/eglext.h`:

```
EGL_DRM_BUFFER_FORMAT_MESA        0x31D0  <- KEY
EGL_DRM_BUFFER_USE_MESA           0x31D1  <- KEY
EGL_DRM_BUFFER_FORMAT_ARGB32_MESA 0x31D2  <- VALUE
EGL_DRM_BUFFER_MESA               0x31D3
EGL_DRM_BUFFER_STRIDE_MESA        0x31D4
EGL_DRM_BUFFER_USE_SCANOUT_MESA   0x00000001
EGL_DRM_BUFFER_USE_SHARE_MESA     0x00000002
```

**Change 2 — Default Path B disabled; guarded behind `--allow-pbuffer-risk`**

Before: Phase 5 unconditionally created pbuffer + GLES2 context before attempting Path A. If Path A failed, execution fell through to Path B (known SIGSEGV in Mesa/QNX virgl) without any guard.

After: Phases reorganized so:
- Phase 1–4 run EGL init + extension inventory + function pointer resolution only (no Screen, no GL).
- Phase 4 (gate) checks `eglExportDMABUFImageMESA` availability.
- Phase 5 (gate) checks Path A availability (`EGL_MESA_drm_image` + `eglCreateDRMImageMESA`).
- Path A attempted first. If Path A succeeds → export and exit.
- Path B attempted **only** if `--allow-pbuffer-risk` flag is passed on the command line.

```
  [Gate] Path A (EGL_MESA_drm_image): AVAILABLE.
  --- Path A: EGL_MESA_drm_image ---
  [Path A] Attempting eglCreateDRMImageMESA...
  ...
  [Path B] NOT ATTEMPTED: Path B is DISABLED by default.
  NOTE: --allow-pbuffer-risk is NOT authorized by Phase 1B plan.
```

Passing `--allow-pbuffer-risk` produces:

```
  [SETUP] --allow-pbuffer-risk: Path B (pbuffer fallback) is ENABLED.
  [SETUP] WARNING: Path B is NOT authorized by the Phase 1B plan.
```

**Change 3 — Restructured phases (no pbuffer before Path A)**

| Phase | Before | After |
|------|--------|-------|
| 1 | Screen context (diagnostic) | EGL init only |
| 2 | EGL init | EGL extension inventory |
| 3 | EGL extension inventory | EGL function pointer resolution |
| 4 | GLES extension check (pbuffer created here unconditionally) | Gate: `eglExportDMABUFImageMESA` available? |
| 5 | EGL function pointer resolution | Gate: Path A available? |
| 6 | Extension/function pointer table summary | Attempt Path A, then conditional Path B |
| 7 | STOP GATE | Removed (merged into Phase 4/5) |
| 8 | Export source candidates (Path A, then Path B without guard) | Final report |

The new Phase 5 attempts Path A before any GL/pbuffer code is touched. `eglCreatePbufferSurface` now appears only inside `try_path_b()`, which is entered only when `s_allow_pbuffer_risk == 1`.

### `tools/qnx_probes/README.md`

Updated the `qnx_dmabuf_export_only_probe.c` entry to clarify:
- Path A (EGL_MESA_drm_image, no GL) is the active path.
- Path B (pbuffer fallback) is **disabled by default** and guarded behind `--allow-pbuffer-risk`.
- Path B is explicitly noted as NOT authorized by the Phase 1B plan.

## Compile command and result

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o /tmp/qnx_dmabuf_export_only_probe_safe \
    tools/qnx_probes/qnx_dmabuf_export_only_probe.c \
    -lscreen -lEGL -lGLESv2
```

Result: **PASSED** — exit 0, no stdout/stderr, no warnings, no errors.

Binary size: 32656 bytes.

## Confirmation: default path avoids pbuffer fallback

| Property | Verified |
|----------|----------|
| `eglCreatePbufferSurface` is not called before Path A attempt | **YES** — only at line 401 inside `try_path_b()`, called only when `--allow-pbuffer-risk` is set |
| Path A attempted before any GL code | **YES** — `try_path_a()` at line 717; GL context only created in `try_path_b()` at line 360+ |
| Path B requires explicit `--allow-pbuffer-risk` | **YES** — guarded by `s_allow_pbuffer_risk` flag, parsed at line 562 |
| Default behavior: stop if Path A unavailable | **YES** — Phase 5 gate prints "No export path available. Stopping." and jumps to `cleanup_and_exit` |

## Stop conditions check

| Stop condition | Status |
|---------------|--------|
| Stop on any compile/link diagnostic | **PASSED** — none |
| Stop if source still calls `eglCreatePbufferSurface` before completing/aborting Path A | **PASSED** — verified: pbuffer only at line 401 inside guarded `try_path_b()` |
| Do not run QEMU | **COMPLIANT** — no QEMU invocation |

## Blockers and requested plan updates

None. Source hardening is complete.

### Plan update requested (minor, optional)

The `docs/qnx/ozone-out-of-process-gpu-plan.md` Phase 1B-exportonly stop conditions section currently reads:

> Stop/report if pbuffer is the only available path; do not re-enter the known Mesa/QNX virgl pbuffer crash path unless the probe can guard it safely.

This is now satisfied: the probe guards Path B behind `--allow-pbuffer-risk` and does not enter it by default. The plan may optionally add a note that the probe now guards Path B behind a flag, but this is not required before the bounded runtime microtask.

## Next smallest runtime microtask command and stop conditions

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_export_only_probe \
    tools/qnx_probes/qnx_dmabuf_export_only_probe.c \
    -lscreen -lEGL -lGLESv2
timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- \
    ./qnx_dmabuf_export_only_probe
```

**Stop conditions for the next bounded runtime microtask:**

1. Stop/report if required EGL extensions or function pointers are absent at Phase 4 gate.
2. Stop/report if `eglCreateDRMImageMESA` returns `EGL_NO_IMAGE_KHR` at Path A without proceeding to Path B (default).
3. Stop/report immediately after the first successful export of at least one DMAbuf fd with format/stride/modifier/fstat/fcntl metadata.
4. Stop/report on Path A failure — probe must exit cleanly, not enter Path B by default.
5. Do not broaden into IPC/import/display.
