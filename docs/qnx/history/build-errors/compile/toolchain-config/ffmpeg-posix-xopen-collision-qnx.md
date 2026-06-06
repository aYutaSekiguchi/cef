# FFmpeg on QNX needs a target-local POSIX/XOPEN compatibility shim

- Date: 2026-06-05
- Signature: sys/platform.h rejects POSIX_C_SOURCE with XOPEN_SOURCE in ffmpeg_internal
- Stage: compile
- Category: toolchain-config
- Scope: third_party/ffmpeg ffmpeg_internal

## Symptoms

- FFmpeg compilation failed because QNX `sys/platform.h` rejected `_POSIX_C_SOURCE=200809L` paired with `_XOPEN_SOURCE=600`.

## Root cause

- The QNX toolchain deliberately forced `_POSIX_C_SOURCE=200809L` globally for other headers.
- FFmpeg expected the older `_POSIX_C_SOURCE=200112` together with `_XOPEN_SOURCE=600`.

## Fix pattern

- When a third-party target needs an older feature-test macro set, override it locally through a force-included compatibility shim instead of weakening the whole toolchain.

## Applied change

- Added `qnx_ffmpeg_compat.h` to redefine `_POSIX_C_SOURCE` to `200112` only for `ffmpeg_internal`.
- Injected that shim through a QNX-only `-include` in `third_party/ffmpeg/BUILD.gn`.

## Verification

- FFmpeg C translation units built with the expected macro combination and non-FFmpeg QNX targets were unaffected.

## Files touched

- `cef/patch/qnx/chromium/new_files/build/config/qnx/qnx_ffmpeg_compat.h`
- `cef/patch/patches/qnx/chromium/ffmpeg_qnx_posix_override.patch`
- `cef/patch/patch.cfg`
- `third_party/ffmpeg/BUILD.gn`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/gn/build-graph/ffmpeg-qnx-platform-config-directory-missing.md`
