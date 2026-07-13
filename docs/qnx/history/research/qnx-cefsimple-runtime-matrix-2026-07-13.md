# cefsimple runtime matrix on QNX (2026-07-13)

- Date: 2026-07-13
- Scope: `//cef:cefsimple` runtime under QEMU `--virgl` on QNX 800 /
  Chromium 147.0.7727.147
- Status: investigation only. **No source fix applied. No rebuild. No commit.**
- Binary under test: `/home/yuta/chromium/src/out/qnx_release/cefsimple`
  (built 2026-07-11, before the 2026-07-13 `ad3d491a4` SCREEN_PROPERTY_FD
  Option A fix, so this matrix reflects the state of the current binary
  in `out/qnx_release`).

## 1. Methodology / commands

All runs used `tools/qnx_run.sh --virgl --kill-existing --boot-timeout 180`
on `/home/yuta/chromium/src/cef`. `--timeout` was 120 s for the first two
cases and 45 s for the rest, once the required markers were known to
appear within the first few seconds of each run; a live/hanging process
after the required markers is sufficient to classify the case.

The QNX guest has no `timeout` binary (`posix_spawnp(/.../timeout): No
such file or directory` if you wrap with `timeout` — observed on the
first broken V0 run). The proven pattern from
`qnx-ozone-phase7-cefsimple-runtime-diagnostics-2026-07-10` is to run
the binary directly and let `qnx_run.sh` bound the wall-clock with
`--timeout`. Each case was therefore one `qnx_run.sh` invocation with the
cefsimple (or content_shell) command line as the only positional arg.

Marker-only filter (no raw log reads): `/tmp/filter_cefsimple.py`
matches `QNX-TRACE`, `QNX_OZONE_GPU_TRACE`, `FATAL`, `ERROR`, `WARNING`,
`signal`, `SIG*`, `abort`, `std::system_error`, `mutex lock failed`,
`Resource deadlock`, `Initialization of all EGL display types failed`,
`GLDisplayEGL`, `DisplayEGL`, `EGLDisplay`, `eglSwapBuffers`, `EGL vendor`,
`EGL_MESA`, `ANGLE`, `use-angle`, `use-gl`, `CreatePlatformWindow`,
`AttachWidget`, `SubmitFrame`, `SubmitTestFrame`, `BuildScreenBufferDescriptor`,
`SCREEN_PROPERTY_FD`, `accepted=`, `implementation=`, `OnGpuServiceLaunched`,
`GPU process`, `exit code`, `exit 13`, `exit 1`, `exit 0`, `exit 134`,
`exit 139`, `__PI_QNX_EXIT__`, `Command:`, `sh -c`.

Common flags (where applicable):

- `--ozone-platform=qnx`
- `--no-sandbox`
- `--url=file:///mnt/nfs/out/qnx_release/qnx-option-a.html`
- `--ozone-qnx-gpu-trace`
- `--enable-logging=stderr`

## 2. Command matrix

| Case | UI mode        | GL backend                         | Extra                              | Log |
|------|----------------|------------------------------------|------------------------------------|-----|
| CS0  | (content_shell)| `--use-gl=egl`                     | none                               | `/tmp/content-shell-egl.log` |
| V0   | default Views  | `--use-gl=egl`                     | none                               | `/tmp/cefsimple-default-egl.log` |
| N0   | `--use-native` | `--use-gl=egl`                     | none                               | `/tmp/cefsimple-native-egl.log` |
| N0pre| `--use-native` | `--use-gl=egl`                     | `--env LD_PRELOAD=/usr/lib/libEGL.so.1` | `/tmp/cefsimple-native-egl-preload.log` |
| V1   | default Views  | `--use-gl=angle --use-angle=gl`    | none                               | `/tmp/cefsimple-views-angle-gl.log` |
| V2   | default Views  | `--use-gl=angle --use-angle=gles`  | none                               | `/tmp/cefsimple-views-angle-gles.log` |
| N1   | `--use-native` | `--use-gl=angle --use-angle=gl`    | none                               | `/tmp/cefsimple-native-angle-gl.log` |

