# QNX ANGLE tests need the EGL GL backend sources

- Date: 2026-06-11
- Signature: `undefined reference to rx::DisplayEGL::DisplayEGL(...)` and then `undefined reference to angle::DrmFourCCFormatToGLInternalFormat(...)` while linking `angle_tests`
- Stage: link
- Category: build-graph
- Scope: ANGLE QNX test targets

## Symptoms

- `./out/qnx_release/ninja_qnx.sh angle_tests` failed while linking `angle_white_box_perftests` and `angle_white_box_tests`.
- First failure: `obj/third_party/angle/libANGLE_no_vulkan/Display.o` referenced `rx::DisplayEGL::DisplayEGL(egl::DisplayState const&)`.
- After adding the EGL display source, the next failure referenced `angle::DrmFourCCFormatToGLInternalFormat(int, bool*)` from `DmaBufImageSiblingEGL.o`.

## Root cause

- The QNX minimal ANGLE port routes QNX through `ANGLE_PLATFORM_LINUX` so the headless/offscreen code path can compile.
- ANGLE's GL backend source list still excluded QNX, so `DisplayEGL.cpp` was referenced by `Display.cpp` but not compiled into `angle_gl_backend`.
- The dma-buf helper dependency was also Linux/ChromeOS-only, so `DmaBufImageSiblingEGL.cpp` compiled without the `src/common/linux:angle_dma_buf` object that defines `DrmFourCCFormatToGLInternalFormat`.

## Fix pattern

- Keep the QNX headless path on the Linux-style EGL backend sources.
- Keep QNX out of Linux-only libdrm probing and X11/GBM/Wayland optional dependencies.
- Include only the shared helper dependency required by the EGL source list.

## Applied change

- Extended `third_party/angle/src/libANGLE/renderer/gl/gl_backend.gni` to add the EGL GL backend sources for `is_qnx`.
- Extended `third_party/angle/src/libANGLE/renderer/gl/BUILD.gn` to depend on `src/common/linux:angle_dma_buf` for QNX.
- Registered the durable change in `cef/patch/patches/qnx/chromium/angle_qnx_minimal_linux_headless.patch`.

## Verification

- Clean ANGLE checkout: `git apply --check -p0` succeeds for `angle_qnx_minimal_linux_headless.patch`.
- Combined clean apply check succeeds for the QNX ANGLE patch set.
- `./out/qnx_release/ninja_qnx.sh angle_tests` now completes successfully.

## Files touched

- `third_party/angle/src/libANGLE/renderer/gl/gl_backend.gni`
- `third_party/angle/src/libANGLE/renderer/gl/BUILD.gn`
- `cef/patch/patches/qnx/chromium/angle_qnx_minimal_linux_headless.patch`

## Related notes

- `docs/qnx/history/build-errors/compile/feature-guard/angle-minimal-linux-headless-qnx-port.md`
- `docs/qnx/history/build-errors/compile/build-graph/angle-perftests-glmark2-angle-qnx-skip.md`
- `docs/qnx/history/build-errors/compile/build-graph/angle-gpu-info-util-qnx-systeminfo.md`
