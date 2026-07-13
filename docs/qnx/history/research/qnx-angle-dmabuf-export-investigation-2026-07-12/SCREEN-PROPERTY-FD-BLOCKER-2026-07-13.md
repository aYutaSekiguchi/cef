# Stage 1+2 post-fix blocker investigation: SCREEN_PROPERTY_FD ENOTSUP

- Date: 2026-07-13
- Scope: QEMU x86_64 with `--virgl` (virtio-gpu), Chromium 147 / CEF qnx_7727
        @ `a24d4b134`
- Status: reproduced in this session. Implementation-grade diagnosis confirmed.

---

## 1. Background — review outcome

The 2026-07-12 ROOT-CAUSE draft attributed the EGL init failure to a missing
`libkhronos` configuration file in the QEMU environment. The user disputed
that conclusion and asked for a clean reproduction of the actual
SCREEN_PROPERTY_FD failure. This doc records the corrected view and verbatim
log evidence. The reproduction in §2 was performed in this session using the
exact invocation the user supplied.

Three corrections to the 2026-07-12 draft:

1. **"LD_PRELOAD=/usr/lib/libEGL.so.1 is required for --use-gl=egl"** — WRONG.
   I had been running `qnx_run_test.sh` which is `headless` by default; it
   has no GPU device exposed to the QEMU guest, so `--use-gl=egl` could not
   initialize EGL and either fell through to `gl_factory.cc:111` policy
   rejection or to `gl_display.cc:673`. The user's reply pointed out that
   the QEMU invocation must use **virtio-gpu** via `--qemu-graphics virgl`
   (`qnx_run.sh --virgl`) so the GPU process can actually bring up Mesa.
2. **Main blocker identity** — confirmed as `SCREEN_PROPERTY_FD` getter on a
   pixmap-created buffer returning `ENOTSUP` (errno 48). This is in our Stage 1+
   added code, not in the EGL/ANGLE runtime.
3. **`libkhronos` configuration file** — *not observed* in my reproduction
   run; the hypothesis is dropped from main cause. It remains a side
   observation for the future, tagged "not in this reproduction".

`--use-angle` ANGLE display init failure is **also reproduced** in the second
case below and is kept as a **separate, parallel known issue** (matching the
existing `qnx-angle-egl-runtime-investigation-2026-07-10.md` and
`qnx-angle-recursive-mutex-investigation-2026-07-11.md` conclusions). It is
*not* the reason for black screen with the system path on real QNX hardware,
and it is *not* what the Stage 1+2 kScreenBridge path is supposed to fix.

---

## 2. Reproduction (verifiable)

Tool: `/home/yuta/chromium/src/cef/tools/qnx_run.sh` (older script, with the
`--virgl` flag). All workdirs are `/home/yuta/chromium/src/cef`.

### 2.1 `--use-gl=egl` — reaches producer code path, fails on SCREEN_PROPERTY_FD

```bash
cd /home/yuta/chromium/src/cef
LOG=$(mktemp /tmp/egl_case.XXXXXX.log)
./tools/qnx_run.sh --virgl --kill-existing --boot-timeout 180 --timeout 70 -- \
  ./content_shell --ozone-platform=qnx --use-gl=egl --ozone-qnx-gpu-trace \
  --enable-logging=stderr --v=1 about:blank >"$LOG" 2>&1 || true
```

Filter for key markers:

```bash
python3 - <<PY "$LOG"
import sys
from pathlib import Path
p=Path(sys.argv[1])
for i,l in enumerate(p.read_text(errors='ignore').splitlines(),1):
    if any(k in l for k in [
        'QNX_OZONE_GPU_TRACE',
        'BuildScreenBufferDescriptor',
        'Not supported (48)',
        'Initialization of all EGL display types failed',
        'libkhronos',
    ]):
        print(f"{i}: {l}")
PY
```

Observed (verbatim, from `/tmp/egl_case.W0F2st.log`):

