# QNX Ozone OOP-GPU Feasibility — Phase 1B DMAbuf Recovery Audit

**Date:** 2026-07-02
**Scope:** Read-only audit of partial/unaccepted Phase 1B probe files under `tools/qnx_probes/`.
**QEMU:** Not run.

## Review

### Correct

- `docs/qnx/ozone-out-of-process-gpu-plan.md:95-102` lists the intended Phase 1B files, and the on-disk inventory matches those names plus the Phase 1A probe.
- `tools/qnx_probes/README.md:46-57` correctly states the intended Phase 1B milestones: DMAbuf export, SCM_RIGHTS fd passing, EGLImage import, GL texture bind, and Screen composition.
- `tools/qnx_probes/qnx_dmabuf_ipc.h:7-17` defines a simple Unix-domain socket + SCM_RIGHTS protocol, and `tools/qnx_probes/qnx_dmabuf_ipc.h:34-48` defines a fixed-size metadata header with an explicit `exported` state.
- The current C files are complete enough to compile and link when `-lsocket` is included. Producer link produced a QNX x86_64 ELF binary in `/tmp`; consumer link also produced a QNX x86_64 ELF binary in `/tmp`.

### Blocker

- `tools/qnx_probes/qnx_dmabuf_export_producer.c:525-535` unconditionally skips the real DMAbuf export path and sets `hdr.exported = 2`, `hdr.n_planes = 0`. The file resolves `eglExportDMABUFImage*` pointers at `tools/qnx_probes/qnx_dmabuf_export_producer.c:439-449`, but does not call them. The parent forwards data using plain `send()` at `tools/qnx_probes/qnx_dmabuf_export_producer.c:185` and `tools/qnx_probes/qnx_dmabuf_export_producer.c:194`, not `sendmsg(SCM_RIGHTS)`. This cannot satisfy the Phase 1B DMAbuf/fd-sharing milestone in `tools/qnx_probes/README.md:52-55` or the plan checklist item at `docs/qnx/ozone-out-of-process-gpu-plan.md:133`.
- `docs/qnx/ozone-out-of-process-gpu-plan.md:120-123` shows Phase 1B build commands without `-lsocket`. With the current source, those exact link commands fail on unresolved socket symbols. The source-file comments include `-lsocket` (`tools/qnx_probes/qnx_dmabuf_export_producer.c:15-19`, `tools/qnx_probes/qnx_dmabuf_import_consumer.c:17-21`), so the plan command needs a parent-owned update before another worker follows it literally.

### Note

- Current partial file inventory from `find tools/qnx_probes -maxdepth 1 -type f -printf '%p %s bytes\n' | sort`:
  - `tools/qnx_probes/README.md` — 2779 bytes
  - `tools/qnx_probes/qnx_dmabuf_export_producer.c` — 23308 bytes
  - `tools/qnx_probes/qnx_dmabuf_import_consumer.c` — 27588 bytes
  - `tools/qnx_probes/qnx_dmabuf_ipc.h` — 2578 bytes
  - `tools/qnx_probes/qnx_egl_extension_probe.c` — 17403 bytes
- `git status --short -- tools/qnx_probes ...` reported `?? tools/qnx_probes/`, so these probe files are currently untracked/unaccepted in this checkout.
- High-level intent appears to have drifted during recovery: the comments describe true DMAbuf export/import (`tools/qnx_probes/qnx_dmabuf_export_producer.c:6-13`, `tools/qnx_probes/qnx_dmabuf_import_consumer.c:6-15`), but the implementation has become a raw RGBA socket-transfer fallback (`tools/qnx_probes/qnx_dmabuf_export_producer.c:532-549`, `tools/qnx_probes/qnx_dmabuf_import_consumer.c:610-674`). This raw-pixel path may be useful as a Screen/GL smoke test, but it is not evidence that DMAbuf works.
- Likely reason the broad workers timed out: runtime debugging expanded beyond the original milestone. The producer comments show prior issues with slow Mesa/virgl GPU initialization and a socket race (`tools/qnx_probes/qnx_dmabuf_export_producer.c:83-100`), Mesa fatal/abort suppression attempts (`tools/qnx_probes/qnx_dmabuf_export_producer.c:226-235`), and a claimed pbuffer/EGLImage crash that blocks the intended export path (`tools/qnx_probes/qnx_dmabuf_export_producer.c:461-470`). Compile/link is not the likely stall; runtime recovery/debugging is.
- Potential runtime risk not validated here: the consumer still creates a pbuffer context at `tools/qnx_probes/qnx_dmabuf_import_consumer.c:532-559`, while the producer comments warn that QNX Mesa/virgl pbuffer paths can trigger internal Screen failures (`tools/qnx_probes/qnx_dmabuf_export_producer.c:461-466`). This may be harmless for the 4x4 consumer pbuffer, but it should be treated as unproven until a bounded QEMU run.

