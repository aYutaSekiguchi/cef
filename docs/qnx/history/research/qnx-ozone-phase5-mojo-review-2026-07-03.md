## Review

### Correct
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:15-31` correctly models each DMAbuf plane with `handle<platform> fd` plus stride/offset/size metadata. This matches the design IPC flow in `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:210-213` and the design rationale for Mojo platform handle transport in `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:355-368`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:42-60` includes the expected frame generation, dimensions, FourCC, modifier, and plane array metadata; this matches the Phase 2 buffer-flow description in `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:210-213`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:76` provides a `SubmitFrame(...) => (bool accepted, string diagnostic)` ACK/error callback, matching `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:511-519`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:90-93` uses `gfx.mojom.Size` for attach/resize size, and `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:13` imports `ui/gfx/geometry/mojom/geometry.mojom`; this matches the design sketch in `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:408-414`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:43-57` wires the mojom target into the QNX source set, and `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:69-73` includes deps covering the current `gfx.mojom.Size` import and a future `gfx.mojom.AcceleratedWidget` import.
- The Phase 5 plan explicitly starts with QNX-local Mojo interfaces before GPU producer implementation (`docs/qnx/ozone-out-of-process-gpu-plan.md:366-390`), so the substep scope is appropriately bounded.

### Fixed
- None. Review-only task; no source files were edited. This report file was written to the requested output path.

### Blocker
- blocker: `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:40`, `:80`, `:90`, `:93`, `:97` use raw `uint32 widget` parameters instead of `gfx.mojom.AcceleratedWidget`. The Phase 2 design sketch imports `ui/gfx/mojom/accelerated_widget.mojom` (`docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:383-385`) and uses `gfx.mojom.AcceleratedWidget` for `QnxDmaBufFrame.widget`, `ReportProducerLost`, `AttachWidget`, `ResizeWidget`, and `DetachWidget` (`docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:393-416`). Chromium also already defines this mojom type at `ui/gfx/mojom/accelerated_widget.mojom:8`. Because the review task specifically called out `gfx.mojom.AcceleratedWidget`, the schema does not meet the requirement as-is. Expected fix: import `ui/gfx/mojom/accelerated_widget.mojom` and replace the widget fields/parameters with `gfx.mojom.AcceleratedWidget`.

### Note
- high: `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:66-73` and `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn:14-23` define two separate mojom targets over the same `qnx_gpu.mojom` source. The parent target is `:qnx_gpu_mojom`; the subdirectory target is `//ui/ozone/platform/qnx/mojom:mojom`. The QNX source set depends only on the parent target (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:55-56`), making `mojom/BUILD.gn` unused in the current wiring. If both targets are later pulled into the GN graph, they are likely to generate the same `gen/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom.*` outputs. Prefer one pattern: either keep only the parent target, or follow existing Ozone platform style and depend on the subdir target.
- medium: Reported validation commands in `docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md:96-118` are not fully credible/reproducible as written. From `/home/yuta/chromium/src`, `../out/qnx_release/args.gn` does not exist; the local file is `/home/yuta/chromium/src/out/qnx_release/args.gn`. The documented `cp -r ... ui/ozone/platform/qnx/` also needs the destination directory to exist first, but the current root source tree has no `ui/ozone/platform/qnx/` directory. Finally, `ls gen/ui/ozone/platform/qnx/mojom/` should point under the output directory (`out/qnx_phase5_mojo/gen/...`) unless the command first changes directory.
- medium: I could not corroborate the reported retained validation artifacts. `ninja -C /home/yuta/chromium/src/out/qnx_phase5_mojo -t targets` fails because `build.ninja` is absent, and `find /home/yuta/chromium/src/out/qnx_phase5_mojo -path '*qnx_gpu*' -o -path '*libqnx_ui.a'` found no matching artifacts. This does not prove the validation never ran, but it means the current checkout does not support the report's claim that build artifacts were retained (`docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md:157-161`).
- low: The report line counts are stale: `docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md:19-20` says `mojom/BUILD.gn` has 21 lines and `qnx_gpu.mojom` has 126 lines; `wc -l` shows 24 and 98 respectively.

### Likely GN/Mojo compile risks
- The current mojom syntax for `handle<platform>` and `gfx.mojom.Size` is consistent with Chromium patterns and should be compile-safe in isolation.
- Changing widget fields to `gfx.mojom.AcceleratedWidget` should be a low-risk Mojo change if `import "ui/gfx/mojom/accelerated_widget.mojom";` is added; `//ui/gfx/mojom` is already in both BUILD files' `public_deps`.
- The duplicate parent/subdir mojom target layout is the main GN risk. It should be consolidated before relying on independent target builds or before future code depends on the subdir target.