```text
49: ... QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote bound; GPU process is ready to call SubmitFrame
60: ... QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 size=800x600
61: ... QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 path=kScreenBridge init_ok=1 reason=preferring kScreenBridge primary path; Mesa kept as fallback
62: ... QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 TRIGGER SubmitTestFrameForWidget (remote_bound=true producer_valid=true)
63: ... ERROR:ui/ozone/platform/qnx/qnx_render_producer.cc:515] QnxRenderProducer::BuildScreenBufferDescriptor: failed to read SCREEN_PROPERTY_FD: Not supported (48)
```

Negative observations (errors that DID NOT appear):

- No `gl_display.cc:673` "Initialization of all EGL display types failed".
- No `libkhronos: Exiting: Failed to open configuration file.`
- No ANGLE display init failure.
- No `gl_factory.cc:111` impl policy rejection.

Flow interpretation:

1. QEMU's `--virgl` exposes `virtio-gpu`. The GPU process picks up the Mesa
   EGL display; `gl_factory.cc:111` accepts `--use-gl=egl` (system EGL/Mesa).
2. `qnx_gpu_host` and `qnx_render_producer` initialize, the latter selects
   `kScreenBridge` (Stage 1+2 active path), `init_ok=1`.
3. The first widget-attached `SubmitTestFrameForWidget` exercise runs
   `CreateExportFrame` → `BuildScreenBufferDescriptor` →
   `screen_get_buffer_property_iv(screen_buffer_, SCREEN_PROPERTY_FD, &fd)`.
4. The getter returns `-1` with `errno == ENOTSUP`. Our impl logs the error
   and bails the export. The producer state stays `is_valid_=true` for the
   next attempt.

### 2.2 `--use-gl=angle` — ANGLE display init failure + same producer error

```bash
cd /home/yuta/chromium/src/cef
LOG=$(mktemp /tmp/angle_case.XXXXXX.log)
./tools/qnx_run.sh --virgl --kill-existing --boot-timeout 180 --timeout 70 -- \
  ./content_shell --ozone-platform=qnx --use-gl=angle --enable-logging=stderr \
  --v=1 about:blank >"$LOG" 2>&1 || true
```

Observed (verbatim, from `/tmp/angle_case.bTmqdv.log`):

```text
50: ... ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
52: ... ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
80: ... ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
82: ... ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
91: ... ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
93: ... ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
103: ... ERROR:ui/ozone/platform/qnx/qnx_render_producer.cc:515] QnxRenderProducer::BuildScreenBufferDescriptor: failed to read SCREEN_PROPERTY_FD: Not supported (48)
```

Interpretation:

- ANGLE display init fails for every display type tried — confirming the
  known ANGLE runtime issue (see `--use-angle` known problem references).
- The browser process keeps running after the GPU process exits, so
  `qnx_render_producer.cc:515` is still hit through the screen-bridge path
  attempt and lands the same `ENOTSUP` on `SCREEN_PROPERTY_FD`.

So `SCREEN_PROPERTY_FD ENOTSUP` is an **independent blocker** from ANGLE
display init failure. Even with `--use-gl=angle` resolved elsewhere, the
producer's `BuildScreenBufferDescriptor` still hits the same property-getter
error.

### 2.3 Tool error I had previously introduced

I had previously assumed `tools/qnx_run_test.sh` was equivalent to
`tools/qnx_run.sh --virgl`. It is not: `qnx_run_test.sh` defaults to
`headless`, which has no GPU. Without `--virgl` (or another GPU device),
the producer code path is unreachable from `--use-gl=egl`, so the
SCREEN_PROPERTY_FD failure never surfaces. The user's clarification of the
correct invocation (`qnx_run.sh --virgl`) restored the missing GPU device.

---

## 3. SCREEN_PROPERTY_FD property semantics (from `screen.h`)

Verified at `/home/yuta/qnx800/target/qnx/usr/include/screen/screen.h`:

```c
/** A single integer that indicates the file descriptor to be used to access
*   a buffer's memory.  Currently, this can only be used to provide memory to
*   Screen.  When setting this property type, ensure that you have sufficient
*   storage for one integer.
*   ...
*   - Applicable to the following Screen API object(s):
*     - buffer
*/
SCREEN_PROPERTY_FD = 174,
```

