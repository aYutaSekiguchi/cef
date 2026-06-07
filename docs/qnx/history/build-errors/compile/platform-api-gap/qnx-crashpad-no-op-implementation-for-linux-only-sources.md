# QNX Crashpad no-op implementation for Linux-only sources

- Date: 2026-06-07
- Signature: fatal error: 'sys/prctl.h' file not found / error: Port. / NativeCPUContext
- Stage: compile
- Category: platform-api-gap
- Scope: components/crash/core/app

## Symptoms

- `components/crash/core/app/crashpad_linux.cc:8:10: fatal error: 'sys/prctl.h' file not found`
- `components/crash/core/app/crashpad.cc:103:2: error: Port.`
- `third_party/crashpad/crashpad/util/misc/capture_context.h:84:21: error: unknown type name 'NativeCPUContext'`
- The failure happened while building `components/crash/core/app:app` during QNX bootstrap / cefsimple build.

## Root cause

- QNX sets `is_qnx = true` and also `is_linux = true` in GN, so the crash component follows the Linux build graph.
- `components/crash/core/app/BUILD.gn` therefore pulled in Linux-only Crashpad sources (`crashpad_linux.cc`) and the generic `crashpad.cc` implementation.
- `crashpad.cc` contains an explicit `#error Port.` for unsupported platforms, and the Linux code path expects headers/APIs that QNX does not provide (`sys/prctl.h`, `NativeCPUContext`, Linux-only crashpad plumbing).

## Fix pattern

- When a Linux-only Crashpad path is not required on QNX, replace it with a QNX-specific no-op implementation instead of trying to force Linux compatibility.
- Keep the public API intact, but make initialization and upload/report paths return inert values.
- Preserve `base/debug/stack_trace.h` for local stack traces; omit Crashpad dump/upload plumbing.

## Applied change

- Added a QNX-specific implementation file: `components/crash/core/app/crashpad_qnx.cc`.
- Updated `components/crash/core/app/BUILD.gn` to use `crashpad_qnx.cc` on QNX and to skip `crashpad_linux.cc` there.
- Saved the change as a managed CEF patch: `patch/patches/qnx/chromium/crashpad_qnx.patch`.
- Added the new source file under `patch/qnx/chromium/new_files/components/crash/core/app/crashpad_qnx.cc` so bootstrap can install it into the Chromium tree.
- Wired `cef/tools/cef_create_projects_qnx.sh` to apply the patch after new_files are installed.
- Commit: `f05f105e9`.

## Verification

- `./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800` completed with `168 patches total (160 applied, 8 skipped, 0 failed)`.
- `ninja -C out/qnx_release components/crash/core/app:app` no longer failed on Crashpad; the next blocker moved to `sandbox/linux/services/*` missing QNX headers such as `sys/syscall.h`.
- This confirmed the crashpad-specific blocker was removed and the build advanced to a different subsystem.

## Files touched

- `components/crash/core/app/BUILD.gn`
- `components/crash/core/app/crashpad_qnx.cc`
- `patch/patches/qnx/chromium/crashpad_qnx.patch`
- `patch/qnx/chromium/new_files/components/crash/core/app/crashpad_qnx.cc`
- `tools/cef_create_projects_qnx.sh`

## Related notes

- `docs/qnx/history/archive/phase1/qnx_toolchain_is_linux.md`
- `docs/qnx/history/archive/phase1/summary.md`
- `docs/qnx/history/archive/repro-2026-05-12.md`
