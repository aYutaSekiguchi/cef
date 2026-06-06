# Rust bindgen consumers need QNX sysroot macros for dav1d and libyuv

- Date: 2026-06-03
- Signature: sys/platform.h not configured for target or Endian not defined during bindgen
- Stage: compile
- Category: feature-guard
- Scope: rust bindgen for crabbyavif

## Symptoms

- `dav1d_bindgen.rs` and `libyuv_bindgen.rs` generation failed with QNX sysroot header errors:
  - `Endian not defined`
  - `not configured for target`
  - `not configured for CPU`
  - missing `_NTO_CPU_HDR_DIR_(platform.h)`

## Root cause

- The C/C++ toolchain already passed the required QNX sysroot macros for normal compilation.
- The `rust_bindgen_generator` subprocess did not inherit those platform defines automatically.
- `dav1d` and `libyuv` headers therefore hit the raw QNX sysroot guards without `__LITTLEENDIAN__`, `__QNXNTO__`, `__QNX__`, and `__X86_64__`.

## Fix pattern

- When bindgen parses target headers outside the normal compile path, inject the platform-defining macros through the config used by that bindgen target.
- Prefer the narrowest target-local define workaround unless the shared bindgen infrastructure is ready for a durable platform branch.

## Applied change

- Added QNX define blocks to `third_party/dav1d/BUILD.gn` and `third_party/libyuv/BUILD.gn` configs.
- Registered the resulting patches as `dav1d_qnx_endian.patch` and `libyuv_qnx_endian.patch`.

## Verification

- `dav1d_bindgen.rs` and `libyuv_bindgen.rs` generated cleanly.
- The build moved on to the Dawn/libsync blocker.

## Files touched

- `cef/patch/patches/qnx/chromium/dav1d_qnx_endian.patch`
- `cef/patch/patches/qnx/chromium/libyuv_qnx_endian.patch`
- `cef/patch/patch.cfg`
- `third_party/dav1d/BUILD.gn`
- `third_party/libyuv/BUILD.gn`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/dawn-platform-linux-shim-and-libsync-stub.md`
