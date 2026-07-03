# QNX Ozone OOP-GPU Phase 1B-dmabuf-import audit

Date: 2026-07-03
Scope: read-only audit plus bounded qcc compile/link only; no QEMU run; no source edits.

## Review

- Correct: the approved producer/consumer files now substantially reflect the Phase 1B-dmabuf-import intent. The plan requires Path A export, SCM_RIGHTS fd passing, EGL_LINUX_DMA_BUF_EXT import, and glEGLImageTargetTexture2DOES binding without Screen display (`docs/qnx/ozone-out-of-process-gpu-plan.md:138-140`, `docs/qnx/ozone-out-of-process-gpu-plan.md:186-210`). The producer source uses `eglCreateDRMImageMESA` (`tools/qnx_probes/qnx_dmabuf_export_producer.c:165-177`), exports via `eglExportDMABUFImageMESA` (`tools/qnx_probes/qnx_dmabuf_export_producer.c:211-221`), sets `hdr.exported = 1` and real plane metadata (`tools/qnx_probes/qnx_dmabuf_export_producer.c:240-253`), then sends exported fds with `sendmsg(SCM_RIGHTS)` (`tools/qnx_probes/qnx_dmabuf_export_producer.c:300-316`).
- Correct: the consumer source receives SCM_RIGHTS ancillary fds (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:154-211`), builds an `EGL_LINUX_DMA_BUF_EXT` import attribute list (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:228-278`), and binds the EGLImage to a GLES texture with `glEGLImageTargetTexture2DOES` (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:293-321`).
- Correct: compile/link is complete enough to build both binaries. Producer compiled cleanly. Consumer compiled and linked with one warning: `composite_to_screen` is defined but unused (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:326`), consistent with Screen composition being skipped at the current phase.
- Blocker: the parent-to-consumer IPC framing appears inconsistent with the consumer receive path. The producer parent sends the header first with plain `send()` (`tools/qnx_probes/qnx_dmabuf_export_producer.c:590-599`), then sends fds in a separate `sendmsg(SCM_RIGHTS)` carrying only dummy payload (`tools/qnx_probes/qnx_dmabuf_export_producer.c:601-637`). The consumer's `recv_with_fds()` expects the fixed-size header and ancillary fds in one `recvmsg()` call (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:140-211`). On a stream socket, the ancillary fds are associated with the data byte(s) from the later `sendmsg`, not the earlier header bytes, so this can yield a valid header with zero fds. Fix this before accepting any runtime result.
- Note: the source still contains raw-pixel fallback code in the consumer (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:642-716`) and a Screen composition helper (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:326-430`), but current producer Path A no longer advertises `hdr.exported = 2`, and main unconditionally skips Screen composition (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:724-727`). This is not a compile blocker, but it is extra code for this microtask.
- Note: the README documents the intended run with `--no-screen-composition` and no Screen display acceptance (`tools/qnx_probes/README.md:30-63`, `tools/qnx_probes/README.md:116-134`). The implementation actually skips Screen composition regardless of the flag, while still printing a flag-dependent banner; this is acceptable for import/bind but should be cleaned up later if display is reintroduced.

## Current partial file inventory

`git status --short --untracked-files=all -- docs/qnx/ozone-out-of-process-gpu-plan.md docs/qnx/history/research tools/qnx_probes` shows the QNX plan, research reports, and probe directory are untracked in this checkout. Relevant probe files currently present:

| Path | Lines | Status / role |
|---|---:|---|
| `tools/qnx_probes/README.md` | 149 | Documents Phase 1B-dmabuf build/run and milestones. |
| `tools/qnx_probes/qnx_dmabuf_ipc.h` | 68 | Shared header; fixed-size metadata plus SCM_RIGHTS fd protocol. |
| `tools/qnx_probes/qnx_dmabuf_export_producer.c` | 672 | Current producer; Path A export plus fork/socket relay. |
| `tools/qnx_probes/qnx_dmabuf_import_consumer.c` | 758 | Current consumer; recv/import/bind path plus unused Screen helper. |
| `tools/qnx_probes/qnx_dmabuf_export_only_probe.c` | 772 | Prior export-only probe; runtime evidence already recorded. |
| `tools/qnx_probes/qnx_scmrights_probe.c` | 630 | Prior SCM_RIGHTS prerequisite probe. |
| `tools/qnx_probes/qnx_egl_extension_probe.c` | 422 | Prior extension inventory probe. |

No staged files were present during this audit (`git diff --cached --name-only` returned no paths).

## Compile/link commands and summarized output

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

Result: **pass**, exit 0, no warnings/errors.

Consumer:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c \
  -lsocket -lscreen -lEGL -lGLESv2
```

Result: **pass**, exit 0, one warning:

```text
tools/qnx_probes/qnx_dmabuf_import_consumer.c:326:12: warning: 'composite_to_screen' defined but not used [-Wunused-function]
```

## Specific audit answers

- Producer complete enough to compile/link: **yes**.
- Consumer complete enough to compile/link: **yes**, with the unused Screen helper warning.
- Producer appears to use proven export-only Path A: **yes**. It follows the previously validated `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` path; prior runtime report proved this path exported AR24 one-plane dmabuf under QEMU virgl (`docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-runtime-2026-07-02.md:65-84`).
- Producer appears to send real fds with `sendmsg(SCM_RIGHTS)`: **yes in source**, both GPU-child-to-parent and parent-to-consumer paths use SCM_RIGHTS (`tools/qnx_probes/qnx_dmabuf_export_producer.c:300-316`, `tools/qnx_probes/qnx_dmabuf_export_producer.c:618-637`). Runtime fd receipt is still unproven for dmabuf because QEMU was not run in this audit.
- Consumer appears to import with `EGL_LINUX_DMA_BUF_EXT`: **yes**, target `0x3270` is passed to `eglCreateImageKHR` (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:276-278`).
- Consumer appears to bind with `glEGLImageTargetTexture2DOES`: **yes** (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:306-321`).
- Consumer avoids Screen display in current main path: **yes**, main prints that Screen composition is skipped and does not call `composite_to_screen()` (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:724-727`).

## Likely timeout reason

The qcc build is fast and does not explain a 15-minute worker timeout. The likely cause is that the worker broadened into implementation/runtime/debugging before writing the required report. The current source also has a runtime-blocking IPC framing mismatch: producer parent sends header and SCM_RIGHTS fds in two separate socket writes, while consumer expects both in one `recvmsg()`. Repeated QEMU attempts around that mismatch, plus the producer's built-in 30s/60s poll waits, could consume the worker budget quickly.

## Smallest safe next microtask

Do **not** run broad QEMU validation until the IPC framing is fixed and recompiled.

Smallest code microtask:

1. In `tools/qnx_probes/qnx_dmabuf_export_producer.c`, change only the parent-to-consumer forwarding path so it sends `qnx_dmabuf_ipc_header_t hdr` as the `sendmsg()` iovec in the same call that carries the SCM_RIGHTS fds. Remove or bypass the preceding plain `send(consumer_fd, &hdr, ...)` for that path.
2. Leave GPU-child-to-parent framing as-is for the next microtask, because parent currently receives that path in two steps.
3. Recompile only; stop before QEMU.

Exact compile commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_export_producer \
  tools/qnx_probes/qnx_dmabuf_export_producer.c \
  -lsocket -lscreen -lEGL -lGLESv2
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c \
  -lsocket -lscreen -lEGL -lGLESv2
```

Stop conditions:

- Stop/report if either compile fails.
- Stop/report if a source edit changes probe scope beyond parent-to-consumer IPC framing.
- Stop/report if the consumer still cannot receive header+fds in a single `recvmsg()` by inspection.
- Do not run QEMU in that microtask unless the plan explicitly authorizes a separate bounded runtime run.

## Plan updates required before continuing

No architecture-level plan change is required before a narrow IPC-framing fix. The plan already authorizes updating the producer/consumer to validate Path A export, SCM_RIGHTS fd pass, EGL import, and GL bind without Screen display (`docs/qnx/ozone-out-of-process-gpu-plan.md:138-140`, `docs/qnx/ozone-out-of-process-gpu-plan.md:186`). After the IPC-framing compile microtask succeeds, the next plan entry should explicitly authorize a bounded QEMU runtime run and define stop conditions for import failure vs. bind failure.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings include a compile pass for both producer and consumer, source evidence for Path A export and EGL import/bind, and a blocker in parent-to-consumer IPC framing with file/line citations."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-audit-2026-07-02.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "git status --short --untracked-files=all -- docs/qnx/ozone-out-of-process-gpu-plan.md docs/qnx/history/research tools/qnx_probes && git diff --cached --name-only",
      "result": "passed",
      "summary": "QNX docs/probe files are untracked in this checkout; no staged files."
    },
    {
      "command": "wc -l tools/qnx_probes/qnx_dmabuf_ipc.h tools/qnx_probes/qnx_dmabuf_export_producer.c tools/qnx_probes/qnx_dmabuf_import_consumer.c tools/qnx_probes/README.md tools/qnx_probes/qnx_dmabuf_export_only_probe.c tools/qnx_probes/qnx_scmrights_probe.c tools/qnx_probes/qnx_egl_extension_probe.c",
      "result": "passed",
      "summary": "Inventory captured line counts for seven qnx_probes files."
    },
    {
      "command": "source ../out/qnx_release/qnx_env.sh; export PATH=\"$QNX_HOST/usr/bin:$PATH\"; qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_producer tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Producer compile/link exit 0 with no warnings/errors."
    },
    {
      "command": "source ../out/qnx_release/qnx_env.sh; export PATH=\"$QNX_HOST/usr/bin:$PATH\"; qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_import_consumer tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Consumer compile/link exit 0 with one unused-function warning for composite_to_screen."
    }
  ],
  "validationOutput": [
    "Producer compile: exit 0, no warnings/errors.",
    "Consumer compile: exit 0; warning: tools/qnx_probes/qnx_dmabuf_import_consumer.c:326:12: 'composite_to_screen' defined but not used.",
    "No QEMU run performed per task restriction."
  ],
  "residualRisks": [
    "Runtime dmabuf fd receipt/import/bind remains unproven because QEMU was explicitly not run.",
    "Parent-to-consumer IPC framing likely prevents consumer from receiving SCM_RIGHTS fds with its current single recvmsg path.",
    "Consumer creates a small pbuffer GLES context; compile passes, but runtime EGL behavior remains unvalidated in this audit.",
    "Screen display/composition remains out of scope and the helper is currently unused."
  ],
  "noStagedFiles": true,
  "diffSummary": "Added the requested read-only audit report only; no source files edited.",
  "reviewFindings": [
    "blocker: tools/qnx_probes/qnx_dmabuf_export_producer.c:590-637 and tools/qnx_probes/qnx_dmabuf_import_consumer.c:140-211 - producer parent sends header and SCM_RIGHTS fds in separate stream writes, but consumer expects header plus fds in one recvmsg; fix framing before runtime acceptance.",
    "note: tools/qnx_probes/qnx_dmabuf_import_consumer.c:326 - composite_to_screen is unused, producing one -Wunused-function warning; acceptable for no-Screen import/bind phase.",
    "no blocker: tools/qnx_probes/qnx_dmabuf_export_producer.c:165-221 - producer uses proven Path A eglCreateDRMImageMESA + eglExportDMABUFImageMESA.",
    "no blocker: tools/qnx_probes/qnx_dmabuf_import_consumer.c:276-321 - consumer imports with EGL_LINUX_DMA_BUF_EXT and binds with glEGLImageTargetTexture2DOES."
  ],
  "manualNotes": "Report written at the authoritative path. No QEMU was run and docs/qnx/ozone-out-of-process-gpu-plan.md was not edited."
}
```
