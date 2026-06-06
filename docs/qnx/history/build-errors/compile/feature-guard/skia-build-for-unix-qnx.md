# Skia on QNX must predefine SK_BUILD_FOR_UNIX to avoid falling into the Mac semaphore path

- Date: 2026-06-04
- Signature: dispatch/dispatch.h not found in SkSemaphore.cpp
- Stage: compile
- Category: feature-guard
- Scope: Skia platform detection

## Symptoms

- `SkSemaphore.cpp` tried to include Apple GCD headers on QNX.

## Root cause

- QNX did not satisfy Skia's auto-detected Unix/Linux compiler-macro checks, so the code fell through to `SK_BUILD_FOR_MAC`.

## Fix pattern

- When a library's source-level auto-detection misses QNX but an existing Unix branch is correct, predefine the intended platform macro in GN rather than rewriting the implementation.

## Applied change

- Added `SK_BUILD_FOR_UNIX` in Skia's Linux/ChromeOS config branch, which is already active for QNX.

## Verification

- `SkSemaphore.cpp` built using QNX `<semaphore.h>` instead of `dispatch/dispatch.h`.

## Files touched

- `cef/patch/patches/qnx/chromium/skia_qnx_build_for_unix.patch`
- `cef/patch/patch.cfg`
- `skia/BUILD.gn`

## Related notes

- `docs/qnx/build-error-index.md`
