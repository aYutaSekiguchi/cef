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

## 2026-06-07: rebase regression for CEF 147.0.7727.147

After the CEF 147 rebase, `use_dawn = false` alone stopped being
sufficient to remove the Dawn vulkan backend from the QNX build.
Five targets failed at the same compile boundary:

- `obj/third_party/dawn/.../TextureVk.o`
- `obj/third_party/dawn/.../RenderDoc.o`
- `obj/third_party/dawn/.../MemoryServiceImplementationOpaqueFD.o`
- `obj/third_party/dawn/.../MemoryServiceImplementationDmaBuf.o`
- `obj/third_party/dawn/.../static/VulkanBackend.o`

### Re-emerged symptoms

- `renderdoc_app.h:43` reported `#error "Unknown platform"` and a
  cascade of `RENDERDOC_CC` typedef redefinitions plus an
  `unknown type name 'pRENDERDOC_RemoveHooks'`.
- `MemoryServiceImplementationOpaqueFD.cpp` reported
  `unknown type name 'ExternalImageDescriptorOpaqueFD'` and likewise
  for `OpaqueFD`/`DmaBuf`/`FD` export-info structs in
  `VulkanBackend.cpp`.

### Re-emerged root cause

- `dawn/scripts/dawn_features.gni` declares
  `dawn_enable_vulkan = is_linux || is_chromeos || ...`.
- QNX is routed through `is_linux` by Chromium's GN plumbing, so
  `dawn_enable_vulkan` defaults to `true` on QNX even though
  `use_dawn = false`.
- `third_party/dawn/src/dawn/native/BUILD.gn:52` guards the whole
  `vulkan/` source list with `if (dawn_enable_vulkan)`, so the
  `vulkan/external_memory/MemoryServiceImplementation{DmaBuf,OpaqueFD}.cpp`
  and `vulkan/utils/RenderDoc.cpp` translation units are still
  compiled.
- These translation units reference `ExternalImageDescriptor{OpaqueFD,DmaBuf,FD}`
  and friends. In the CEF 147 Dawn tree the corresponding
  `struct` declarations are not present, so the include resolves
  the header but the type is unknown.
- `renderdoc_app.h` uses `#if defined(__linux__) || defined(_WIN32) || ...`;
  QNX matches none of those, so it falls through to `#error "Unknown platform"`.

### Re-emerged fix pattern

- Apply the same one-line pattern as the original 2026-06-05 fix
  (a QNX `args.gn` override), but for the next downstream flag in
  the chain. Track each Dawn subsystem independently so a future
  rebase that re-enables part of the chain (e.g. by adding a
  consumer-side guard upstream) is easy to peel back.
- Upstream `dawn_features.gni` is a reasonable merge candidate for
  the `(is_linux && !is_qnx)` shape, but until that lands we keep
  the override local.

### Re-emerged applied change

- Added `dawn_enable_vulkan = false` next to `use_dawn = false` in
  `cef/tools/cef_create_projects_qnx.sh` (Phase 4 args.gn
  generation block).
- Re-ran `cef_create_projects_qnx.sh` to regenerate
  `out/qnx_release/args.gn`.
- The five targets listed above compile cleanly; the build
  progresses to the next actionable failure.

### Re-emerged verification

- `obj/third_party/dawn/src/dawn/native/sources/TextureVk.o` and
  the rest of the `vulkan/` translation units drop out of the
  build graph (`ninja -t deps` shows no consumer).
- The `renderdoc_app.h` include is unreachable from
  `libdawn_native` on QNX, so the platform macro error stops firing.

### Re-emerged files touched

- `cef/tools/cef_create_projects_qnx.sh`
- `out/qnx_release/args.gn`
- `docs/qnx/history/build-errors/compile/build-graph/dawn-disabled-for-headless-qnx.md` (this note)
