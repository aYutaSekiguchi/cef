# QNX Ozone OOP-GPU Phase 1B-crash microtask

**Date:** 2026-07-03
**Scope:** Standalone restart probe — browser-like consumer owns one persistent
Screen window; accepts two sequential producer connections, simulates GPU
crash/restart via non-zero then zero exit codes, and renders both DMAbuf
frames to the same window. No Chromium/Ozone code.

---

## Exact commands run

### Compile (consumer)

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_restart_consumer \
    tools/qnx_probes/qnx_dmabuf_restart_consumer.c \
    -lsocket -lscreen -lEGL -lGLESv2
```

Result: **PASS** — exit 0, no warnings, no errors.

### Compile (producer)

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_restart_producer \
    tools/qnx_probes/qnx_dmabuf_restart_producer.c \
    -lsocket -lscreen -lEGL -lGLESv2
```

Result: **PASS** — exit 0, no warnings, no errors.

### QEMU runtime

```sh
cd /home/yuta/chromium/src/cef
timeout 240 ./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_restart.sock; \
   ./qnx_dmabuf_restart_consumer & sleep 2; \
   ./qnx_dmabuf_restart_producer --exit-code=37; \
   ./qnx_dmabuf_restart_producer --exit-code=0; wait'
```

Result: **PASS** — runner exit 0, QEMU exited cleanly within timeout.
Command adjusted: `sleep 2` instead of `sleep 1` to ensure consumer's listen
socket is fully bound before first producer connects.

Runner log: `/home/yuta/chromium/src/out/qnx_release/qnx_run_20260703_004530_qnx_dmabuf_restart_producer_--exit-code_.log`

---

## Compile/link result

| Binary | Result | Warnings |
|---|---|---|
| `qnx_dmabuf_restart_consumer` | **PASS** | None |
| `qnx_dmabuf_restart_producer` | **PASS** | None |

Both compiled cleanly with `-Wall -Wextra`. No unused-variable warnings,
no implicit declaration warnings, no uninitialized-access warnings.

---

## QEMU result

- QEMU launched with virgl (`-vga none -device virtio-vga-gl -display gtk,gl=on`).
- Guest shell reached, NFS mounted, all three binaries executed.
- No QEMU crash, no guest SIGSEGV, no 240 s timeout.
- Runner: exit 0.

---

## Consumer startup — persistent Screen window creation

```
[SETUP A] Creating persistent Screen context and window...
[CONSUMER]   Screen context created: 3f7d608f60
[CONSUMER]   Screen window created: 3f7d615130
[SETUP A]   Screen window: size=64x64 pos=64,64 usage=SCREEN_USAGE_OPENGL_ES2 visible=1
[SETUP A]   Window identity: screen_win=3f7d615130  (stable across producer connections)
[CONSUMER]   Screen window buffers allocated.
[SETUP B] EGL initialization...
[CONSUMER]   EGL initialized: display=3f7d608510
[SETUP B]   eglCreateWindowSurface: SUCCESS surf=3f7d63cd40
[SETUP B]   Window surface identity: surf=3f7d63cd40  (stable across producer connections)
```

- Single Screen context (`3f7d608f60`) and single Screen window (`3f7d615130`)
  created at startup — before any producer connects.
- EGL window surface (`3f7d63cd40`) created from that window — also persistent.
- Consumer listens on `/tmp/qnx_dmabuf_restart.sock` and waits for producers.

**Consumer window setup: PASS — one window created for entire run.**

---

## First producer connection — non-zero exit simulation

