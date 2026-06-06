# farmhash must switch to the qnx-ports fork and add a QNX byte-swap path

- Date: 2026-06-03
- Signature: fatal error: 'byteswap.h' file not found
- Stage: compile
- Category: build-graph
- Scope: third_party/farmhash

## Symptoms

- `third_party/farmhash/src/src/farmhash.cc` failed because `<byteswap.h>` was not found.

## Root cause

- The upstream farmhash platform-selection chain fell through to a glibc-only `<byteswap.h>` include on QNX.
- QNX does not ship that header and also lacks the exact FreeBSD-style fallback headers/macros used by other branches.
- The qnx-ports fork was closer to QNX needs, but still required a QNX-specific bswap branch.

## Fix pattern

- For vendor code with a maintained QNX-adjacent fork, route QNX builds to that source tree first.
- If one platform-specific gap remains, overlay the fork with a minimal QNX-specific source replacement in managed new files.

## Applied change

- Added `third_party/farmhash_qnx/src` to QNX source sync.
- Rewired `third_party/farmhash/BUILD.gn` to use the qnx-ports fork on QNX.
- Added a managed replacement `farmhash.cc` with an `__QNXNTO__` byte-swap branch.

## Verification

- `third_party/farmhash` compiled cleanly on QNX.
- The next failing target moved to crabbyavif bindgen generation.

## Files touched

- `cef/patch/patches/qnx/chromium/qnx_source_sync.patch`
- `cef/patch/patches/qnx/chromium/farmhash_qnx_paths.patch`
- `cef/patch/qnx/chromium/new_files/third_party/farmhash_qnx/src/src/farmhash.cc`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/rust-bindgen-qnx-sysroot-defines-for-dav1d-and-libyuv.md`
