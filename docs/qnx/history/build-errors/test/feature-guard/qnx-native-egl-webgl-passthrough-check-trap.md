# QNX native EGL WebGL traps with the passthrough command decoder

- Date: 2026-07-23
- Signature: Running WebGL through passthrough command decoder without ANGLE's validation is a security risk
- Stage: test
- Category: feature-guard
- Scope: GPU command buffer / WebGL on native Mesa EGL

## Symptoms

Opening `https://www.youtube.com` in `cefsimple` terminated the GPU process
with raw wait status `133`. Renderer-side `SharedImage` allocation and command
buffer creation then failed.

## Root cause

The QNX build did not compile the validating command decoder. Chromium
therefore forced the passthrough decoder, while the supported QNX runtime used
native Mesa EGL instead of ANGLE. WebGL context creation reached the security
`CHECK` in `GLES2DecoderPassthroughImpl::Initialize` and trapped because
passthrough WebGL requires ANGLE validation.

The GPU core terminated with `SIGTRAP`; symbolizing the `libcef.so` program
counter resolved to `gles2_cmd_decoder_passthrough.cc` at that `CHECK`.

## Fix pattern

Enable the validating command decoder only for QNX in the GN feature guard.
Keep the system Mesa EGL runtime unchanged and do not weaken the passthrough
decoder security check.

## Applied change

Changed `enable_validating_command_decoder` from `is_android` to
`is_android || is_qnx`. The existing production decoder sources and
dependencies were already present; enabling the build flag selects the
validating decoder by default on QNX.

## Verification

- `./out/qnx_release/ninja_qnx.sh cefsimple` completed successfully.
- `cefsimple --use-cmd-decoder=validating
  --url=https://www.youtube.com` ran for 180 seconds under QEMU with native
  Mesa EGL.
- YouTube reached `OnLoadEnd` with HTTP status 200.
- The original command line without `--use-cmd-decoder` also ran for 120
  seconds without a GPU exit, confirming that validating is selected by
  default.
- No GPU process exit, `exit_code=133`, `StagingBuffer` `SharedImage` failure,
  or `GpuControl.CreateCommandBuffer` failure appeared in the serial log.
- A follow-up QNX Screen capture showed that the initially discovered DNS
  server (`192.168.0.1`) stalled name resolution and made YouTube display its
  offline page. Changing `/etc/resolv.conf` to `8.8.8.8` made
  `getent hosts www.youtube.com` complete immediately and `curl` return HTTP
  200. After reload, a Screen capture showed the complete YouTube home page
  with thumbnails. This network issue is separate from decoder rendering.

## Files touched

- `ui/gl/features.gni`
- `cef/patch/patch.cfg`
- `cef/patch/patches/qnx/chromium/ui_gl_enable_validating_command_decoder_qnx.patch`

## Related notes

- `docs/qnx/history/research/qnx-angle-egl-runtime-investigation-2026-07-10.md`
- `docs/qnx/history/research/qnx-cefsimple-runtime-matrix-2026-07-13.md`