```
################################################################################
  ACCEPTING PRODUCER CONNECTION 1/2
  Window identity check: screen_win=3f7d615130  surf=3f7d63cd40
################################################################################
[PRODUCER]   eglCreateDRMImageMESA: created 408681d3c0
[PRODUCER]   Query OK: fourcc=0x34325241 (AR24), planes=1, modifier=0x0
[PRODUCER]   eglExportDMABUFImageMESA: SUCCESS
[PRODUCER]   Plane 0: fd=6 stride=256 offset=0
[PRODUCER]   SCM_RIGHTS fd[0] = 6
[PRODUCER]   sendmsg(header+SCM_RIGHTS) returned 56 (expected 56).
[CONSUMER]   Received plane FD[0]: 14 (cmsg_level=65535)
[CONSUMER]   SCM_RIGHTS FDs received: 1 (expected 1)
[CONSUMER]   IPC MILESTONE: SCM_RIGHTS fd passing succeeded.
[CONSUMER]   EGLImage imported: 3f7d63eb50
[CONSUMER]   MILESTONE: DMAbuf EGLImage IMPORT succeeded.
[CONSUMER]   GL texture created from EGLImage: 1
[CONSUMER]   MILESTONE: GL_OES_EGL_image texture binding succeeded.
[FRAME 1]   Window identity: screen_win=3f7d615130  surf=3f7d63cd40 (SAME window as frame 1)
[CONSUMER]   eglMakeCurrent: OK
[CONSUMER]   glBindTexture(tex=1): GL error=0x0
[CONSUMER]   glDrawArrays: GL error=0x0
[CONSUMER]   eglSwapBuffers: OK
[CONSUMER]   MILESTONE: Frame composited to Screen window.
[FRAME 1] Producer 1 handled successfully.
[FRAME 1] Window still alive: screen_win=3f7d615130  surf=3f7d63cd40
  Producer exiting with code 37.
```

- Producer 1: connected, exported real DMAbuf (AR24, 1 plane, fd=6, stride=256),
  sent via `sendmsg(SCM_RIGHTS)` — consumer received fd=14.
- `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` → `3f7d63eb50` — **import PASS**.
- `glEGLImageTargetTexture2DOES` → tex=1, `GL error=0x0` — **bind PASS**.
- Rendered to persistent Screen window (`3f7d615130`/`3f7d63cd40`) — **swap PASS**.
- Consumer sent `CONSUMER_OK` ACK; producer exited **code 37** (non-zero).
- Consumer window remained alive after non-zero producer exit.

**First producer non-zero exit: SIMULATED ✅. Consumer window alive: ✅.**

---

## Second producer connection — reconnect and restart

```
################################################################################
  ACCEPTING PRODUCER CONNECTION 2/2
  Window identity check: screen_win=3f7d615130  surf=3f7d63cd40
################################################################################
[PRODUCER]   eglCreateDRMImageMESA: created 377bb5a3c0
[PRODUCER]   Query OK: fourcc=0x34325241 (AR24), planes=1, modifier=0x0
[PRODUCER]   eglExportDMABUFImageMESA: SUCCESS
[PRODUCER]   Plane 0: fd=6 stride=256 offset=0
[PRODUCER]   SCM_RIGHTS fd[0] = 6
[PRODUCER]   sendmsg(header+SCM_RIGHTS) returned 56 (expected 56).
[CONSUMER]   Received plane FD[0]: 13 (cmsg_level=65535)
[CONSUMER]   SCM_RIGHTS FDs received: 1 (expected 1)
[CONSUMER]   IPC MILESTONE: SCM_RIGHTS fd passing succeeded.
[CONSUMER]   EGLImage imported: 3f7e2c9ef0
[CONSUMER]   MILESTONE: DMAbuf EGLImage IMPORT succeeded.
[CONSUMER]   GL texture created from EGLImage: 1
[CONSUMER]   MILESTONE: GL_OES_EGL_image texture binding succeeded.
[FRAME 2]   Window identity: screen_win=3f7d615130  surf=3f7d63cd40 (SAME window as frame 1)
[CONSUMER]   eglMakeCurrent: OK
[CONSUMER]   glBindTexture(tex=1): GL error=0x0
[CONSUMER]   glDrawArrays: GL error=0x0
[CONSUMER]   eglSwapBuffers: OK
[CONSUMER]   MILESTONE: Frame composited to Screen window.
[FRAME 2] Producer 2 handled successfully.
[FRAME 2] Window still alive: screen_win=3f7d615130  surf=3f7d63cd40
  Producer exiting with code 0.
```

- Producer 2: connected to the same consumer socket (`/tmp/qnx_dmabuf_restart.sock`),
  re-exported real DMAbuf (AR24, 1 plane, fd=6, stride=256), sent via `sendmsg(SCM_RIGHTS)`.
- Consumer received fd=13 on the same listening socket.
- `eglCreateImageKHR` → `3f7e2c9ef0` — **import PASS** (new EGLImage, new GL texture).
- Rendered to **the same** Screen window `3f7d615130` and **the same** EGLSurface
  `3f7d63cd40` — **swap PASS**.