The header is explicit:

- The document says "**this can only be used to provide memory to Screen**"
  — `SCREEN_PROPERTY_FD` is for *setting* an externally-provided FD as the
  backing of a buffer (`screen_set_buffer_property_iv`).
- `Configurable: No` (per header body nearby) means the buffer does not
  expose an FD for `screen_get_buffer_property_iv(...)`, in particular when
  Screen owns the memory.

So `ENOTSUP` (errno 48) on `screen_get_buffer_property_iv(buf, FD, &fd)`
is **the documented correct behaviour** for any buffer whose memory was not
externally supplied via `SCREEN_PROPERTY_FD`. Stage 1+2 calls this getter on
a pixmap-created buffer, where the memory is internally allocated by Screen
through `screen_create_pixmap_buffer`. There is no FD to read.

---

## 4. Why Stage 1+2 reached the error

Stage 1+2 implementation:

```cpp
// qnx_render_producer.cc:511-518 (annotated)
if (!screen_buffer_) { ... }
int fd = -1;
if (screen_get_buffer_property_iv(screen_buffer_, SCREEN_PROPERTY_FD, &fd) !=
        0 ||
    fd < 0) {
  if (out_error) { *out_error = "SCREEN_PROPERTY_FD unavailable"; }
  PLOG(ERROR) << "QnxRenderProducer::BuildScreenBufferDescriptor: "
              << "failed to read SCREEN_PROPERTY_FD";
  return false;
}
const int dup_fd = HANDLE_EINTR(dup(fd));
...
```

- The buffer is created via `screen_create_pixmap_buffer(...)` (Stage 1+2,
  InitializeScreenBridge flow).
- `screen_create_pixmap_buffer` does **not** allocate a user-provideable FD.
- The getter is therefore correctly unsupported, and our impl incorrectly
  treats it as the only valid path for cross-process sharing.

The Stage 2 design assumption — "use Screen's pixmap buffer memory FD as
a dma-buf equivalent" — is a category error: Screen's pixmap buffers do not
have an FD property.

---

## 5. Proposed fix strategy

Three options, in increasing order of scope:

### Option A — Graceful non-shareable fallback (recommended for QEMU)

- In `BuildScreenBufferDescriptor`, when `SCREEN_PROPERTY_FD` returns
  `ENOTSUP` (errno 48), treat it as expected and:
  - Set `out_desc->fd` to an invalid descriptor (already default).
  - Set `out_desc->native_image` from `SCREEN_PROPERTY_NATIVE_IMAGE` if
    available; otherwise leave null.
  - Mark the frame as "render-only" via a new `QnxDmaBufFrame` flag (or
    reuse `frame.fd.is_valid()` falseness at the call site).
- In `CreateExportFrame` and downstream, only attempt cross-process sharing
  when `frame.fd.is_valid()`. When not valid, log a one-time informational
  message and submit a non-shareable frame; the producer side has done the
  render (via `RenderSolidIntoScreenBuffer`), the importer will read the
  buffer through the same Screen context and present it locally.
- For our QEMU smoke, this allows the code path to complete without crash,
  with rendering visible only through the Screen buffer (QEMU framebuffer
  surface back-channel — scope of future QEMU setup work).

### Option B — Use `SCREEN_PROPERTY_NATIVE_IMAGE` + screen-side EGL stream

- Replace the FD-based handoff with an `EGLImage` handoff:
  - `screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_NATIVE_IMAGE, &img)`.
  - Wrap as `EGLImage` via `eglCreateImageKHR(EGL_NATIVE_PIXMAP_KHR, img, ...)`.
  - Hand `EGLImageKHR` to the importer via `SCREEN_PROPERTY_EGL_HANDLE` or
    a custom import path.
- Requires MESA's `EGL_KHR_image_pixmap` and consumer-side `eglCreateImageKHR`
  in `qnx_frame_importer`. This still depends on Mesa stack QEMU availability,
  which prior investigation shows is unavailable — so Option B does not
  unblock QEMU smoke either, only real hardware.

