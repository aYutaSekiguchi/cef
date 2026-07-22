# QNX Port Status

## Current documentation snapshot

This page is the top-level snapshot for the QNX port.  Detailed incident
write-ups remain under `docs/qnx/history/build-errors/`; this page only carries
state that is useful for the next bootstrap/build/test session.

| Item | Value |
|---|---|
| snapshot date | 2026-07-22 |
| compatibility tag | `147.0.7727.147` |
| checkout model | clean Chromium checkout plus CEF-managed QNX patches/new files |
| source sync | `cef/tools/qnx_sync_sources.sh -f -R` |
| bootstrap | `cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>` |
| build dir | `out/qnx_release` |
| build wrapper | `./out/qnx_release/ninja_qnx.sh <target>` |
| QEMU runner | `./cef/tools/qnx_run_test.sh` module dispatcher |
| runner logs | `out/qnx_release/qnx_run_*.log*` |

## Current validation matrix

| Area | Current status | Notes / next use |
|---|---|---|
| Clean bootstrap | Latest recorded clean bootstrap reaches `gn gen` with `0 failed` patches after the QNX patch-stack refreshes. | Always run `cef/tools/qnx_sync_sources.sh -f -R` before bootstrap when validating from a refreshed tree. |
| `base_unittests` | Validated as the stable broad baseline with the standard QNX exclusion filter. | Use this as the first regression check after bootstrap/toolchain changes. |
| `ceftests` build | `./out/qnx_release/ninja_qnx.sh ceftests` succeeds from a clean bootstrap after the CEF resource glue, fontconfig, and V8 registration fixes. | This is the active CEF API validation target. |
| `ceftests` runtime | Focused headless QNX runs now pass for the recently fixed areas: `DownloadTest.*`, `AxViewportCollapseTest.*`, `FindHandlerTest.*`, `DraggableRegionsTest.*`, and the repaired CORS groups. | Full-suite broad status is still a bring-up track; do not describe the whole `ceftests` suite as fully passing yet. |
| `cefsimple` / `cefsimple_capi` | `cefsimple` builds and displays compositor output across the full 1280x768 QEMU Screen display when launched with `--start-maximized`; physical-mouse input reaches web content. | Use `tests/cefsimple/qnx_input_probe.html` for coordinate regressions. Right-click/context-menu stability remains a separate follow-up. |
| `v8_unittests` | Uses the QNX per-test runner and status-file skips for the managed residual set. | Run via `cef/tools/qnx_run_test.sh --v8`; do not run the whole suite in one process. |
| ANGLE | `angle_unittests` reached `5989 PASS / 0 FAIL`; `angle_end2end_tests` is blocked by missing Vulkan-capable GPU/surface support in the QEMU environment. | Resume end-to-end work only when the guest graphics environment changes. |

## Current accepted broad-run exclusions

These exclusions are currently considered environment-specific for the
`base_unittests` broad run and are automatically applied by
`cef/tools/qnx_run_test.sh`'s default `--base` module:

| Test pattern | Reason |
|---|---|
| `StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree` | QNX signal-handler symbolization path is not yet async-signal-safe enough for this test. |
| `ImportantFileWriterTest.FailedWriteWithObserver` | QNX `/tmp` and path-handling semantics do not match the test's expected failure mode. |
| `*AnyCriticalThreadHung*` | Broad-run-only flake under QEMU load; isolated reruns have not produced a stable product bug. |

Module-specific skips or residuals for `ceftests`, `v8_unittests`, ANGLE, and
SwiftShader are tracked in their structured notes and runner modules, not in
this base broad-run exclusion table.

## Current next work

A fresh session should normally work in this order:

1. **Reconfirm clean bootstrap reproducibility**
   - run `cef/tools/qnx_sync_sources.sh -f -R` when refreshing a QNX working tree
   - run `cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>`
   - stop on any failed patch and repair the CEF-managed patch stack before continuing
