# FFmpeg QNX config files should be regenerated via configure, not copied from Linux

- Date: 2026-06-05
- Signature: avconfig.h missing or stale Linux-derived FFmpeg config values
- Stage: bootstrap
- Category: build-graph
- Scope: FFmpeg QNX config generation

## Symptoms

- The original Linux-copy shortcut left missing or stale FFmpeg config artifacts such as `avconfig.h` and `ffversion.h`.

## Root cause

- Copying Linux-generated config files did not preserve the actual QNX configure state, commit hash, or future branding-specific config values.

## Fix pattern

- Generate target-platform config artifacts from the upstream configure flow whenever the platform path already exists but the generated outputs are target-specific.

## Applied change

- Added `cef/tools/qnx_build_ffmpeg_config.sh` to run FFmpeg `configure` for QNX and emit the 12 expected config outputs.
- Replaced the copied/stale files under the managed new-files directory with generated QNX versions.

## Verification

- `avconfig.h` existed, `ffversion.h` matched the current commit, and the generated config directory became reproducible and idempotent.

## Files touched

- `cef/tools/qnx_build_ffmpeg_config.sh`
- `cef/patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/Chromium/qnx/x64/*`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/toolchain-config/ffmpeg-posix-xopen-collision-qnx.md`
