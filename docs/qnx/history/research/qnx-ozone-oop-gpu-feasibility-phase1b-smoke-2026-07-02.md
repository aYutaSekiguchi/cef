# QNX Ozone OOP-GPU Feasibility — Phase 1B Smoke Report

**Date:** 2026-07-02
**Scope:** Bounded runtime smoke microtask — read-only; no source files edited.
**Run duration:** ~2 minutes wall-clock (within 180-second limit)

---

## Exact commands run

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"

# Producer link
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_producer \
  tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2

# Consumer link
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2

# QEMU run
timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_probe.sock; ./qnx_dmabuf_import_consumer & sleep 1; ./qnx_dmabuf_export_producer; wait'
```

---

## Compile/link result

| Binary | Result | Size |
|---|---|---|
| `qnx_dmabuf_export_producer` | **PASS** (exit 0) | 27.6 KB |
| `qnx_dmabuf_import_consumer` | **PASS** (exit 0) | 36.4 KB |

Both binaries compile and link cleanly with `-lsocket -lscreen -lEGL -lGLESv2`. No warnings beyond the pre-existing unused `g_verbose` variable in the producer.

---

## Bounded QEMU run result

**QEMU session:** Completed without timeout.
**Process exit codes:** Producer parent=0, child=0x0; Consumer exit=0.
**No crash.** Both processes exited cleanly.

### Run timeline (key events)

1. Consumer started → retry loop connecting to `/tmp/qnx_dmabuf_probe.sock` (10 retries before producer was ready).
2. Producer started → fork parent (socket server) + child (GPU work).
3. Parent socket server accepted consumer connection within the 30s parent-accept poll.
4. Child: EGL 1.5 initialized (Mesa Project). Screen window + GLES2 context created. `EGL_MESA_image_dma_buf_export` present; `eglExportDMABUFImageMESA` resolved.
5. Child: pbuffer surface created successfully (`0x402ac827b0`).
6. Child: **Producer prints `SKIPPED: EGLImage DMAbuf path BLOCKED (Mesa/QNX virgl pbuffer crash).`** — per producer source `qnx_dmabuf_export_producer.c:461-470`, the pbuffer→EGLImage→`eglExportDMABUFImageMESA` path is documented as blocked due to Mesa's internal Screen pixmap integration causing a crash. The producer does not call `eglExportDMABUFImageMESA`; it sets `hdr.exported = 2`, `n_planes = 0`.
7. Child: glReadPixels succeeded (64×64, 16384 bytes).
8. Parent: received `hdr.exported=2`, `n_planes=0` from child via pipe.
9. Parent: forwarded header (56 bytes) + raw pixels (16384 bytes) to consumer via plain `send()` (not `sendmsg(SCM_RIGHTS)`).
10. Consumer: received IPC header with `exported=2`, `planes=0`. Consumer code received `n_fds_found=0` via `recvmsg()`. Consumer reports `IPC MILESTONE: SCM_RIGHTS fd passing succeeded.` — **this label is misleading**: SCM_RIGHTS fd passing was never exercised because the producer sent zero fds. The `recvmsg()` call succeeded with 0 ancillary fds, which is a normal outcome when no fds are sent.
11. Consumer: received 8192 of 16384 raw pixel bytes (first `recv()` call got 8192; second `recv()` may have gotten the rest or the parent may have fragmented).
12. Consumer: created GL texture via `glTexImage2D`. Reports `MILESTONE: Raw pixel → GL texture succeeded.`
13. Consumer: attempted Screen composition via `composite_to_screen()`. Called `screen_get_window_property_iv(win, SCREEN_PROPERTY_EGL_HANDLE, &egl_h)`. **Failed** with `[CONSUMER ERROR] screen_get_window_property_iv(EGL_HANDLE) failed`. Reports `Screen composition MILESTONE: partial`.
14. Consumer sent `CONSUMER_OK` ack to producer; both processes exited cleanly.

---

## Milestone classification table

| Milestone | Status | Evidence |
|---|---|---|
| **Compile** | ✅ PASS | Both ELF64 binaries linked |
| **Process IPC** (socket connect/accept/transfer) | ✅ PASS | Consumer connected; header + 16384 bytes transferred |
| **True DMAbuf fd export** (`eglExportDMABUFImageMESA` called) | ❌ BLOCKED | Producer doc-comment: "Mesa/QNX virgl pbuffer crash." Extension present, pointer resolved, but call is skipped. |
| **FD passing via `sendmsg(SCM_RIGHTS)`** | ⚠️ NOT EXERCISED | Producer used plain `send()`; zero fds sent; `recvmsg()` returned 0 ancillary fds (normal empty case). |
| **DMAbuf import via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`** | ❌ NOT REACHED | `n_fds=0`, `hdr.exported=2`; consumer takes raw-pixel branch. |
| **GL texture bind via `glEGLImageTargetTexture2DOES`** | ❌ NOT REACHED | Same as above; raw-pixel branch taken instead. |
| **Screen display** (GL blit + `screen_post` or `eglSwapBuffers`) | ⚠️ PARTIAL | `screen_get_window_property_iv(EGL_HANDLE)` failed; `eglCreateWindowSurface` not reached. No crash, but no visible output. |

