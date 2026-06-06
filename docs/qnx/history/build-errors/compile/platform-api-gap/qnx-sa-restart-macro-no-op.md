# QNX lacks SA_RESTART and needs a no-op macro definition for portable signal code

- Date: 2026-05-31
- Signature: use of undeclared identifier 'SA_RESTART'
- Stage: compile
- Category: platform-api-gap
- Scope: v8 libsampler signals

## Symptoms

- `v8_unittests` failed to build in `signals-and-mutexes-unittest.cc`.
- The compiler reported `use of undeclared identifier 'SA_RESTART'`.

## Root cause

- QNX declares in `<signal.h>` that `SA_RESTART` is not supported and leaves the macro commented out.
- Upstream code assumed the POSIX signal flag name exists and used it in `sa_flags`.

## Fix pattern

- For unsupported POSIX flag names that are semantically optional on QNX, provide a platform macro shim with the correct no-op value instead of modifying many call sites.
- Keep platform macro shims in the forced-include QNX platform header, not in unrelated standard-library polyfills.

## Applied change

- Added `#define SA_RESTART 0` to `build/config/qnx/qnx_macros.h`.
- Kept the shim in the force-included QNX platform macro header.

## Verification

- `v8_unittests` built successfully after the macro shim was added.
- Existing `base_unittests` builds remained unaffected.

## Files touched

- `build/config/qnx/qnx_macros.h`
- `cef/patch/qnx/chromium/new_files/build/config/qnx/qnx_macros.h`

## Related notes

- `docs/qnx/build-error-index.md`