### Commands run
- `nl -ba patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn | sed -n '1,140p'`
- `nl -ba patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn | sed -n '1,120p'`
- `nl -ba patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom | sed -n '1,160p'`
- `nl -ba docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md | sed -n '200,540p'`
- `nl -ba docs/qnx/ozone-out-of-process-gpu-plan.md | sed -n '330,430p'`
- `nl -ba docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md | sed -n '1,220p'`
- `grep`/`read` checks for `ui/gfx/mojom/accelerated_widget.mojom`, `ui/gfx/mojom/BUILD.gn`, and existing Ozone mojom usage.
- `ninja -C /home/yuta/chromium/src/out/qnx_phase5_mojo -t targets`
- `find /home/yuta/chromium/src/out/qnx_phase5_mojo -path '*qnx_gpu*' -o -path '*libqnx_ui.a'`
- `test -f /home/yuta/chromium/out/qnx_release/args.gn; test -f /home/yuta/chromium/src/out/qnx_release/args.gn`
- `git status --short -- <review-scope-files>`
- `wc -l patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md`

### Acceptance assessment
Do not accept Phase 5 Mojo substep as-is. The schema should be updated to use `gfx.mojom.AcceleratedWidget` for widget IDs, and the duplicate/conflicting mojom target layout should be consolidated. After those fixes, rerun `gn gen` and build the selected mojom target plus the QNX platform target using corrected validation commands.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete blocker/high/medium/low findings are listed with file paths and line numbers; residual GN/Mojo and validation risks are documented."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-phase5-mojo-review-2026-07-03.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "nl -ba patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn | sed -n '1,140p'",
      "result": "passed",
      "summary": "Inspected parent QNX BUILD.gn with line numbers."
    },
    {
      "command": "nl -ba patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn | sed -n '1,120p'",
      "result": "passed",
      "summary": "Inspected mojom subdir BUILD.gn with line numbers."
    },
    {
      "command": "nl -ba patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom | sed -n '1,160p'",
      "result": "passed",
      "summary": "Inspected qnx_gpu.mojom schema with line numbers."
    },
    {
      "command": "nl -ba docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md | sed -n '200,540p'",
      "result": "passed",
      "summary": "Compared implementation against design IPC and handshake sections."
    },
    {
      "command": "ninja -C /home/yuta/chromium/src/out/qnx_phase5_mojo -t targets",
      "result": "failed",
      "summary": "Output directory is not currently a Ninja build dir because build.ninja is missing."
    },
    {
      "command": "find /home/yuta/chromium/src/out/qnx_phase5_mojo -path '*qnx_gpu*' -o -path '*libqnx_ui.a'",
      "result": "passed",
      "summary": "No qnx_gpu or libqnx_ui artifacts found in the reported retained output directory."
    },
    {
      "command": "test -f /home/yuta/chromium/out/qnx_release/args.gn; test -f /home/yuta/chromium/src/out/qnx_release/args.gn",
      "result": "passed",
      "summary": "Confirmed documented ../out path is absent while src/out args.gn exists."
    },
    {
      "command": "wc -l patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md",
      "result": "passed",
      "summary": "Confirmed documentation line-count mismatch."
    }
  ],
  "validationOutput": [
    "Current schema uses handle<platform> and gfx.mojom.Size correctly, but uses uint32 widget instead of gfx.mojom.AcceleratedWidget.",
    "Current out/qnx_phase5_mojo cannot be used to verify reported builds because build.ninja and qnx_gpu artifacts are absent."
  ],
  "residualRisks": [
    "No long build was run by this review; compile status should be revalidated after schema and BUILD target fixes.",
    "Duplicate mojom target behavior was assessed by GN/Mojo output reasoning; a corrected GN graph should be regenerated to confirm no duplicate outputs."
  ],
  "noStagedFiles": true,
  "diffSummary": "Review-only assessment; no code diffs applied. Required review report file written.",
  "reviewFindings": [
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:40 - widget is uint32 instead of required gfx.mojom.AcceleratedWidget; same issue at lines 80, 90, 93, and 97.",
    "high: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:66 and patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn:14 define duplicate mojom targets over the same source.",
    "medium: docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md:96 - validation commands contain wrong output path assumptions and are not reproducible as written.",
    "medium: out/qnx_phase5_mojo lacks build.ninja and qnx_gpu artifacts, so reported retained build artifacts could not be independently corroborated.",
    "low: docs/qnx/history/research/qnx-ozone-phase5-mojo-interfaces-2026-07-03.md:19 - reported line counts are stale."
  ],
  "manualNotes": "Acceptance recommendation: needs fixes before accepting Phase 5 Mojo substep."
}
```
