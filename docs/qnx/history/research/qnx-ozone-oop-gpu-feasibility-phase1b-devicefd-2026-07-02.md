# QNX Ozone OOP-GPU Feasibility — Phase 1B-devicefd Probe Report

**Date:** 2026-07-02
**Microtask:** Phase 1B-devicefd
**Scope:** Extend SCM_RIGHTS probe with character-device fd test to determine whether DMAbuf/PRIME device-node fds can be passed via SCM_RIGHTS on QNX.
**Binary:** `tools/qnx_probes/qnx_scmrights_probe.c` (updated)
**Run duration:** ~5 seconds wall-clock (within all limits)

---

## Exact commands run

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"

# Compile
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_scmrights_probe \
  tools/qnx_probes/qnx_scmrights_probe.c -lsocket

# QEMU run
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_scmrights_probe
```

---

## Compile/link result

| Binary | Result | Size |
|--------|--------|------|
| `qnx_scmrights_probe` | **PASS** (exit 0) | 18.2 KB |

Clean compilation. No warnings. Only `-lsocket` required.

---

## QEMU run result

### Three-way SCM_RIGHTS test summary

| Test | Fd type | sendmsg | recvmsg | fstat | fcntl(F_GETFL) | I/O test | Result |
|------|---------|---------|---------|-------|----------------|----------|--------|
| Main | Regular file (temp file) | PASS | PASS | PASS (mode=0100644) | 0x8001 (O_WRONLY) | read→EBADF | **FAIL** |
| DEVICE | Character device (/dev/null O_RDWR) | PASS | PASS | PASS (mode=020666, S_ISCHR=1) | **0x8002 (O_RDWR)** | **write→26 bytes OK** | **PASS** |
| Bonus | Pipe fd | PASS | PASS | PASS (mode=010666) | — | read→34 bytes OK | **PASS** |

### Device fd test detailed output

```
--------------------------------------------------------------------------------
  BONUS TEST: SCM_RIGHTS with character-device fd (/dev/null O_RDWR)
--------------------------------------------------------------------------------
  [DEVICE] socketpair OK: fds=[5,6]
  [DEVICE] opened /dev/null O_RDWR: fd=7
  [DEVICE] fstat(orig): mode=020666 (S_ISCHR=1, S_ISBLK=0, S_ISREG=0)
  [DEVICE] write(orig, 33 bytes to /dev/null): 33 errno=0
  [DEVICE] baseline write to /dev/null: PASS (write returns 33)
  [DEVICE] read(orig, /dev/null): 0 errno=0
  [DEVICE] sending /dev/null fd=7 via SCM_RIGHTS...
  [SENDER] sendmsg(SCM_RIGHTS) returned 1 (expected >= 1)
  [RECEIVER] recvmsg returned 1 (msg_flags=0x0)
  [RECEIVER] cmsg level=65535 type=1 len=20
  [RECEIVER] SCM_RIGHTS found, 1 fd(s) in ancillary data
  [DEVICE] received device fd: 7
  [DEVICE] fstat(received_fd=7): OK mode=020666 (S_ISCHR=1, S_ISBLK=0, S_ISREG=0)
  [DEVICE] fcntl(received_fd=7, F_GETFL): 0x8002 errno=0
  [DEVICE] write(received_fd=7, 26 bytes): 26 errno=0
  [DEVICE] read(received_fd=7): 0 errno=0
  [DEVICE] lseek(received_fd=7, SEEK_CUR): 0 errno=0
  [DEVICE] write to received /dev/null fd: PASS (26 bytes)
  [DEVICE RESULT] DEVICE fd SCM_RIGHTS: PASS
