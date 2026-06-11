# ANGLE gpu_info_util needs the Linux SystemInfo sources on QNX

- Date: 2026-06-11
- Signature: `undefined reference to angle::GetSystemInfo(angle::SystemInfo*)` while linking `angle_perftests`
- Stage: compile
- Category: build-graph
- Scope: ANGLE gpu_info_util / perf test support

## Symptoms

- After the earlier `libangle_util.so` PIC issue was removed, `angle_perftests` advanced to a link failure with unresolved `angle::GetSystemInfo`, `GetCurrentSystemTime`, `GetPathSeparator`, `SetCWD`, and related helpers.

## Root cause

- ANGLE's `angle_gpu_info_util` only pulled in `SystemInfo_linux.cpp` on `is_linux || is_chromeos`.
- Once the experiment stopped treating QNX as `is_linux`, the Linux `SystemInfo` implementation fell out of the QNX graph.
- The perf-test support code still needs those helpers, and the Linux implementation is the closest compatible path.

## Fix pattern

- Keep QNX on the Linux-style ANGLE GPU info utility path when the code is really just shared cross-platform support.
- Only exclude Linux-only probing paths that have a known QNX incompatibility (for example, `libpci`).

## Applied change

- Added `is_qnx` to the `libangle_gpu_info_util_linux_sources` condition in `third_party/angle/BUILD.gn`.
- Registered the patch as `cef/patch/patches/qnx/chromium/angle_qnx_gpu_info_util_system_info.patch`.

## Verification

- `git apply --check` succeeds against a clean ANGLE checkout.
- `ninja -C out/qnx_release angle_perftests` now completes successfully.

## Files touched

- `third_party/angle/BUILD.gn`
- `cef/patch/patches/qnx/chromium/angle_qnx_gpu_info_util_system_info.patch`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/history/build-errors/compile/feature-guard/angle-use-libpci-disabled-on-qnx.md`
- `docs/qnx/history/build-errors/compile/build-graph/angle-perftests-glmark2-angle-qnx-skip.md`
