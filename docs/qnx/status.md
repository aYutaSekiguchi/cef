# QNX Port Status

## Current validated baseline

The current QNX port baseline has been validated against Chromium compatibility tag `147.0.7727.147`.

### Last validated

| Item | Value |
|---|---|
| validation date | 2026-05-25 |
| checkout model | clean Chromium checkout at `147.0.7727.147` |
| source sync | `cef/tools/qnx_sync_sources.sh` |
| bootstrap | `cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>` |
| build | `./out/qnx_release/ninja_qnx.sh base_unittests` |
| broad QEMU run | `./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"` |
| focused verification | `CommandLineTest.CommandLineConstructor`, `ProcessMemoryDumpTest.CountResidentBytes`, and related regression checks |
| build dir | `out/qnx_release` |
| runner logs | `out/qnx_release/qnx_run_*.log*` |

### Verified results

- `cef/tools/cef_create_projects_qnx.sh` successfully bootstraps a clean checkout.
- `out/qnx_release/ninja_qnx.sh base_unittests` builds successfully.
- broad QEMU `base_unittests` runs pass with the standard QNX exclusion filter.
- targeted regressions such as `CommandLineTest.CommandLineConstructor` and `ProcessMemoryDumpTest.CountResidentBytes` pass.

## Current accepted exclusions

These exclusions are currently considered environment-specific and are automatically applied by `cef/tools/qnx_run_test.sh`:

| Test pattern | Reason |
|---|---|
| `StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree` | QNX signal-handler symbolization path is not yet async-signal-safe enough for this test. |
| `ImportantFileWriterTest.FailedWriteWithObserver` | QNX `/tmp` and path-handling semantics do not match the test's expected failure mode. |
| `*AnyCriticalThreadHung*` | Broad-run-only flake under QEMU load; isolated reruns have not produced a stable product bug. |

## Current next work

A fresh session should normally work in this order:

1. **Cross-machine validation**
   - rerun the validated bootstrap/build/test flow on the target machine
   - confirm the same baseline before changing code
2. **Broaden test coverage beyond `base_unittests`**
   - identify the next most valuable unit/integration target
   - prefer focused bring-up over broad speculative changes
3. **Reduce exclusions only when necessary**
   - do not reopen the three accepted exclusions unless they block a concrete goal
4. **Keep CEF-managed ownership intact**
   - any durable fix should land in `cef/patch/...` or `cef/patch/qnx/chromium/new_files/...`
5. **Fresh-environment bootstrap follow-up**
   - the previous `libclang_rt.builtins.a` blocker is now addressed by generating QNX `clang_rt.builtins` during the build into `out/qnx_release/qnx_clang_rt/...`
   - run `cef/tools/qnx_sync_sources.sh` before `cef_create_projects_qnx.sh` so DEPS-managed QNX sources such as `third_party/epoll/src` are present
   - after the epoll source-sync fix, the next fresh-environment blocker is `-leventfd` link resolution, not missing epoll sources or LLVM builtins

### Avoid spending time on these unless explicitly required

- replacing accepted QEMU-only exclusions with invasive new workarounds
- ad-hoc root-tree fixes that are not captured in CEF-managed patches
- large platform cleanups before the next concrete failing target is identified

## Standard bootstrap flow

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_sync_sources.sh
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh base_unittests
```

## Standard QEMU test flow

```bash
cd <CHROMIUM_SRC_ROOT>
sudo ./cef/tools/qnx_setup_env.sh
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

## Cross-machine verification checklist

Use this checklist when validating on another machine:

1. Install QNX SDP 8 and confirm `QNX_HOST` / `QNX_TARGET` are valid under your local SDK root.
2. Start from a clean Chromium checkout at compatibility tag `147.0.7727.147`.
3. Run `cef/tools/qnx_sync_sources.sh`.
4. Run `cef/tools/cef_create_projects_qnx.sh`.
5. Build `base_unittests` using `out/qnx_release/ninja_qnx.sh`.
6. Run `sudo ./cef/tools/qnx_setup_env.sh`.
7. Run `./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"`.
8. If any failures appear, check `fixes-and-decisions.md` first, then `history/` for deeper investigations.

## Preserved historical branches

These branches are retained as historical references and should not be deleted:

- `qnx-cef-consolidate`
- `qnx-cef-patched-baseline`

## Patch ownership model

The intended model is:

- keep QNX changes in `cef/patch/...`
- keep new files in `cef/patch/qnx/chromium/new_files/...`
- regenerate a working tree from `cef/tools/cef_create_projects_qnx.sh`

Avoid relying on ad-hoc local root-tree edits as the long-term source of truth.
