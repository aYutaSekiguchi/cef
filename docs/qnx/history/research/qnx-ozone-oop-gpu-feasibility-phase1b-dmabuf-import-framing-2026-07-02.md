# QNX Ozone OOP-GPU Phase 1B-dmabuf-import IPC-framing microtask

Date: 2026-07-03
Scope: compile-only fix of parent-to-consumer IPC framing; no QEMU run.

## Problem

The Phase 1B-dmabuf-import audit (`qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-audit-2026-07-02.md`) identified a blocking IPC framing mismatch:

- **Producer (parent-to-consumer path):** sent `qnx_dmabuf_ipc_header_t hdr` via a plain `send(consumer_fd, &hdr, ...)` call, then sent SCM_RIGHTS fds via a second `sendmsg(consumer_fd, &cmsg_send, MSG_NOSIGNAL)` carrying only a dummy iovec payload.
- **Consumer:** expected both header and fds in a single `recvmsg()` call (`recv_with_fds()` at `tools/qnx_probes/qnx_dmabuf_import_consumer.c:139-211`).

On a Unix-domain stream socket, ancillary SCM_RIGHTS data is associated with the byte(s) of the **immediately preceding** `sendmsg()` call, not with any earlier `send()`. Therefore the consumer's single `recvmsg()` would receive the dummy iovec bytes with the fds attached — or the fds would be missing entirely if the consumer did not call `recvmsg()` again after the initial header `send()`.

## Fix

**File changed:** `tools/qnx_probes/qnx_dmabuf_export_producer.c`

Replaced the two-step `send(hdr)` + `sendmsg(dummy+SCM_RIGHTS)` with a single `sendmsg()` that carries `hdr` as the iovec payload AND SCM_RIGHTS fds as ancillary data in the same call.

Before (lines 589–644):
```c
printf("[PARENT] Forwarding header to consumer...\n");
ssize_t sent = send(consumer_fd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
// error check ...
printf("[PARENT] Sent %zd-byte header to consumer.\n", sent);

printf("[PARENT] Forwarding %d plane fds to consumer via sendmsg(SCM_RIGHTS)...\n", n_fds);
struct iovec ciov;
char cdummy[64];
ciov.iov_base = cdummy;    // <-- dummy payload
ciov.iov_len  = sizeof(cdummy);
// ... build cmsg_send with SCM_RIGHTS ...
ssize_t sent_fds = sendmsg(consumer_fd, &cmsg_send, MSG_NOSIGNAL);
```

After (lines 589–634):
```c
printf("[PARENT] Forwarding header + %d plane fds to consumer via single sendmsg(SCM_RIGHTS)...\n",
       n_fds);

/*
 * Single sendmsg() carries both the fixed-size header as iovec payload
 * AND the SCM_RIGHTS ancillary fds.  This matches the consumer's
 * recv_with_fds() which does a single recvmsg() expecting header+fds
 * together — the ancillary fds are associated with the byte range from
 * this sendmsg, not from any prior send() call.
 */
struct iovec ciov;
ciov.iov_base = (void*)&hdr;   // <-- header IS the iovec payload
ciov.iov_len  = sizeof(hdr);

// ... build cmsg_send with SCM_RIGHTS ...
ssize_t sent_total = sendmsg(consumer_fd, &cmsg_send, MSG_NOSIGNAL);
```

The variable name `sent_total` and log format also updated to reflect the combined send. No other code was touched. The GPU-child-to-parent framing (separate `send()` + `sendmsg()` on the socketpair) was intentionally left unchanged — the parent receives that two-step delivery via its own two-step `recv()` + `recvmsg()`, which is the correct inverse.

## Consumer receive contract preserved

`recv_with_fds()` (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:139-211`) does exactly one `recvmsg()`:
```c
struct iovec iov;
iov.iov_base = hdr_out;
iov.iov_len  = sizeof(*hdr_out);   // receives qnx_dmabuf_ipc_header_t
// ... cmsg_buf for SCM_RIGHTS ...
ssize_t r = recvmsg(sock_fd, &msg, 0);
```
The iovec is sized for the full `qnx_dmabuf_ipc_header_t` and the CMSG area holds up to 4 SCM_RIGHTS fds. This matches the producer's single `sendmsg()` that now sends `sizeof(hdr)` bytes of header as iovec plus the same SCM_RIGHTS ancillary. The consumer can receive header+fds in one `recvmsg()`.

## Compile commands and results

Producer:
```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_export_producer \
  tools/qnx_probes/qnx_dmabuf_export_producer.c \
  -lsocket -lscreen -lEGL -lGLESv2
```
Result: **pass**, exit 0, no warnings or errors.

Consumer (unchanged source):
```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c \
  -lsocket -lscreen -lEGL -lGLESv2
```
Result: **pass**, exit 0, one pre-existing warning (unchanged from audit):
```
tools/qnx_probes/qnx_dmabuf_import_consumer.c:326:12: warning: 'composite_to_screen' defined but not used [-Wunused-function]
```

## Scope confirmation

- Only `tools/qnx_probes/qnx_dmabuf_export_producer.c` was edited.
- `qnx_dmabuf_ipc.h` was not modified.
- `qnx_dmabuf_import_consumer.c` was not modified.
- No GN/Ozone/runner code changed.
- No QEMU run performed.

## No blocking issues found

The change is mechanically correct: it puts the fixed-size header in the `sendmsg()` iovec and SCM_RIGHTS fds in the same `sendmsg()` ancillary, which is what the consumer's `recv_with_fds()` expects. No further inspection finds a compile or protocol-level blocker.

## Next smallest bounded runtime microtask

**Authorized next step:** compile and run the true DMAbuf producer/consumer probe under QEMU virgl to validate that the consumer receives header+fds and imports the EGLImage and binds the GL texture.

Run command (per plan `Phase 1B initial build/run commands`):
```sh
./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_probe.sock /tmp/qnx-dmabuf-consumer.bmp; ./qnx_dmabuf_import_consumer & sleep 1; ./qnx_dmabuf_export_producer; wait'
```

**Stop conditions:**
- Stop/report if QEMU fails to launch.
- Stop/report if the consumer's `recvmsg()` fails with `EBADF` or zero fds received.
- Stop/report if `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` fails after fd receipt.
- Stop/report if `glEGLImageTargetTexture2DOES` returns non-zero GL error after EGLImage creation.
- Do not attempt Screen-window display; `SCREEN_PROPERTY_EGL_HANDLE` is a known separate blocker.
- Report on import success (EGLImage created) vs. bind success (GL texture valid) as separate milestones.
