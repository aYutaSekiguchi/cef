# ANGLE on QNX can build through the Linux headless path with targeted guard trims

- Date: 2026-06-05
- Signature: Unsupported Vulkan platform or Linux-only helper assumptions in ANGLE
- Stage: compile
- Category: feature-guard
- Scope: ANGLE minimal QNX build port

## Symptoms

- ANGLE failed on QNX due to unsupported platform checks, futex-only code, Linux affinity helpers, Linux-only link libs, and duplicate headless source selections.

## Root cause

- Chromium GN already routed QNX through many Linux-like ANGLE branches, but ANGLE source-level platform detection and helper assumptions still treated QNX as neither Linux nor a supported dedicated backend.

## Fix pattern

- For a headless target, reuse the closest existing Linux/offscreen path and trim only the Linux-specific assumptions that block buildability.

## Applied change

- Patched ANGLE platform detection to treat QNX as Linux for the minimal headless path.
- Disabled or stubbed Linux-only helpers such as futex mutexes, affinity pinning, execinfo-based crash handling, RenderDoc integration, and `dl`/`rt` link additions.

## Verification

- `angle_system_info_test`, `angle_unittests`, and `angle_end2end_tests` all built as QNX ELF binaries.

## Files touched

- `cef/patch/patches/qnx/chromium/angle_qnx_minimal_linux_headless.patch`
- `cef/patch/patch.cfg`
- `third_party/angle/src/common/platform.h`
- `third_party/angle/src/common/SimpleMutex.h`
- `third_party/angle/util/BUILD.gn`

## Related notes

- `docs/qnx/build-error-index.md`
