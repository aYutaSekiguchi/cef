# FFmpeg QNX platform config directory must exist under chromium/config/Chromium/qnx/x64

- Date: 2026-06-03
- Signature: missing and no known rule to make it for ffmpeg_nasm_action.inputdeps
- Stage: gn
- Category: build-graph
- Scope: third_party/ffmpeg config selection

## Symptoms

- The first `ninja -C out/qnx_release/ cefsimple` after a clean bootstrap failed before any C++ compilation.
- Ninja reported that `third_party/ffmpeg/chromium/config/Chromium/qnx/x64/config.asm` was missing and had no generating rule.

## Root cause

- `third_party/ffmpeg/BUILD.gn` resolves platform config from `chromium/config/$ffmpeg_branding/$os_config/$ffmpeg_arch`.
- For QNX builds, `$os_config` became `qnx`.
- Upstream Chromium shipped pre-generated config directories for Linux and other mainstream targets, but not for `qnx/x64`.
- GN therefore selected a directory that did not exist on disk.

## Fix pattern

- When GN selects a platform-specific generated-config directory, either provide that directory as durable new files or explicitly redirect the platform to an existing compatible directory.
- Prefer adding managed new files when the selected path is already stable and the downstream build expects it directly.

## Applied change

- Created `third_party/ffmpeg/chromium/config/Chromium/qnx/x64/`.
- Added the four expected config files: `config.asm`, `config.h`, `config_components.asm`, `config_components.h`.
- Stored them under `cef/patch/qnx/chromium/new_files/...` so bootstrap installs them automatically.

## Verification

- `ninja -C out/qnx_release/ cefsimple` advanced past the FFmpeg `phony/.../inputdeps` step.
- The `nasm_assemble("ffmpeg_nasm")` action completed cleanly.

## Files touched

- `cef/patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/Chromium/qnx/x64/config.asm`
- `cef/patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/Chromium/qnx/x64/config.h`
- `cef/patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/Chromium/qnx/x64/config_components.asm`
- `cef/patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/Chromium/qnx/x64/config_components.h`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/fieldtrial-to-struct-platform-qnx-choice.md`