Full command template (replace `<FLAGS>` with the case-specific extras):

```bash
./tools/qnx_run.sh --virgl --kill-existing --boot-timeout 180 --timeout 45 \
  [case-specific --env LD_PRELOAD=...] -- \
  '/mnt/nfs/out/qnx_release/<cefsimple|content_shell> \
   --ozone-platform=qnx <--use-gl=egl | --use-gl=angle --use-angle=gl|gles> \
   [--use-native] --no-sandbox \
   --url=file:///mnt/nfs/out/qnx_release/qnx-option-a.html \
   --ozone-qnx-gpu-trace --enable-logging=stderr'
```

## 3. Concise result matrix

| Case | Wrapper exit | Chromium outcome | Key markers |
|------|--------------|------------------|-------------|
| CS0  | 1 (timeout, still running) | PASS | `QnxGpuService::Initialize: gpu_host_remote bound`; `AttachWidget path=kScreenBridge init_ok=1 reason=preferring kScreenBridge primary path; Mesa kept as fallback`; `QnxRenderProducer::CaptureCurrentFramebufferToDmaBuf: captured compositor framebuffer`; `QnxRenderProducer::CreateMesaExportFrame: path=kMesaFallback source=compositor widget=1 generation=1 fourcc=0x34325241 planes=1 frame ready`; `QnxGpuHost::SubmitFrame: FINAL accepted=true display_ok=true; eglSwapBuffers reached` (repeats per frame). |
| V0   | 139 | FAIL (SIGSEGV) | GPU proc: `ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.`; `ui/ozone/common/gl_ozone_egl.cc:26] GLDisplayEGL::Initialize failed.`; `components/viz/service/main/viz_main_impl.cc:189] Exiting GPU process due to errors during initialization`. Browser: `segmentation violation (core dumped)`; `__PI_QNX_EXIT__:139`. **No `OzonePlatformQnx::CreatePlatformWindow` in the log.** |
| N0   | 1 (timeout, hang) | FAIL (no crash, hangs) | `OzonePlatformQnx::CreatePlatformWindow: bounds=10,10 1004x748`; `QnxWindow::QnxWindow: widget=1 screen_win=…`; GPU process respawns (host_id 1..7) with repeated `Initialization of all EGL display types failed`. After respawns: `QnxGpuService::Initialize: gpu_host_remote bound`; `AttachWidget widget=1 generation=1 size=1004x748`; **`QnxRenderProducer: eglGetDisplay(EGL_DEFAULT_DISPLAY) returned EGL_NO_DISPLAY` (qnx_render_producer.cc:152)**; `QnxGpuService::AttachWidget: widget=1 generation=1 SKIP SubmitTestFrameForWidget (producer_valid=false)`. |
| N0pre| 1 (timeout, still running) | PASS | `OnGpuServiceLaunched`; `QnxGpuService::Initialize: gpu_host_remote bound` (on first attempt); `OzonePlatformQnx::CreatePlatformWindow: bounds=10,10 1004x748`; `AttachWidget widget=1 generation=1 size=1004x748 path=kScreenBridge init_ok=1 reason=preferring kScreenBridge primary path; Mesa kept as fallback` (see full log line 65); `QnxGpuService::AttachWidget: widget=1 generation=1 TRIGGER SubmitTestFrameForWidget (remote_bound=true producer_valid=true)`; `QnxGpuHost::SubmitFrame: ENTERED`; `VALIDATION_PASSED widget=1 generation=1; proceeding to EGL/Screen import`; `about to call ImportAndDisplayFrame widget=1`; **`SubmitFrame: FINAL widget=1 generation=1 accepted=true display_ok=true; eglSwapBuffers reached`**; `QnxGpuService::SubmitTestFrameForWidget callback: widget=1 generation=1 accepted=1`. |
| V1   | 139 | FAIL (SIGSEGV) | Same as V0: GPU proc EGL init fails → `Exiting GPU process due to errors during initialization`; browser `segmentation violation (core dumped)`; no `CreatePlatformWindow`. |
| V2   | 139 | FAIL (SIGSEGV) | Same as V1 (gles variant identical to gl variant at the display-init layer). |
| N1   | 1 (timeout, hang) | FAIL (hang) | Same shape as N0: window created, GPU respawns, eventually binds, `QnxRenderProducer: eglGetDisplay(EGL_DEFAULT_DISPLAY) returned EGL_NO_DISPLAY`; `SKIP SubmitTestFrameForWidget (producer_valid=false)`; hangs. ANGLE selection does not change the outcome vs N0 because the generated ANGLE `libEGL` cannot produce a display on QNX either way. |

