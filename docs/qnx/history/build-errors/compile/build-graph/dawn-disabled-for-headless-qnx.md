# Headless QNX builds should disable Dawn until WebGPU work is intentionally resumed

- Date: 2026-06-05
- Signature: Dawn Vulkan backend and RenderDoc types fail while building headless cfsimple
- Stage: compile
- Category: build-graph
- Scope: Dawn/WebGPU enablement

## Symptoms

- After ANGLE work, Dawn resumed blocking `cefsimple` with RenderDoc platform checks and Linux-only external-image FD types.

## Root cause

- `use_dawn` defaulted to true because Chromium treated QNX as Linux-like.
- The headless `cefsimple` target did not need WebGPU at all.

## Fix pattern

- Disable large optional subsystems at the GN-arg level when they are outside the product goal and their remaining porting cost is high.

## Applied change

- Set `use_dawn = false` in the QNX `args.gn` generation path and active build args.

## Verification

- The build progressed past the previous Dawn blockers and moved on to WebRTC-specific issues.

## Files touched

- `cef/tools/cef_create_projects_qnx.sh`
- `out/qnx_release/args.gn`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/dawn-platform-linux-shim-and-libsync-stub.md`