2. **Run the stable baseline**
   - build `base_unittests`
   - run the default QNX runner (`--base`) with the accepted exclusion filter
3. **Continue CEF API validation**
   - build `ceftests`
   - run focused headless groups before attempting a full broad run
   - known follow-up area: `FrameHandlerTest` cross-origin ordering on QNX/OOP renderer paths
4. **Continue sample/runtime validation as needed**
   - use the physical-input probe after QNX Screen, window-state, GPU-buffer, or input-routing changes
   - investigate the remaining right-click/context-menu stop independently of the resolved display-coordinate issue
5. **Keep durable ownership intact**
   - any fix must be captured in `cef/patch/...` or `cef/patch/qnx/chromium/new_files/...`
   - every patch file that should apply during bootstrap must be registered in `cef/patch/patch.cfg`
6. **Do not reopen accepted environment exclusions unless they block a concrete goal**
   - prefer focused tests and structured notes over broad speculative changes

### Fresh-environment prerequisites

- Install QNX SDP 8 and confirm `QNX_HOST` / `QNX_TARGET` are valid.
- Install the QNX Software Center package that provides Notification FD
  Interfaces; builds that link eventfd users require `libeventfd`.
- Use the in-tree/bundled fontconfig path (`use_bundled_fontconfig = true`)
  because QNX SDP 8's system fontconfig is not usable for Chromium 147's
  current fontconfig symbols.
- Do not treat missing epoll sources or LLVM builtins as current blockers:
  DEPS-managed QNX sources are synced by `qnx_sync_sources.sh`, and QNX
  `clang_rt.builtins` is generated into `out/qnx_release/qnx_clang_rt/...`.

### Avoid spending time on these unless explicitly required

- replacing accepted QEMU-only exclusions with invasive new workarounds
- ad-hoc root-tree fixes that are not captured in CEF-managed patches
- treating Linux sandbox sources as QNX-compatible instead of excluding them
  until a real QNX sandbox exists
- large platform cleanups before the next concrete failing target is identified

## Standard bootstrap flow

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_sync_sources.sh -f -R
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh base_unittests
```

## Standard QEMU test flow

```bash
cd <CHROMIUM_SRC_ROOT>
sudo ./cef/tools/qnx_setup_env.sh
./cef/tools/qnx_run_test.sh --base --timeout 7200 --kill-existing "*"
```

Focused `ceftests` example:

```bash
./out/qnx_release/ninja_qnx.sh ceftests
./cef/tools/qnx_run_test.sh --ceftests --timeout 600 --kill-existing \
  "DownloadTest.*"
```

## Cross-machine verification checklist

Use this checklist when validating on another machine:

1. Install QNX SDP 8 and confirm `QNX_HOST` / `QNX_TARGET` are valid under your local SDK root.
2. Install the SDP package that provides `libeventfd` / Notification FD Interfaces.
3. Start from a clean Chromium checkout at compatibility tag `147.0.7727.147`.
4. Run `cef/tools/qnx_sync_sources.sh -f -R`.
5. Run `cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>`.
6. Build `base_unittests` using `out/qnx_release/ninja_qnx.sh`.
7. Run `sudo ./cef/tools/qnx_setup_env.sh`.
8. Run `./cef/tools/qnx_run_test.sh --base --timeout 7200 --kill-existing "*"`.
9. If validating CEF API coverage, build `ceftests` and run focused `--ceftests` groups before a broad run.
10. If any failures appear, check `build-error-index.md` first, then `history/build-errors/` for the concrete incident note.

## Patch ownership model

The intended model is:

- keep QNX changes in `cef/patch/...`
- keep new files in `cef/patch/qnx/chromium/new_files/...`
- register every bootstrap-applied patch in `cef/patch/patch.cfg`
- regenerate a working tree from `cef/tools/cef_create_projects_qnx.sh`

Avoid relying on ad-hoc local root-tree edits as the long-term source of truth.