Exit codes: 139 = browser SIGSEGV (128 + 11). Exit 1 = `qnx_run.sh`
command-wait deadline reached (`--timeout`); cefsimple was still alive
(hanging or rendering).

## 4. content_shell comparison (proven baseline)

`content_shell --ozone-platform=qnx --use-gl=egl …` (CS0) is the working
reference for the GPU service path on QNX virgl. The proven invocation
is recorded in
`qnx-angle-dmabuf-export-investigation-2026-07-12/SCREEN-PROPERTY-FD-BLOCKER-2026-07-13`
§2.1 (post-`ad3d491a4` Option A); the matrix here re-runs it with the
deterministic `file:///mnt/nfs/out/qnx_release/qnx-option-a.html` URL.

What makes CS0 work, that V0/N0 do not:

| Aspect | content_shell (CS0) | cefsimple (V0, N0) |
|--------|---------------------|---------------------|
| `libEGL` resolution | `NEEDED libEGL.so.1` → QNX system Mesa | `libcef.so` has `NEEDED libEGL.so` → generated `out/qnx_release/libEGL.so` (ANGLE) |
| `eglGetDisplay(EGL_DEFAULT_DISPLAY)` | returns a usable display | `EGL_NO_DISPLAY` (ANGLE `rx::DisplayEGL` not selected for default native display on QNX) |
| GPU process EGL init | succeeds with Mesa | `gl_display.cc:673] Initialization of all EGL display types failed` |
| Window creation (`OzonePlatformQnx::CreatePlatformWindow`) | reached | Views: not reached before SIGSEGV; native: reached |
| GPU service bind (`gpu_host_remote bound`) | yes | N0: yes (after several GPU respawns); V0: not reached |
| `SubmitFrame` | `accepted=true display_ok=true; eglSwapBuffers reached` (loops) | N0: `SKIP SubmitTestFrameForWidget (producer_valid=false)`; V0: not reached |
| Workaround needed? | none | `LD_PRELOAD=/usr/lib/libEGL.so.1` (N0pre) restores the CS0-class behaviour |

The `LD_PRELOAD` line is the key bridging result: forcing QNX system
Mesa `libEGL.so.1` into the cefsimple GPU process gives N0pre the same
visible success shape as CS0 (`eglSwapBuffers reached`,
`accepted=true`). So **the N0 native-mode producer failure
(`eglGetDisplay(EGL_DEFAULT_DISPLAY)` → `EGL_NO_DISPLAY`,
`SKIP SubmitTestFrameForWidget (producer_valid=false)`) is caused by
EGL library resolution upstream of the Ozone/QNX producer code**, not
by the producer itself. The default CEF Views crash in V0/V1/V2 is a
separate phenomenon: it SIGSEGVs in the browser-creation path before
`OzonePlatformQnx::CreatePlatformWindow`, and whether it is downstream
of the same EGL resolution problem or independent of it is **not
resolved by the N0pre result** — see Recommendation 1.

## 5. Prioritized issues / root causes

