# ANGLE on QNX needs executable-path lookup via CHROME_EXE_PATH or /proc/self/exefile

- Date: 2026-06-05
- Signature: SystemUtils.ExecutablePath failed or angle_end2end_tests could not find expectations
- Stage: test
- Category: runtime-assumption
- Scope: ANGLE runtime path resolution

## Symptoms

- Several `angle_unittests` path tests failed.
- `angle_end2end_tests` aborted before normal test execution because it could not find its expectations file.

## Root cause

- The Linux executable-path implementation used `/proc/self/exe`, which is not the right contract on QNX.
- QNX test infrastructure already exported the binary path through `CHROME_EXE_PATH`.

## Fix pattern

- When a port deliberately reuses a Linux runtime path helper, add a QNX-specific executable-path fallback rather than forcing all callers to special-case missing paths.

## Applied change

- Extended `system_utils_linux.cpp` to try `CHROME_EXE_PATH`, then `/proc/self/exefile`, and keep `/proc/self/exe` for non-QNX Linux.

## Verification

- The previous `SystemUtils.*` and helper-binary lookup failures passed.
- `angle_end2end_tests` reached normal gtest execution instead of failing during expectations lookup.

## Files touched

- `third_party/angle/src/common/system_utils_linux.cpp`
- `cef/patch/patches/qnx/chromium/angle_qnx_minimal_linux_headless.patch`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/angle-minimal-linux-headless-qnx-port.md`