## Commands run

```sh
git status --short -- tools/qnx_probes docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md
```

Summary: reported `?? tools/qnx_probes/`.

```sh
find tools/qnx_probes -maxdepth 1 -type f -printf '%p %s bytes\n' | sort
```

Summary: listed the five files and sizes shown above.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -fsyntax-only -Wall tools/qnx_probes/qnx_dmabuf_export_producer.c 2>&1 | sed -n '1,160p'
```

Summary: inconclusive; this `qcc` invocation attempted to link and failed with `undefined reference to main`.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -fsyntax-only -Wall tools/qnx_probes/qnx_dmabuf_import_consumer.c 2>&1 | sed -n '1,200p'
```

Summary: inconclusive for the same reason; `qcc` attempted to link and failed with `undefined reference to main`.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
rm -f /tmp/qnx_dmabuf_export_producer.o
qcc -Vgcc_ntox86_64 -Wall -c -o /tmp/qnx_dmabuf_export_producer.o tools/qnx_probes/qnx_dmabuf_export_producer.c 2>&1 | sed -n '1,200p'
status=${PIPESTATUS[0]}
rm -f /tmp/qnx_dmabuf_export_producer.o
exit $status
```

Summary: compile passed with one warning: `g_verbose` defined but not used.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
rm -f /tmp/qnx_dmabuf_import_consumer.o
qcc -Vgcc_ntox86_64 -Wall -c -o /tmp/qnx_dmabuf_import_consumer.o tools/qnx_probes/qnx_dmabuf_import_consumer.c 2>&1 | sed -n '1,240p'
status=${PIPESTATUS[0]}
rm -f /tmp/qnx_dmabuf_import_consumer.o
exit $status
```

Summary: compile passed with no output.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
rm -f /tmp/qnx_dmabuf_export_producer
qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_producer tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2 2>&1 | sed -n '1,200p'
status=${PIPESTATUS[0]}
if [ -x /tmp/qnx_dmabuf_export_producer ]; then file /tmp/qnx_dmabuf_export_producer; fi
rm -f /tmp/qnx_dmabuf_export_producer
exit $status
```

Summary: link passed; output was an x86-64 QNX PIE executable.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
rm -f /tmp/qnx_dmabuf_import_consumer
qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_import_consumer tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2 2>&1 | sed -n '1,200p'
status=${PIPESTATUS[0]}
if [ -x /tmp/qnx_dmabuf_import_consumer ]; then file /tmp/qnx_dmabuf_import_consumer; fi
rm -f /tmp/qnx_dmabuf_import_consumer
exit $status
```

Summary: link passed; output was an x86-64 QNX PIE executable.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
rm -f /tmp/qnx_dmabuf_export_producer_nosocket
qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_producer_nosocket tools/qnx_probes/qnx_dmabuf_export_producer.c -lscreen -lEGL -lGLESv2 2>&1 | sed -n '1,120p'
status=${PIPESTATUS[0]}
rm -f /tmp/qnx_dmabuf_export_producer_nosocket
exit $status
```

Summary: link failed with unresolved `socket`, `bind`, `listen`, `accept`, `send`, and `recv`.

```sh
set -o pipefail
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
rm -f /tmp/qnx_dmabuf_import_consumer_nosocket
qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_import_consumer_nosocket tools/qnx_probes/qnx_dmabuf_import_consumer.c -lscreen -lEGL -lGLESv2 2>&1 | sed -n '1,120p'
status=${PIPESTATUS[0]}
rm -f /tmp/qnx_dmabuf_import_consumer_nosocket
exit $status
```

Summary: link failed with unresolved `socket`, `connect`, `recvmsg`, `recv`, and `send`.

```sh
git diff --cached --name-only -- tools/qnx_probes docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md
```

Summary: no staged files in the inspected scope.

## Smallest safe next subagent microtask

After the parent updates the plan, launch a **bounded runtime smoke microtask**; do not ask a worker to solve DMAbuf export, SCM_RIGHTS, raw-pixel composition, and crash/restart in one attempt.

**Files in scope:**

- `tools/qnx_probes/qnx_dmabuf_ipc.h`
- `tools/qnx_probes/qnx_dmabuf_export_producer.c`
- `tools/qnx_probes/qnx_dmabuf_import_consumer.c`

**Commands:**

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_producer \
  tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2
timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_probe.sock; ./qnx_dmabuf_import_consumer & sleep 1; ./qnx_dmabuf_export_producer; wait'
```

**Stop conditions:**

