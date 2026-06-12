# Stale libdrm QNX patch must be removed after upstream `xf86drm.c` disappears

- Date: 2026-06-11
- Signature: `patches failed to apply` for `qnx/chromium/libdrm_qnx_memstream_makedev`
- Stage: bootstrap
- Category: build-graph
- Scope: CEF patch stack / `third_party/libdrm`

## Symptoms

- `cef/tools/cef_create_projects_qnx.sh` aborted in the patch phase with a failure on `qnx/chromium/libdrm_qnx_memstream_makedev`.
- The patch still targeted `third_party/libdrm/src/xf86drm.c`, but the current Chromium/libdrm roll no longer contains that file.
- The failure happened before `args.gn` regeneration, so the bootstrap never reached a valid GN state.

## Root cause

- The patch was valid for an older libdrm snapshot where `xf86drm.c` still existed and QNX needed the `memstream` include / `makedev` wrapper.
- Upstream libdrm was later refactored, removing the target file from the tree.
- On the current QNX bootstrap path, `third_party/libdrm` is not part of the Linux-like build surface that QNX exercises, so the patch had become stale rather than merely needing a rebase.

## Fix pattern

- When a CEF-managed patch targets a deleted upstream file and the affected component is no longer part of the QNX build path, remove the patch from the bootstrap stack instead of hand-repairing generated state.
- Treat the patch stack as the durable source of truth; a partially recovered tree is not evidence that bootstrap is healthy.
- If the patch is still needed conceptually, capture the replacement in a new patch against the current upstream file set. Otherwise delete both `patch.cfg` registration and the stale patch file.

## Applied change

- Removed `qnx/chromium/libdrm_qnx_memstream_makedev` from `cef/patch/patch.cfg`.
- Deleted `cef/patch/patches/qnx/chromium/libdrm_qnx_memstream_makedev.patch`.
- Kept the historical explanation in `docs/qnx/history/build-errors/link/build-graph/libdrm-memstream-link-dependency-and-makedev-wrapper.md` for context.

## Verification

- After removing the stale patch, bootstrap can proceed past the patch-application phase without this blocker.
- The next validation step is a clean `cef_create_projects_qnx.sh` run, followed by the normal `mojo_unittests` build loop.

## Files touched

- `cef/patch/patch.cfg`
- `cef/patch/patches/qnx/chromium/libdrm_qnx_memstream_makedev.patch`
- `cef/docs/qnx/history/build-errors/bootstrap/build-graph/libdrm-stale-patch-after-xf86drm-removal.md`

## Related notes

- `docs/qnx/patch-hygiene.md`
- `docs/qnx/history/build-errors/link/build-graph/libdrm-memstream-link-dependency-and-makedev-wrapper.md`
- `docs/qnx/build-error-index.md`
