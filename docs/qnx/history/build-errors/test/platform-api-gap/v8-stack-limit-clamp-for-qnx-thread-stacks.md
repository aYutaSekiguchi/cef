# V8 stack limits on QNX must be clamped to actual thread stack sizes

- Date: 2026-05-31
- Signature: ValueSerializer stack-overflow tests hit OS guard page before STACK_CHECK
- Stage: test
- Category: platform-api-gap
- Scope: V8 stack guard calibration

## Symptoms

- Deeply nested serializer tests exited with 13 or SIGSEGV instead of hitting V8's normal stack-overflow handling.

## Root cause

- V8 defaulted to a logical stack size near 984 KB.
- QNX QEMU worker threads had only 256 KB and the main thread 512 KB.
- The computed V8 limit therefore fell below the OS guard page instead of above it.

## Fix pattern

- When a runtime has a configurable logical stack limit, clamp it to the actual OS-provided stack with a safety margin.

## Applied change

- Added `Stack::GetStackSize()` and returned `__tls()->__stacksize` on QNX.
- Clamped `StackGuard::ThreadLocal::Initialize()` using a 128 KB safety margin.

## Verification

- `ValueSerializerTest.*StackOverflow*` and `DecodeVerifyObjectCount` passed.
- No regressions were reported in other V8 test clusters.

## Files touched

- `cef/patch/patches/qnx/chromium/v8_stack_limit_qnx.patch`
- `v8/src/base/platform/platform.h`
- `v8/src/base/platform/platform-posix.cc`
- `v8/src/base/platform/platform-qnx.cc`
- `v8/src/execution/stack-guard.cc`
- `cef/tools/stack_measure.c`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/v8-runtime-stack-top-from-tls.md`
