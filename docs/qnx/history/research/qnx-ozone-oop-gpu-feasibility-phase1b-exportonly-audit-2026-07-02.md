# QNX Ozone OOP-GPU Phase 1B-exportonly audit

Date: 2026-07-02
Scope: read-only audit of the partial `tools/qnx_probes/qnx_dmabuf_export_only_probe.c`; compile/link only; no QEMU run.

## Review

### Correct

- `docs/qnx/ozone-out-of-process-gpu-plan.md:159-174` already names the export-only probe, compile/run command, and stop conditions. The current checklist still leaves the actual export-only build/run unchecked at `docs/qnx/ozone-out-of-process-gpu-plan.md:183`.
- `tools/qnx_probes/qnx_dmabuf_export_only_probe.c` is a standalone probe, not GN/Chromium/Ozone wiring. Its file header states the intended export-only scope and no IPC/import/display at `tools/qnx_probes/qnx_dmabuf_export_only_probe.c:1-40`.
- The file is complete enough to compile/link with the planned libraries. Command used:

  ```sh
  cd /home/yuta/chromium/src/cef
  source ../out/qnx_release/qnx_env.sh
  export PATH="$QNX_HOST/usr/bin:$PATH"
  qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_only_probe_audit \
    tools/qnx_probes/qnx_dmabuf_export_only_probe.c -lscreen -lEGL -lGLESv2
  ```

  Result: passed, exit 0, no stdout/stderr. A second bounded compile with `-Wall -Wextra` also passed with no diagnostics.

### Current partial file inventory

- `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-2026-07-02.md` is absent.
- `git status --short --untracked-files=all -- tools/qnx_probes docs/qnx/history/research docs/qnx/ozone-out-of-process-gpu-plan.md` showed these relevant files as untracked: the plan file, prior Phase 1A/1B reports, `tools/qnx_probes/README.md`, `qnx_egl_extension_probe.c`, `qnx_dmabuf_ipc.h`, `qnx_dmabuf_export_producer.c`, `qnx_dmabuf_import_consumer.c`, `qnx_scmrights_probe.c`, and `qnx_dmabuf_export_only_probe.c`.
- `tools/qnx_probes` contains seven files: `README.md`, `qnx_egl_extension_probe.c`, `qnx_dmabuf_ipc.h`, `qnx_dmabuf_export_producer.c`, `qnx_dmabuf_import_consumer.c`, `qnx_scmrights_probe.c`, and `qnx_dmabuf_export_only_probe.c`.
- Line counts: `qnx_dmabuf_export_only_probe.c` 695 lines; `README.md` 63 lines; existing producer/consumer/scmrights sources are also present but were not compiled in this audit.

### Blocker

- **high: `tools/qnx_probes/qnx_dmabuf_export_only_probe.c:495-505` — the preferred non-pbuffer Path A appears semantically malformed.** The `eglCreateDRMImageMESA` attribute list uses `0x31D2` (`EGL_DRM_BUFFER_FORMAT_ARGB32_MESA`) as the first attribute key, then raw `64, 64` values, then `0x31D4`, `0x31D1`, `0x31D0`, `0x31D3`. In the QNX EGL header, `0x31D0` is `EGL_DRM_BUFFER_FORMAT_MESA`, `0x31D1` is `EGL_DRM_BUFFER_USE_MESA`, and `0x31D2` is the ARGB32 format value. This likely makes Path A fail before any safe DMAbuf export attempt, pushing runtime into Path B.
- **high: `tools/qnx_probes/qnx_dmabuf_export_only_probe.c:365-386` and `535-647` — the file is not strictly safe/non-pbuffer as-is.** It attempts a non-GL/non-Screen Path A at `490-524`, but before that it unconditionally creates a pbuffer surface and makes a GLES context for extension probing. If Path A fails, it then enters the documented `EGL_GL_TEXTURE_2D_KHR` fallback, which the file itself identifies as a known Mesa/QNX virgl SIGSEGV path at `477-482` and `672-674`. The SIGSEGV/SIGBUS guard only wraps `eglCreateImageKHR` at `604-637`; it does not guard pbuffer creation/make-current, and SIGBUS is not restored.

### Note

- The likely timeout cause was not compile/link. The audited `qcc` command completed successfully and silently within the bounded audit run. Based on the remaining file state, the previous worker most likely spent the allotted time producing a large 695-line multi-path probe and did not reach compile/run/report.
- No QEMU execution was performed in this audit, per task constraint. Therefore runtime extension presence, actual Path A behavior, and actual DMAbuf fd export remain unvalidated.
- The file does attempt the right high-level milestone (`eglExportDMABUFImageMESA` and fd/fstat/fcntl reporting at `tools/qnx_probes/qnx_dmabuf_export_only_probe.c:146-236`), but the current Path A attribute issue and pbuffer-before-Path-A behavior should be fixed before treating a runtime run as the safe export-only milestone.

## Smallest safe next microtask

Do **not** broaden to IPC/import/display. First make the export-only run truly non-pbuffer-first, then run once.

