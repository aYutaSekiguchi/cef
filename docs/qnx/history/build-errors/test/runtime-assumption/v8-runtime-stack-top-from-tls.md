# V8 on QNX needs current-thread stack-top detection from TLS metadata

- Date: 2026-05-30
- Signature: v8_hello_world traps in Isolate::StackOverflow during normal startup
- Stage: test
- Category: runtime-assumption
- Scope: v8 runtime stack detection

## Symptoms

- `v8_hello_world` built but crashed immediately on QNX/QEMU with a trap during script compilation.

## Root cause

- QNX stack-top detection in `platform-qnx.cc` was not returning a valid current-thread stack start.
- V8 therefore interpreted normal runtime activity as stack overflow.

## Fix pattern

- On QNX, derive per-thread stack bounds from TLS metadata rather than assuming Linux-like stack-top discovery.

## Applied change

- Implemented `Stack::ObtainCurrentThreadStackStart()` using `__tls()` and `_thread_local_storage`.
- Added `#include <sys/storage.h>`.

## Verification

- `v8_hello_world` ran successfully and printed `Hello, World!` and `3 + 4 = 7`.

## Files touched

- `v8/src/base/platform/platform-qnx.cc`
- `cef/patch/patches/qnx/chromium/v8_qnx_targeting.patch`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/platform-api-gap/v8-stack-limit-clamp-for-qnx-thread-stacks.md`
