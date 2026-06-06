# qnx_std_polyfill.h must end with a newline to avoid warning floods

- Date: 2026-06-03
- Signature: qnx_std_polyfill.h warning: no newline at end of file
- Stage: compile
- Category: toolchain-config
- Scope: forced-include QNX polyfill header

## Symptoms

- Every QNX translation unit emitted the same `-Wnewline-eof` warning for `qnx_std_polyfill.h`.

## Root cause

- The force-included header lacked a trailing newline, so the warning repeated for every translation unit.

## Fix pattern

- Treat force-included headers as amplification points; even a one-byte formatting defect can flood the whole build log.

## Applied change

- Added a single trailing newline to `qnx_std_polyfill.h`.

## Verification

- The warning disappeared from subsequent QNX build logs.

## Files touched

- `cef/patch/qnx/chromium/new_files/build/config/qnx/qnx_std_polyfill.h`

## Related notes

- `docs/qnx/build-error-index.md`