### P0 — libcef.so loads generated ANGLE `libEGL`, not system Mesa (I1)

- `libcef.so` has `NEEDED libEGL.so` from `//third_party/angle:libEGL`,
  resolving to the generated `out/qnx_release/libEGL.so` rather than
  QNX system Mesa `libEGL.so.1`. content_shell does not carry this
  dependency and resolves Mesa instead.
- Symptom in every cefsimple case without LD_PRELOAD: the GPU process
  fails `gl_display.cc:673] Initialization of all EGL display types
  failed` (and, where it reaches the producer, `QnxRenderProducer:
  eglGetDisplay(EGL_DEFAULT_DISPLAY) returned EGL_NO_DISPLAY` at
  `qnx_render_producer.cc:152`).
- Root cause inside ANGLE: QNX has neither GBM nor Wayland, so
  `rx::DisplayEGL` is not selected for default native displays (per
  `qnx-angle-egl-runtime-investigation-2026-07-10` §"Explicit ANGLE
  results"). The plain `gl`/`gles` variants fail at
  `GLDisplayEGL::Initialize`; the `gles-egl` variant reaches the
  native-EGL ANGLE route but aborts with `std::system_error: mutex
  lock failed: Resource deadlock avoided` (134) per the same doc —
  not exercised in this matrix per the bounded `--use-angle=gl|gles`
  scope.
- Confirmed workaround: `LD_PRELOAD=/usr/lib/libEGL.so.1` (N0pre) →
  eglSwapBuffers reached.

### P0 — Default CEF Views SIGSEGV before `CreatePlatformWindow` (I3)

- V0, V1, V2 all reach GPU process EGL init failure → GPU exit → browser
  SIGSEGV (139), with **no `OzonePlatformQnx::CreatePlatformWindow`
  trace** in the log.
- The crash is in the CEF Views browser-creation path and is distinct
  from native mode (N0/N0pre/N1), which reaches window creation. It is
  also distinct from the `gles-egl` 134 abort reported in prior docs.
- `qnx-ozone-phase7-cefsimple-runtime-diagnostics-2026-07-10` already
  flagged this Views crash as the "Default CEF Views crash before
  `CreatePlatformWindow`" residual blocker; this matrix confirms it
  reproduces on the current binary.

### P1 — ANGLE runtime not viable on QNX (I4)

- `--use-gl=angle --use-angle=gl` and `…=gles` both fail at
  `GLDisplayEGL::Initialize` (V1, V2). Native mode (N1) reaches the
  window then hangs on `eglGetDisplay → EGL_NO_DISPLAY`, same shape
  as N0.
- Per `qnx-angle-egl-runtime-investigation-2026-07-10` and
  `qnx-angle-recursive-mutex-investigation-2026-07-11`, ANGLE on QNX
  requires (a) a `DisplayEGL` selection path for default native
  displays and (b) further mutex diagnostics; the `gles-egl` recursive-
  mutex abort remains unresolved upstream in ANGLE. Treated as a
  separate port-effort track, deliberately out of scope per the
  bounded matrix here.

### P2 — Diagnostic instrumentation left in binary (I5)

- The current binary prints `[QNX-ANGLE-TRACE] terminate handler
  installed (pid=N)` for every process spawned by cefsimple. The
  source is the `qnx-angle-throw-tracer` interposer from the
  2026-07-11 research (`docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11`).
  It is harmless but inflates every log and confuses first-time
  readers.

### P3 — QEMU environment benign noise (I6)

- `Failed to read /proc/sys/fs/inotify/max_user_watches`
  (`base/files/file_path_watcher_inotify.cc:929`) and
  `third_party/webrtc/rtc_base/cpu_info.cc:73] No function to get
  number of cores` appear in every run. Environment-specific, not
  cefsimple bugs.

## 6. Evidence log paths

```
/tmp/cefsimple-default-egl.log         V0   cefsimple default Views  + --use-gl=egl
/tmp/cefsimple-native-egl.log          N0   cefsimple --use-native  + --use-gl=egl
/tmp/cefsimple-native-egl-preload.log  N0pre cefsimple --use-native + --use-gl=egl + LD_PRELOAD=/usr/lib/libEGL.so.1
/tmp/cefsimple-views-angle-gl.log      V1   cefsimple default Views  + --use-gl=angle --use-angle=gl
/tmp/cefsimple-views-angle-gles.log    V2   cefsimple default Views  + --use-gl=angle --use-angle=gles
/tmp/cefsimple-native-angle-gl.log     N1   cefsimple --use-native  + --use-gl=angle --use-angle=gl
/tmp/content-shell-egl.log             CS0  content_shell            + --use-gl=egl
/tmp/filter_cefsimple.py               marker-only filter
/tmp/pi-cefsimple-status.txt           per-case + final PI-SUMMARY status file
```

The `qnx_run.sh` host logs and serial transcripts are at
`/home/yuta/chromium/src/out/qnx_release/qnx_run_<stamp>_cefsimple*.log*`
and `…_content_shell*.log*` (one set per case).

## 7. Recommended next investigations (NOT performed)

These are written up as next steps so the next session can pick them up
without re-deriving the matrix; **none were attempted in this session**.

1. **P0 linkage fix.** Change `libcef.so` to resolve QNX system
   `libEGL.so.1` instead of the generated ANGLE `libEGL.so` for the
   `--use-gl=egl` runtime path, per
   `qnx-ozone-phase7-cefsimple-runtime-diagnostics-2026-07-10`
   ("Avoid forcing generated ANGLE libEGL.so into every CEF process").
   Validate: re-run **N0** without `LD_PRELOAD`; expect it to reach
   `eglSwapBuffers reached` / `accepted=true display_ok=true`, matching
   N0pre. **Do not** expect V0 to clear as a consequence: re-run V0
   alongside N0 and treat its outcome as the evidence for whether the
   Views SIGSEGV is downstream of the same EGL resolution problem
   (clears with the fix) or independent (persists after the fix and
   must be addressed separately).
2. **P0 crash isolation.** If the linkage fix does not by itself stop
   the Views SIGSEGV, capture a stack trace using the
   sigaction-interpose pattern from
   `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/`
   (`DESIGN-SIGACTION-INTERPOSE-ONLY.md`). Investigate the CEF Views
   browser-creation path that crashes after the GPU process exit.
3. **P1 ANGLE port.** Continue the ANGLE display-init work tracked in
   `qnx-angle-egl-runtime-investigation-2026-07-10` §"ANGLE-preserving
   remediation path" (QNX `DisplayEGL` for default native displays;
   recursive-mutex diagnostic for `gles-egl`). Validate: V1, V2, N1
   reach a working EGL display.
4. **P2 binary hygiene.** Rebuild without the
   `qnx-angle-throw-tracer` interposer for any final validation run.
5. **Status update.** Once P0 items close, move the `cefsimple` /
   `cefsimple_capi` row in `docs/qnx/status.md` from "build/runtime
   bring-up target" toward a validated-runtime status, citing this
   matrix as the pre-fix reference.

## 8. What was NOT done

- No CEF-managed patch file was added, edited, or regenerated.
- No `git diff` / `patch_updater.py --resave` was run.
- `out/qnx_release` was not regenerated; the binary under test was
  used as-is.
- No source file under `cef/`, `cef/patch/`, `cef/patch/qnx/chromium/`,
  or `cef/patch/qnx/chromium/new_files/` was modified.
- No `git commit` / `git push` was made.
- No unrelated targets were smoked (only `cefsimple` and `content_shell`
  under QEMU `--virgl`).
- The `--use-angle=gles-egl` variant (documented to reach native-EGL
  ANGLE then abort with `Resource deadlock`/exit 134) was not run, per
  the bounded `--use-angle=gl|gles` scope of this matrix.