1. Stop and report immediately on any compile/link failure.
2. Stop and report if QEMU/run command reaches the 180-second timeout.
3. Stop and report if output contains `SKIPPED: EGLImage DMAbuf path BLOCKED`, `pbuffer crash`, `recvmsg() failed`, or `No texture available`; do not continue broad debugging.
4. If the raw-pixel path reaches `Screen composition MILESTONE: succeeded`, report it only as raw-pixel Screen/GL smoke evidence, not as DMAbuf evidence.
5. Do not attempt producer crash/restart in this microtask.

## Plan update required before implementation continues

- Add `-lsocket` to the Phase 1B producer and consumer build commands.
- Split Phase 1B acceptance into at least two explicit milestones:
  1. current raw-pixel/socket Screen/GL smoke validation, if the parent wants to preserve it;
  2. true DMAbuf export/import validation requiring `eglExportDMABUFImageMESA`, `sendmsg(SCM_RIGHTS)`, `recvmsg(SCM_RIGHTS)`, `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`, and `glEGLImageTargetTexture2DOES`.
- Mark the current partial files as **not accepted as Phase 1B DMAbuf evidence** until the producer actually exports at least one DMAbuf fd and the consumer imports it.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings cite tools/qnx_probes/qnx_dmabuf_export_producer.c:525-535, tools/qnx_probes/qnx_dmabuf_export_producer.c:185, tools/qnx_probes/qnx_dmabuf_import_consumer.c:579-674, docs/qnx/ozone-out-of-process-gpu-plan.md:120-133, and README milestone lines."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "git status --short -- tools/qnx_probes docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md",
      "result": "passed",
      "summary": "Reported untracked tools/qnx_probes/."
    },
    {
      "command": "find tools/qnx_probes -maxdepth 1 -type f -printf '%p %s bytes\\n' | sort",
      "result": "passed",
      "summary": "Listed five probe files and sizes."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -fsyntax-only -Wall tools/qnx_probes/qnx_dmabuf_export_producer.c",
      "result": "failed",
      "summary": "Inconclusive; qcc attempted to link and failed with undefined reference to main."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -fsyntax-only -Wall tools/qnx_probes/qnx_dmabuf_import_consumer.c",
      "result": "failed",
      "summary": "Inconclusive; qcc attempted to link and failed with undefined reference to main."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -Wall -c -o /tmp/qnx_dmabuf_export_producer.o tools/qnx_probes/qnx_dmabuf_export_producer.c",
      "result": "passed",
      "summary": "Object compile passed with one unused-variable warning."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -Wall -c -o /tmp/qnx_dmabuf_import_consumer.o tools/qnx_probes/qnx_dmabuf_import_consumer.c",
      "result": "passed",
      "summary": "Object compile passed with no output."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_producer tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Linked QNX x86_64 producer binary in /tmp, then removed it."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_import_consumer tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Linked QNX x86_64 consumer binary in /tmp, then removed it."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_producer_nosocket tools/qnx_probes/qnx_dmabuf_export_producer.c -lscreen -lEGL -lGLESv2",
      "result": "failed",
      "summary": "Failed on unresolved socket APIs, proving plan command needs -lsocket."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_import_consumer_nosocket tools/qnx_probes/qnx_dmabuf_import_consumer.c -lscreen -lEGL -lGLESv2",
      "result": "failed",
      "summary": "Failed on unresolved socket APIs, proving plan command needs -lsocket."
    },
    {
      "command": "git diff --cached --name-only -- tools/qnx_probes docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md",
      "result": "passed",
      "summary": "No staged files in inspected scope."
    }
  ],
  "validationOutput": [
    "Producer and consumer compile/link successfully with -lsocket.",
    "Producer and consumer fail to link without -lsocket.",
    "QEMU was intentionally not run."
  ],
  "residualRisks": [
    "Runtime behavior is unvalidated because QEMU was not run in this audit.",
    "The current producer skips true DMAbuf export and sends raw pixels instead.",
    "SCM_RIGHTS fd transfer is not validated because no fd is currently exported or sent.",
    "Consumer pbuffer creation may hit the same Mesa/QNX virgl fragility noted in the producer comments."
  ],
  "noStagedFiles": true,
  "diffSummary": "Created the requested read-only audit report; no source/probe files were edited.",
  "reviewFindings": [
    "blocker: tools/qnx_probes/qnx_dmabuf_export_producer.c:525-535 - true DMAbuf export is unconditionally skipped and raw pixels are sent instead.",
    "blocker: docs/qnx/ozone-out-of-process-gpu-plan.md:120-123 - current build commands omit required -lsocket for the new socket-using probes.",
    "note: tools/qnx_probes/qnx_dmabuf_import_consumer.c:532-559 - consumer still uses a pbuffer path despite producer comments warning of Mesa/QNX virgl pbuffer fragility.",
    "note: no QEMU run was performed per task instruction."
  ],
  "manualNotes": "The current partial files are useful as a possible raw-pixel Screen/GL smoke test, but should not be accepted as Phase 1B DMAbuf/EGLImage evidence."
}
```