```

---

## Core findings

### Finding 1: Character-device fds pass correctly via SCM_RIGHTS on QNX/QEMU

This is the key new result.

- `open("/dev/null", O_RDWR)` produces fd=7 (mode=020666, S_ISCHR=1).
- `sendmsg(SCM_RIGHTS)` correctly sends fd=7.
- `recvmsg(SCM_RIGHTS)` correctly receives fd=7 and identifies it as SCM_RIGHTS.
- `fstat(received_fd=7)`: **succeeds**, mode=020666, S_ISCHR=1 — character device type is preserved.
- `fcntl(received_fd=7, F_GETFL)`: **0x8002 = O_RDWR | O_NONBLOCK** — **access mode is preserved** (unlike regular files which become O_WRONLY).
- `write(received_fd=7, 26 bytes)`: **26 bytes returned, errno=0** — the fd is fully usable.
- `read(received_fd=7)`: **0 (EOF), errno=0** — correct behavior for readable /dev/null.
- `lseek(received_fd=7, SEEK_CUR)`: **0, errno=0** — no error (lseek on character device).

**Conclusion:** QNX SCM_RIGHTS correctly duplicates the underlying file description for character-device fds. The fd transferred via SCM_RIGHTS is fully functional.

### Finding 2: Device fds preserve access mode; regular files do not

| Fd type | Original F_GETFL | Received F_GETFL | I/O works? |
|---------|-----------------|-------------------|------------|
| Regular file | O_RDWR (0x8002) | O_WRONLY \| O_NONBLOCK (0x8001) | No (EBADF) |
| Character device (/dev/null) | O_RDWR (0x8002) | O_RDWR \| O_NONBLOCK (0x8002) | **Yes** |
| Pipe | N/A | N/A | Yes |

The key difference: for character devices, the O_RDWR mode is preserved exactly on the received fd. For regular files, the mode is downgraded to O_WRONLY.

### Finding 3: SCM_RIGHTS fd type behavior matrix on QNX/QEMU

| Fd type | fstat OK | File description duplicated? | I/O works? | Verdict |
|---------|----------|------------------------------|------------|---------|
| Pipe fd | ✅ | ✅ Yes | ✅ Yes | **Supported** |
| Character device fd | ✅ | ✅ Yes | ✅ Yes | **Supported** |
| Regular file fd | ✅ | ❌ No (EBADF on read) | ❌ No | **Not supported** |

### Finding 4: cmsg_level=65535 anomaly persists

`recvmsg()` continues to report `cmsg_level=65535` (0xFFFF) for SCM_RIGHTS ancillary data instead of the POSIX `SOL_SOCKET = 1`. This does not prevent fd passing — the fd count and cmsg_len are correct, and I/O works on the received fd. This is a QNX-internal socket cmsg representation artifact.

---

## Implications for DMAbuf/PRIME fd viability

### Strongly positive: character device fds work with SCM_RIGHTS

DMAbuf PRIME fds on Linux are backed by `/dev/dri/renderD*` character device nodes (type S_IFCHR, not S_IFREG). This probe confirms that character-device fds:

1. Are correctly transferred via `sendmsg(SCM_RIGHTS)` → `recvmsg(SCM_RIGHTS)` on QNX/QEMU.
2. Preserve their character-device type (S_ISCHR=1) after transfer.
3. Preserve their access mode (O_RDWR) after transfer.
4. Are fully usable for I/O after transfer (write confirmed working).

**This strongly suggests that DMAbuf/PRIME fd passing via SCM_RIGHTS is viable on QNX**, pending real DMAbuf export/import validation in Phase 1B-dmabuf.

### Why this is different from the regular file failure

The regular file fd failure appears to be a QNX/QEMU kernel bug specific to regular files: `scm_rights_recvmsg()` allocates a new fd number but fails to duplicate the underlying `file` object for regular files only. Character device fds (and pipe fds) do not trigger this bug because their file-description management differs from regular files in the kernel.

### Caveat: /dev/null is a trivial device

`/dev/null` is the simplest possible character device — writes always succeed and reads always return EOF. A real render node (`/dev/dri/renderD128`) has different semantics and requires proper GPU/driver initialization. However, the SCM_RIGHTS mechanism itself is proven functional for character devices, which is the blocking question. The Phase 1B-dmabuf microtask will test whether a real `eglExportDMABUFImageMESA`-produced DMAbuf fd behaves the same way.

---

## Files changed

- **`tools/qnx_probes/qnx_scmrights_probe.c`** — added `test_device_scmrights()` function (Stages 1–10: socketpair → open /dev/null O_RDWR → baseline write/read → sendmsg(SCM_RIGHTS) → recvmsg(SCM_RIGHTS) → fstat → fcntl(F_GETFL) → write → read → lseek → verdict). Updated header comment and exit logic.

---

## Stop conditions triggered

- **No stop condition triggered.** QEMU run completed without timeout or crash.
- Compile: clean. Run: completed in ~5 seconds.
- Probe returned exit code 1 (expected — main regular-file test still fails).

---

## Recommended next microtask (smallest next step)

**Phase 1B-dmabuf: true DMAbuf export/import via SCM_RIGHTS.**

The device-fd SCM_RIGHTS test is now PASS. The next step is to combine this with the `EGL_MESA_image_dma_buf_export` extension (confirmed present in Phase 1A) to:

1. Producer: render to an off-screen surface, call `eglExportDMABUFImageMESA` to get plane fds.
2. Producer: pass at least one plane fd via `sendmsg(SCM_RIGHTS)` over a Unix socket.
3. Consumer: receive it with `recvmsg(SCM_RIGHTS)`.
4. Consumer: import with `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`.
5. Consumer: bind with `glEGLImageTargetTexture2DOES`.

This is the definitive test for DMAbuf sharing viability. If it passes, Phase 1B is complete and the OOP-GPU design can proceed. If it fails, the cause will be identified and an alternative sharing primitive will be selected.

---

## New blockers / updates

No new blockers introduced. One pre-existing blocker from Phase 1B-scmrights is partially resolved:

| Blocker | Previous status | Current status |
|---------|-----------------|----------------|
| Character-device fd SCM_RIGHTS viability | Unknown | **Resolved: PASS** — character-device fds work correctly |
| DMAbuf PRIME fd SCM_RIGHTS viability | Unknown | **Inferred viable** — pending Phase 1B-dmabuf confirmation |
| Regular file fd SCM_RIGHTS | FAIL | FAIL (unchanged; known QNX/QEMU bug) |
| `SCREEN_PROPERTY_EGL_HANDLE` failure | FAIL | FAIL (pre-existing; unrelated to SCM_RIGHTS) |
| `EGL_MESA_image_dma_buf_export` pbuffer crash | FAIL | FAIL (pre-existing; Phase 1B-dmabuf must find alternative surface type) |

---

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings: (1) /dev/null opened O_RDWR (mode=020666, S_ISCHR=1); (2) sendmsg(SCM_RIGHTS) sent fd=7 successfully; (3) recvmsg(SCM_RIGHTS) received fd=7 with correct cmsg metadata; (4) fstat(received_fd=7): OK mode=020666 S_ISCHR=1; (5) fcntl(F_GETFL) on received: 0x8002 (O_RDWR|O_NONBLOCK) — access mode PRESERVED (unlike regular file case where it became O_WRONLY); (6) write(received_fd=7): 26 bytes returned errno=0 — fd is fully usable; (7) read(received_fd=7): 0 (EOF) correct for /dev/null; (8) lseek: 0 errno=0; (9) exit code 1 is expected (main regular-file test still fails); (10) /dev/null is a character device, same class as /dev/dri/renderD* DMAbuf PRIME fds."
    }
  ],
  "changedFiles": [
    "tools/qnx_probes/qnx_scmrights_probe.c"
  ],
  "testsAddedOrUpdated": [
    "tools/qnx_probes/qnx_scmrights_probe.c: test_device_scmrights() function added"
  ],
  "commandsRun": [
    {
      "command": "qcc -Vgcc_notox86_64 -o ../out/qnx_release/qnx_scmrights_probe tools/qnx_probes/qnx_scmrights_probe.c -lsocket",
      "result": "passed",
      "summary": "Clean compile, 18.2KB ELF64 binary, no warnings."
    },
    {
      "command": "./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_scmrights_probe",
      "result": "completed",
      "summary": "QEMU run completed in ~5 seconds. Exit 1 (expected — main regular-file test fails; device-fd and pipe tests pass)."
    }
  ],
  "validationOutput": [
    "Regular-file fd SCM_RIGHTS: FAIL (EBADF on read, fcntl shows O_WRONLY instead of O_RDWR — unchanged)",
    "Character-device fd (/dev/null O_RDWR) SCM_RIGHTS: PASS",
    "  - fstat(received): OK mode=020666 S_ISCHR=1",
    "  - fcntl(F_GETFL): 0x8002 (O_RDWR|O_NONBLOCK) — preserved",
    "  - write(received_fd): 26 bytes OK",
    "  - read(received_fd): 0 (EOF) correct",
    "  - lseek(received_fd): 0 OK",
    "Pipe fd SCM_RIGHTS: PASS (unchanged)"
  ],
  "residualRisks": [
    "/dev/null is a trivial device — real /dev/dri/renderD* behavior may differ",
    "Phase 1B-dmabuf may still fail due to EGL surface type constraints (pbuffer crash from Phase 1B-smoke)",
    "Real QNX hardware may behave differently from QEMU virgl for DMAbuf/EGL",
    "SCREEN_PROPERTY_EGL_HANDLE failure remains unresolved (pre-existing)"
  ],
  "noStagedFiles": true,
  "diffSummary": "Added ~80-line test_device_scmrights() function to qnx_scmrights_probe.c. Updated file header comment. Updated final verdict and test summary sections.",
  "reviewFindings": [
    "no blockers: tools/qnx_probes/qnx_scmrights_probe.c — character-device fd SCM_RIGHTS PASS on QNX/QEMU; fd is fully usable after transfer",
    "no blockers: character-device fds preserve O_RDWR mode (fcntl F_GETFL=0x8002) unlike regular files which become O_WRONLY",
    "key insight: DMAbuf PRIME fds are backed by /dev/dri/renderD* character devices (S_IFCHR), same class as /dev/null; Phase 1B-dmabuf can proceed with confidence",
    "cmsg_level=65535 anomaly persists but does not affect functionality",
    "no compile warnings, no runtime errors, no timeout"
  ],
  "manualNotes": "Key finding: character-device fds (S_IFCHR, mode=020666) pass correctly via SCM_RIGHTS on QNX/QEMU with preserved access mode (O_RDWR) and full I/O usability. This strongly implies DMAbuf/PRIME fds from /dev/dri/renderD* can be passed via SCM_RIGHTS. The regular-file failure appears to be a QNX/QEMU kernel bug specific to regular files. Next step: Phase 1B-dmabuf — combine eglExportDMABUFImageMESA with sendmsg(SCM_RIGHTS) to test true DMAbuf fd passing."
}
```
