# Overlapping QNX patches break already-applied reverse detection

- Date: 2026-07-13
- Signature: `3 patches failed to apply: qnx_screen_bridge_render_only_fallback, qnx_mesa_fallback_gate, qnx_compositor_frame_capture`
- Stage: bootstrap
- Category: build-graph
- Scope: CEF-managed QNX Ozone patch ordering and repeated bootstrap

## Symptoms

`patch_updater.py` reported three consecutive QNX Ozone patches as failed even
though the live Chromium tree contained the intended final implementation.

## Root cause

The three patches successively modified the same producer hunks. On an
already-patched tree, later changes prevented reverse detection of the earlier
patches. The patcher then treated those earlier patches as unapplied and their
forced forward application failed.

After consolidating those three patches, a clean bootstrap exposed a second
durability gap: the Stage 1+2 Screen bridge implementation existed only in the
generated Chromium tree. It was absent from both `new_files` and the registered
patch stack, so the consolidated fallback patch had no valid clean-tree base.

## Fix pattern

Keep the initial implementation and successive edits to the same newly
introduced QNX subsystem in one CEF-managed patch when no later registered
patch needs the intermediate state. Reconstruct the pre-patch state from
`new_files` plus all earlier `patch.cfg` entries; do not infer it only by
reversing patches from a live tree. Verify both forward application to that
base and reverse detection against the final applied tree.

## Applied change

Consolidated Stage 1+2 Screen bridge, render-only fallback, Mesa DMAbuf
fallback, and compositor capture into
`qnx_screen_bridge_render_only_fallback.patch`. Removed the overlapping Mesa
and compositor patch registrations. The independent Viz pre-swap hook remains
a separate non-overlapping patch.

## Verification

- `patcher.py` successfully applied the consolidated patch to the reconstructed
  pre-change QNX Ozone files.
- `patcher.py` detected the consolidated patch as already applied on the final
  Chromium tree and skipped it.
- The generated patch uses `diff --git` with full index hashes.

## Files touched

- `cef/patch/patch.cfg`
- `cef/patch/patches/qnx/chromium/qnx_screen_bridge_render_only_fallback.patch`

## Related notes

- `docs/qnx/history/build-errors/bootstrap/build-graph/cef-managed-patch-registration-and-clean-bootstrap.md`
- `docs/qnx/patch-hygiene.md`
