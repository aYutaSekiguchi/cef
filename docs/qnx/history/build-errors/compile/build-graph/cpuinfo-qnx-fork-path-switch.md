# cpuinfo must switch to the qnx-ports fork on QNX

- Date: 2026-06-03
- Signature: linux/api.h file not found or CPU_SETSIZE missing in third_party/cpuinfo
- Stage: compile
- Category: build-graph
- Scope: third_party/cpuinfo

## Symptoms

- `third_party/cpuinfo` failed on QNX-specific compile surfaces:
  - missing `<linux/api.h>`
  - missing `CPU_SETSIZE`
  - macro collision around a local `min()` helper

## Root cause

- The Chromium-pinned cpuinfo sources assume Linux headers and Linux-specific topology helpers when routed through the Linux path.
- Chromium's `is_linux` abstraction caused QNX to enter that path.
- The qnx-ports fork already carried a QNX-specific implementation based on syspage and cpuid.

## Fix pattern

- When a third-party dependency already has a maintained QNX-oriented fork, prefer switching the source path instead of locally patching many Linux assumptions one by one.
- Keep the switch reproducible through DEPS/submodule registration plus GN path rewiring.

## Applied change

- Added `third_party/cpuinfo_qnx/src` to the QNX source sync path.
- Added `cpuinfo_qnx_paths.patch` to rewire `third_party/cpuinfo/BUILD.gn` toward the qnx-ports source tree when `is_qnx`.
- Registered the patch in `cef/patch/patch.cfg`.

## Verification

- `third_party/cpuinfo` compiled cleanly on QNX.
- The next build blocker moved to `third_party/farmhash`.

## Files touched

- `cef/patch/patches/qnx/chromium/qnx_source_sync.patch`
- `cef/patch/patches/qnx/chromium/cpuinfo_qnx_paths.patch`
- `cef/patch/patch.cfg`
- `third_party/cpuinfo/BUILD.gn`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/build-graph/farmhash-qnx-fork-path-switch.md`