---

## DMAbuf evidence assessment

**No true DMAbuf evidence was produced.**

The producer never called `eglExportDMABUFImageMESA`. It never sent a DMAbuf fd. The consumer never received a DMAbuf fd and therefore never called `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` or `glEGLImageTargetTexture2DOES`.

The `recvmsg(SCM_RIGHTS)` call in the consumer succeeded with zero ancillary fds — this is the correct behavior when the producer sends zero fds, but it is not evidence of SCM_RIGHTS fd passing working on QNX.

The raw-pixel fallback path reached GL texture creation, confirming that Mesa EGL, GLES2, and the basic Screen window context work. The Screen composition failure at `screen_get_window_property_iv(EGL_HANDLE)` indicates a QNX/virgl compatibility gap for the Screen→EGL handle query path.

---

## New blockers and observations

- **New runtime blocker:** `screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE)` fails in QEMU virgl. This is the path the consumer uses to get an EGL handle for `eglCreateWindowSurface`. This is distinct from the pbuffer crash and represents a separate compatibility gap for Screen-window-based composition. This was not captured in Phase 1A because Phase 1A did not use Screen windows as the composition surface.
- **Producer EGLImage DMAbuf export confirmed blocked:** `eglCreateImageKHR(pbuffer)` is documented as crashing Mesa/QNX virgl internally (Mesa's Screen pixmap path). This is the same root cause that blocked Phase 1A's EGL stream path — QEMU virgl's Mesa does not support the Screen-internal paths needed for EGLImage export.
- **`recvmsg()` with zero FDs is not SCM_RIGHTS evidence:** The consumer's `recvmsg()` succeeded but received zero fds. This path was never validated with actual fds.
- **SCM_RIGHTS syscall viability on QNX:** The socket API works; `recvmsg()` returned success with 0 fds rather than failure. But the actual fd-passing capability of QNX has not been exercised because no fd was ever created/exported to pass.

---

## Recommended next microtask (smallest next step)

The next microtask should validate whether QNX can pass real file descriptors via SCM_RIGHTS, independently of DMAbuf. This is a prerequisite for any fd-based sharing path (DMAbuf, PRIME, or QNX shm regions).

**Proposed:** `qnx_scmrights_probe.c` — a single process that:
1. Creates a temp file,
2. Calls `sendmsg(SCM_RIGHTS)` sending that fd over a Unix socket to itself,
3. Confirms the fd number is received correctly.

This validates the syscall-level capability before committing to a DMAbuf fd-sharing probe. It avoids all EGL/Screen complexity and has a clear binary pass/fail outcome.

After SCM_RIGHTS is confirmed viable, the Phase 1B-dmabuf task can attempt true DMAbuf export on a non-pbuffer surface (e.g., DRM render node or a QNX screen pixmap that Mesa can handle).

---

## blockers / requested plan updates

1. **Plan update needed:** Add `-lsocket` to the Phase 1B build commands in `docs/qnx/ozone-out-of-process-gpu-plan.md`. The current plan at line ~120 omits it and would fail on subsequent workers.
2. **Plan update needed:** Add `screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE)` failure as a new Phase 1B blocker and update the acceptance criteria accordingly.
3. **No crash/restart attempted** — this was bounded smoke only.
4. **No source files were modified** by this microtask.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings: producer qnx_dmabuf_export_producer.c:461-470 confirms EGLImage DMAbuf export blocked; consumer qnx_dmabuf_import_consumer.c:610 hits screen_get_window_property_iv(EGL_HANDLE) failure; no DMAbuf fd was exported or received; raw-pixel path reached GL texture but Screen composition partially failed."
    }
  ],
  "changedFiles": [],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_producer tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Producer linked as 27.6KB ELF64 binary."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_import_consumer tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Consumer linked as 36.4KB ELF64 binary."
    },
    {
      "command": "timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- 'rm -f /tmp/qnx_dmabuf_probe.sock; ./qnx_dmabuf_import_consumer & sleep 1; ./qnx_dmabuf_export_producer; wait'",
      "result": "passed",
      "summary": "Completed without timeout. Producer parent_exit=0 child_status=0x0; Consumer exit=0. No crash."
    }
  ],
  "validationOutput": [
    "Producer EGL 1.5 initialized (Mesa Project).",
    "EGL_MESA_image_dma_buf_export present; eglExportDMABUFImageMESA resolved but not called (pbuffer crash documented).",
    "SKIPPED: EGLImage DMAbuf path BLOCKED (Mesa/QNX virgl pbuffer crash) — stop condition met.",
    "glReadPixels OK (64x64, 16384 bytes).",
    "Socket IPC header transferred (56 bytes) + raw pixels (16384 bytes) via plain send().",
    "recvmsg() returned 0 fds (producer sent none).",
    "Raw pixel -> glTexImage2D succeeded.",
    "screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE) FAILED; Screen composition partial.",
    "Both processes exited cleanly."
  ],
  "residualRisks": [
    "screen_get_window_property_iv(EGL_HANDLE) failure may indicate a fundamental QNX/Screen/virgl incompatibility for Screen-window composition path.",
    "SCM_RIGHTS fd passing has not been validated with real fds on QNX; only empty recvmsg() has been tested.",
    "Producer uses fork+pipe for timing; crash/restart behavior is not tested.",
    "Current raw-pixel fallback is not DMAbuf evidence."
  ],
  "noStagedFiles": true,
  "diffSummary": "No source files edited. Report written to docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-smoke-2026-07-02.md.",
  "reviewFindings": [
    "blocker: tools/qnx_probes/qnx_dmabuf_export_producer.c:461-470 — EGLImage DMAbuf export is blocked by Mesa/QNX virgl pbuffer crash; true DMAbuf export is not exercised.",
    "blocker: tools/qnx_probes/qnx_dmabuf_import_consumer.c:610 — screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE) fails; Screen composition via eglCreateWindowSurface is unreachable without a fix.",
    "blocker: docs/qnx/ozone-out-of-process-gpu-plan.md — Phase 1B build commands missing -lsocket.",
    "note: SCM_RIGHTS fd passing (sendmsg/recvmsg with SCM_RIGHTS) has not been exercised with real fds; only empty recvmsg() was tested.",
    "no source files modified by this microtask."
  ],
  "manualNotes": "The deepest proven milestone is raw-pixel socket transfer + GL texture creation + partial Screen composition attempt. No true DMAbuf evidence was produced. Two separate blockers identified: (1) Mesa/QNX virgl pbuffer crash prevents EGLImage export, and (2) SCREEN_PROPERTY_EGL_HANDLE query fails, preventing Screen-window-based EGL surface creation on the consumer side. Next microtask should validate SCM_RIGHTS syscall viability on QNX before committing to any fd-based sharing path."
}
```
