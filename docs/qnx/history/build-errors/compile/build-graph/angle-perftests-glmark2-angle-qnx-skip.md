# QNX ANGLE perftests should not pull in `glmark2_angle`

- Date: 2026-06-11
- Signature: `third_party/angle/third_party/glmark2/src/src/main.cpp:214:26: error: unknown type name 'native_state'`
- Stage: compile
- Category: build-graph
- Scope: ANGLE perftests / `third_party/glmark2`

## Symptoms

- A QNX build of `angle_perftests` failed inside the vendored `glmark2_angle` benchmark.
- The first error came from `main.cpp` before any benchmark runtime code could run.

## Root cause

- The `glmark2_angle` benchmark in ANGLE's third-party glmark2 tree does not have a QNX native-state backend.
- The earlier QNX allow-list change made `glmark2_angle` reachable again, but the benchmark source still only supports X11/DRM/GBM/Wayland/Dispmanx/Win32 state objects.
- QNX needs the ANGLE unit/perf tests, but not this benchmark target for the build to remain healthy.

## Fix pattern

- Keep the main ANGLE tests enabled on QNX.
- Exclude optional benchmark subtargets that still require a platform-specific native-state implementation.

## Applied change

- Removed QNX from the `glmark2_angle` `data_deps` condition in `third_party/angle/src/tests/BUILD.gn`.
- Updated the CEF patch and its comment to note that `glmark2_angle` stays excluded on QNX.

## Verification

- `gn gen out/qnx_release` succeeded.
- `gn ls out/qnx_release '//third_party/angle/src/tests:*'` no longer shows `glmark2_angle`.
- `ninja -C out/qnx_release -n angle_perftests` no longer references `glmark2`.

## Files touched

- `third_party/angle/src/tests/BUILD.gn`
- `cef/patch/patches/qnx/chromium/angle_tests_qnx_allow.patch`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/history/build-errors/compile/feature-guard/angle-minimal-linux-headless-qnx-port.md`
- `docs/qnx/history/build-errors/test/test-environment/angle-end2end-blocked-by-no-vulkan-gpu.md`