1. Minimal source-hardening microtask:
   - Correct the `eglCreateDRMImageMESA` attribute list to use real key/value pairs (`EGL_WIDTH`, `EGL_HEIGHT`, `EGL_DRM_BUFFER_FORMAT_MESA`, `EGL_DRM_BUFFER_FORMAT_ARGB32_MESA`, `EGL_DRM_BUFFER_USE_MESA`, selected use flags, `EGL_NONE`).
   - Move the GLES extension/pbuffer setup so it happens only after Path A is unavailable/failed, or disable Path B by default. If Path A cannot be attempted or fails, stop/report instead of entering pbuffer fallback unless the plan explicitly approves that risk.
   - Compile only:

     ```sh
     cd /home/yuta/chromium/src/cef
     source ../out/qnx_release/qnx_env.sh
     export PATH="$QNX_HOST/usr/bin:$PATH"
     qcc -Wall -Wextra -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_only_probe_safe \
       tools/qnx_probes/qnx_dmabuf_export_only_probe.c -lscreen -lEGL -lGLESv2
     ```

   - Stop conditions: stop on any compile/link diagnostic; stop if the source still calls `eglCreatePbufferSurface` before completing/aborting Path A.

2. Separate bounded runtime microtask after the above hardening and plan update:

   ```sh
   cd /home/yuta/chromium/src/cef
   source ../out/qnx_release/qnx_env.sh
   export PATH="$QNX_HOST/usr/bin:$PATH"
   qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_only_probe \
     tools/qnx_probes/qnx_dmabuf_export_only_probe.c -lscreen -lEGL -lGLESv2
   timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_dmabuf_export_only_probe
   ```

   Stop conditions: stop/report if required extension/function pointers are absent; stop/report immediately on first successful exported fd with format/stride/modifier/fstat/fcntl metadata; stop/report on Path A failure without falling into pbuffer unless explicitly approved; stop/report any crash/hang without debugging broader IPC/import/display.

## Plan updates required before continuing

- Add this audit report as Phase 1B-exportonly audit evidence.
- Record that current `qnx_dmabuf_export_only_probe.c` has compile-only evidence, not runtime/export evidence.
- Split the next work into a source-hardening microtask and a separate bounded QEMU run microtask, or explicitly approve the current file's pbuffer fallback risk before running it.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Reported concrete findings with paths and line numbers: compile success, malformed Path A attribute list at tools/qnx_probes/qnx_dmabuf_export_only_probe.c:495-505, and pbuffer-risk path at tools/qnx_probes/qnx_dmabuf_export_only_probe.c:365-386 and 535-647."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-audit-2026-07-02.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "git status --short --untracked-files=all -- tools/qnx_probes docs/qnx/history/research docs/qnx/ozone-out-of-process-gpu-plan.md",
      "result": "passed",
      "summary": "Showed relevant plan/docs/probe files as untracked, including qnx_dmabuf_export_only_probe.c."
    },
    {
      "command": "find tools/qnx_probes -maxdepth 1 -type f -printf '%p\\n' | sort",
      "result": "passed",
      "summary": "Listed seven probe/README files under tools/qnx_probes."
    },
    {
      "command": "qcc -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_only_probe_audit tools/qnx_probes/qnx_dmabuf_export_only_probe.c -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Compile/link succeeded with no stdout/stderr."
    },
    {
      "command": "qcc -Wall -Wextra -Vgcc_ntox86_64 -o /tmp/qnx_dmabuf_export_only_probe_audit_wall tools/qnx_probes/qnx_dmabuf_export_only_probe.c -lscreen -lEGL -lGLESv2",
      "result": "passed",
      "summary": "Warning-enabled compile/link also succeeded with no diagnostics."
    },
    {
      "command": "grep -n 'EGL_DRM_BUFFER_FORMAT_MESA\\|EGL_DRM_BUFFER_FORMAT_ARGB32_MESA\\|EGL_DRM_BUFFER_USE_MESA\\|EGL_DRM_BUFFER_MESA\\|EGL_DRM_BUFFER_STRIDE_MESA' $QNX_TARGET/usr/include/EGL/eglext.h",
      "result": "passed",
      "summary": "Confirmed QNX EGL_MESA_drm_image constant meanings used to audit the Path A attribute list."
    },
    {
      "command": "git status --short --untracked-files=all -- docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-audit-2026-07-02.md tools/qnx_probes/qnx_dmabuf_export_only_probe.c docs/qnx/ozone-out-of-process-gpu-plan.md && git diff --cached --name-only",
      "result": "passed",
      "summary": "Verified only untracked relevant files are shown and no staged files are present."
    }
  ],
  "validationOutput": [
    "No QEMU run was performed.",
    "Compile output summary: no stdout/stderr; exit 0."
  ],
  "residualRisks": [
    "Runtime behavior is unvalidated because QEMU execution was disallowed for this audit.",
    "Current source likely falls through from a malformed Path A into a pbuffer/GL texture path that is known risky in Mesa/QNX virgl."
  ],
  "noStagedFiles": true,
  "diffSummary": "Added this audit report only; no source files were modified.",
  "reviewFindings": [
    "high: tools/qnx_probes/qnx_dmabuf_export_only_probe.c:495-505 - preferred non-pbuffer EGL_MESA_drm_image attribute list appears malformed and likely fails at runtime.",
    "high: tools/qnx_probes/qnx_dmabuf_export_only_probe.c:365-386 and 535-647 - probe still creates/uses pbuffer path before or after Path A, risking the known Mesa/QNX virgl pbuffer/eglCreateImageKHR crash path."
  ],
  "manualNotes": "Report written to the authoritative output path. No source files or plan file were edited."
}
```
