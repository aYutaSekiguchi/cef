# Dawn common platform detection and libsync bundling need QNX-specific compile guards

- Date: 2026-06-03
- Signature: Unsupported platform in Dawn Platform.h or linux/sync_file.h not found
- Stage: compile
- Category: feature-guard
- Scope: third_party/dawn and third_party/libsync

## Symptoms

- Dawn common headers failed with `Unsupported platform.` and undeclared `mHandle` follow-on errors.
- `third_party/libsync/src/sync.c` failed because `<linux/sync_file.h>` was not available on QNX.

## Root cause

- Dawn's source-level platform detection did not map `__QNX__` to any supported platform branch.
- The bundled libsync path assumed the Android/Linux sync_file ABI and headers.
- Chromium's Linux-like GN routing brought both pieces into the QNX build.

## Fix pattern

- If the current product goal only needs buildability for common/headless plumbing, map QNX into the closest safe compile-time branch instead of pretending the full feature backend exists.
- For helper libraries whose real API is not needed on QNX, stub the GN target rather than compiling incompatible Linux code.

## Applied change

- Routed QNX through Dawn's Linux/POSIX platform branch in `Platform.h`.
- Replaced libsync's bundled source-set with an empty `group("libsync")` on QNX.
- Registered both edits as durable QNX patches.

## Verification

- Dawn's common target compiled on QNX.
- The bundled libsync NDK shim was no longer pulled in.
- The next failing target moved to SwiftShader.

## Files touched

- `cef/patch/patches/qnx/chromium/dawn_qnx_platform.patch`
- `cef/patch/patches/qnx/chromium/libsync_qnx_stub.patch`
- `cef/patch/patch.cfg`
- `third_party/dawn/src/dawn/common/Platform.h`
- `third_party/libsync/BUILD.gn`

## Related notes

- `docs/qnx/build-error-index.md`