### Option C — Plain `screen_create_buffer` with user-provided FD

- In `InitializeScreenBridge`, instead of a pixmap, use
  `screen_create_buffer()` with `SCREEN_PROPERTY_FD` *set* by us to a
  previously-allocated dma-buf-equivalent FD; the getter then returns our
  own FD. This closes the loop but requires extra memory-allocation glue.
  Plus QEMU typically can't allocate this kind of FD at all, so this option
  is **not** the QEMU path either.

**Recommendation**: implement Option A first (small change, graceful), then
re-run smoke. Option B is appropriate for real-hardware testing but does not
unblock QEMU smoke.

---

## 6. Demoted / parallel issues

### 6.1 `--use-angle` ANGLE display init failure (parallel, documented)

This is the failure at `gl_display.cc:673` "all EGL display types failed"
when the user passes `--use-angle` (any variant). It is **not** what black
screen with the system path on real QNX hardware depends on, and it is
**not** the blocker Stage 1+2 was authored to address.

It is documented and confirmed in:

- `docs/qnx/history/research/qnx-angle-egl-runtime-investigation-2026-07-10.md`
  (ANGLE Display selection fails on QNX — no GBM/Wayland).
- `docs/qnx/history/research/qnx-angle-recursive-mutex-investigation-2026-07-11.md`
  (recursive-mutex fix unblocks one mutex abort but a second abort site
  remains; not yet resolved upstream in ANGLE).

These should remain parallel investigations. Stage 1+2's `kScreenBridge`
design explicitly avoids dependence on ANGLE Display types.

### 6.2 `libkhronos` configuration file hypothesis (not observed)

Previously held in the 2026-07-12 ROOT-CAUSE draft as the **main cause** of
the EGL init failure. The reproduction in §2.1 with `--use-gl=egl` showed:

- No `libkhronos: Exiting: Failed to open configuration file.` message.
- EGL init reaching the producer code path.

That reproduction disconfirmed the hypothesis for this execution. It remains
tagged for future attention (QEMU environment setup gap if `--use-gl=angle`
is ever enabled on `--use-angle=swiftshader` and a similar exit code
surfaces), but it is **not** the current main cause and should not be cited
as such in follow-up commit messages or status reports.

The config-file content is still uncertain. `screen.h` requires the
tokens:

```text
begin khronos ... end khronos
begin egl display ... end egl display
begin wfd device ... end wfd device
```

These were located via `strings /home/yuta/qnx800/target/qnx/x86_64/usr/lib/libEGL.so | grep "begin khronos"`,
but no sample file was found in `/home/yuta/qnx800` or the QEMU image build.
Future work to construct one remains a QEMU environment improvement, not
a Stage 1+2 implementation issue.

---

## 7. Recommended next steps (sequenced)

1. **Apply Option A fix** to `qnx_render_producer.cc:515` and the
   `PopulateFrameFromScreenDescriptor` consumer. Estimate: ~30 lines of edits
   + ~5 lines of header change. Risk: low (graceful branch, no destructive
   change to existing pixels).
2. **Re-run smoke** with `qnx_run.sh --virgl --kill-existing --timeout 70 --`
   and `./content_shell --ozone-platform=qnx --use-gl=egl --ozone-qnx-gpu-trace
   --enable-logging=stderr --v=1 about:blank` (the verified invocation in
   §2.1). Verify `SCREEN_PROPERTY_FD` is no longer fatal (Option A fallback
   is engaged) and exit code is clean (no 139/134).
3. **If smoke is clean**, run `--use-gl=angle` smoke (§2.2 invocation) to
   confirm the ANGLE display init failure is unchanged (parallel known
   issue, not affected by Option A).
4. Update `IMPLEMENTATION-PLAN.md` §1(c) with the Option A behavior (renderer
   always renders into Screen buffer; FD is best-effort for shareable
   fallback).
5. Commit fixes as a follow-up CEF-managed patch.