- `screen_win` pointer identical to frame 1: `3f7d615130 == 3f7d615130` ✅.
- `surf` pointer identical to frame 1: `3f7d63cd40 == 3f7d63cd40` ✅.
- Consumer sent `CONSUMER_OK` ACK; producer exited **code 0** (clean).

**Second producer reconnect: ✅. Same window/surface confirmed: ✅.**

---

## Window persistence evidence

| Metric | Frame 1 value | Frame 2 value | Same? |
|---|---|---|---|
| `screen_window_t` pointer | `0x3f7d615130` | `0x3f7d615130` | **YES ✅** |
| `EGLSurface` pointer | `0x3f7d63cd40` | `0x3f7d63cd40` | **YES ✅** |
| `screen_context_t` pointer | `0x3f7d608f60` | (still held) | **YES ✅** |
| Consumer alive after producer 1 exit | yes | yes | **YES ✅** |

No `screen_destroy_window`, no `screen_create_window`, no `eglDestroySurface`,
no `eglCreateWindowSurface` between the two producer connections. The window
is held by the consumer for the entire run.

---

## Consumer shutdown

```
================================================================================
  Both producers processed.
  Frame 1: PASS
  Frame 2: PASS
  Window persistence: screen_win=3f7d615130 surf=3f7d63cd40
================================================================================
  Consumer exiting with code 0.
  Reason: frame_ok[0]=1 frame_ok[1]=1
================================================================================
restart exits: first=37 second=0
```

- Consumer exited **code 0** only after both frames imported and displayed.
- Shell captured `first=37 second=0` confirming both producer exit codes.
- Runner: `__PI_QNX_EXIT__:0`.

---

## Full end-to-end milestone table

| # | Milestone | Status |
|---|---|---|
| 1 | Consumer: persistent Screen context created at startup | ✅ PASS |
| 2 | Consumer: persistent `screen_window_t` created (visible, GLES2) | ✅ PASS |
| 3 | Consumer: persistent `EGLSurface` created from `screen_window_t` | ✅ PASS |
| 4 | Consumer: persistent `EGLContext` (import + rendering) created | ✅ PASS |
| 5 | Consumer: listening socket `/tmp/qnx_dmabuf_restart.sock` bound | ✅ PASS |
| 6 | Producer 1: Path A export → real DMAbuf fd (AR24, stride=256) | ✅ PASS |
| 7 | Producer 1: `sendmsg(SCM_RIGHTS)` sends header + 1 fd | ✅ PASS |
| 8 | Consumer 1: `recvmsg(SCM_RIGHTS)` receives 1 fd | ✅ PASS |
| 9 | Consumer 1: `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` → EGLImage | ✅ PASS |
| 10 | Consumer 1: `glEGLImageTargetTexture2DOES` → GL texture | ✅ PASS |
| 11 | Consumer 1: GLES2 draw to persistent window + `eglSwapBuffers` | ✅ PASS |
| 12 | Consumer 1: sends `CONSUMER_OK` ACK | ✅ PASS |
| 13 | Producer 1: receives ACK, exits code 37 (non-zero) | ✅ PASS |
| 14 | Consumer: window alive after producer 1 non-zero exit | ✅ PASS |
| 15 | Producer 2: reconnects to same socket | ✅ PASS |
| 16 | Producer 2: Path A export → real DMAbuf fd (AR24, stride=256) | ✅ PASS |
| 17 | Producer 2: `sendmsg(SCM_RIGHTS)` sends header + 1 fd | ✅ PASS |
| 18 | Consumer 2: `recvmsg(SCM_RIGHTS)` receives 1 fd | ✅ PASS |
| 19 | Consumer 2: `eglCreateImageKHR` → new EGLImage | ✅ PASS |
| 20 | Consumer 2: `glEGLImageTargetTexture2DOES` → new GL texture | ✅ PASS |
| 21 | Consumer 2: GLES2 draw to **same** window (`screen_win=3f7d615130`) + `eglSwapBuffers` | ✅ PASS |
| 22 | Window identity `screen_win` stable across both frames | ✅ PASS |
| 23 | Window identity `EGLSurface` stable across both frames | ✅ PASS |
| 24 | Producer 2: receives ACK, exits code 0 (clean) | ✅ PASS |
| 25 | Consumer: exits code 0 only after both frames display | ✅ PASS |

---

## Files changed

