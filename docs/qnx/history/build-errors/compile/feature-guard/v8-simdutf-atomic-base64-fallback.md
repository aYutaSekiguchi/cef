# V8 must guard simdutf atomic Base64 paths when std::atomic_ref is unavailable

- Date: 2026-05-30
- Signature: simdutf::atomic_base64_to_binary_safe missing on QNX
- Stage: compile
- Category: feature-guard
- Scope: v8 typed array builtins

## Symptoms

- Clean QNX builds failed in `v8/src/builtins/builtins-typed-array.cc`.
- Missing symbols included `simdutf::atomic_base64_to_binary_safe` and `simdutf::atomic_binary_to_base64`.

## Root cause

- QNX SDP 8 libc++ does not provide standard-library `std::atomic_ref`.
- The port correctly avoided faking `__cpp_lib_atomic_ref`, so simdutf disabled its atomic Base64 APIs with `SIMDUTF_ATOMIC_REF = false`.
- V8 still called those atomic entry points unconditionally for shared buffers.

## Fix pattern

- When a third-party library gates APIs behind a capability macro, mirror that capability check at the call site instead of inventing fake support macros.
- Prefer a non-atomic fallback path if the semantics remain valid for the current platform constraints.

## Applied change

- Guarded both atomic Base64 call sites with `#if SIMDUTF_ATOMIC_REF`.
- Fell back to the non-atomic simdutf functions when atomic-ref support was unavailable.

## Verification

- Clean QNX V8 builds succeeded without pretending full `std::atomic_ref` support existed.

## Files touched

- `v8/src/builtins/builtins-typed-array.cc`
- `cef/patch/patches/qnx/chromium/v8_base64_atomic.patch`

## Related notes

- `docs/qnx/build-error-index.md`