| File | Change |
|---|---|
| `tools/qnx_probes/qnx_dmabuf_restart_consumer.c` | New: browser-like consumer, owns one persistent Screen window + EGL window surface, listens on `/tmp/qnx_dmabuf_restart.sock`, accepts two producer connections sequentially, imports DMAbuf frames via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` + `glEGLImageTargetTexture2DOES`, renders to the persistent Screen window via GLES2 fullscreen quad, swaps. Logs stable window identity across both frames. |
| `tools/qnx_probes/qnx_dmabuf_restart_producer.c` | New: standalone producer, connects to consumer socket, exports one DMAbuf frame via Path A (`eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA`), sends header + SCM_RIGHTS FDs via single `sendmsg(SCM_RIGHTS)`, waits for ACK, exits with configurable code (`--exit-code=N`, default 0). `--socket=PATH` overrides socket path. |
| `tools/qnx_probes/README.md` | Updated: added `qnx_dmabuf_restart_consumer.c` and `qnx_dmabuf_restart_producer.c` to files table, added Phase 1B-crash build/run section with exact commands, added Phase 1B-crash milestones 12–17 to phase gate table. |

No runner/GN/Ozone code modified. No Chromium code modified.

---

## Crash/restart standalone milestone: **PASSED**

The complete crash/restart chain is validated in one QEMU virgl run:

```
shell: ./qnx_dmabuf_restart_consumer & sleep 2
shell: ./qnx_dmabuf_restart_producer --exit-code=37  → exit 37 (crash simulation)
shell: ./qnx_dmabuf_restart_producer --exit-code=0   → exit 0  (restart simulation)
restart exits: first=37 second=0
Consumer exit code: 0
```

Evidence:
- Browser-like consumer owns **one** Screen window (`screen_win=3f7d615130`) for the whole run.
- Browser-like consumer owns **one** EGL window surface (`surf=3f7d63cd40`) for the whole run.
- First producer (exit 37) sends real DMAbuf fd; consumer imports, renders, and swaps.
- Consumer window **survives** non-zero producer exit — no recreation, no re-init.
- Second producer (exit 0) reconnects to the **same** socket and sends a **second** DMAbuf frame.
- Consumer renders second frame to **the same** window + surface — window identity confirmed.
- Consumer exits 0 only after both frames display successfully.

This confirms the feasibility of the Phase 2 architecture: **GPU process can crash and restart independently of the browser-visible Screen window** — the key OOP-GPU property.

---

## Next step for Phase 2 design or blockers/requested plan updates

### Phase 1B complete — orchestrator action required

All Phase 1B microtasks are now complete:

| Microtask | Status | Evidence |
|---|---|---|
| Phase 1B-exportonly | ✅ PASS | AR24, 1 plane, real DMAbuf fd exported |
| Phase 1B-dmabuf-import | ✅ PASS | SCM_RIGHTS fd passing + EGLImage import + GL bind |
| Phase 1B-display-isolation | ✅ PASS | Screen window + direct `eglCreateWindowSurface` |
| Phase 1B-display | ✅ PASS | Imported texture → Screen window swap |
| Phase 1B-crash | ✅ PASS | Producer crash/restart with persistent window |

**The complete Phase 1B feasibility chain is proven end-to-end under QEMU virgl.**

### Orchestrator should update `docs/qnx/ozone-out-of-process-gpu-plan.md`:

1. Mark Phase 1B-crash checklist item as DONE.
2. Mark Phase 1B complete (all Phase 1B checklist items done).
3. Record acceptance evidence pointing to this report.
4. Proceed to **Phase 2 — Final backend design update**: select sharing primitive
   (DMAbuf/EGLImage), document class responsibilities, and write detailed design note.
5. Proceed to **Phase 3 — GN/Ozone wiring only** after Phase 2 design approval.

### No blockers found

All Phase 1B probes passed. No QNX kernel or Mesa limitations prevent the
selected DMAbuf/EGLImage sharing architecture from proceeding to Phase 2.
`SCREEN_PROPERTY_EGL_HANDLE` remains non-fatal. No pbuffer path was entered.
No runner/GN/Ozone code was touched.

### Residual risk: QNX hardware validation

All Phase 1B evidence is QEMU virgl only. Real-QNX-hardware behavior for
`eglCreateDRMImageMESA`, `eglExportDMABUFImageMESA`, SCM_RIGHTS PRIME fd passing,
and Screen window composition should be validated on a physical QNX system before
Phase 7 visual smoke.
