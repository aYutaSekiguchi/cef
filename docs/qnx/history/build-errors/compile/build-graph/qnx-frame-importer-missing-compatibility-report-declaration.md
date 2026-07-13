# QNX frame importer implementation missing compatibility report declaration

- Date: 2026-07-13
- Signature: `no type named 'ImportCompatibilityReport' in 'ui::QnxFrameImporter'`
- Stage: compile
- Category: build-graph
- Scope: QNX Ozone frame importer durable patch generation

## Symptoms

A clean build failed compiling `qnx_frame_importer.cc` because
`ImportCompatibilityReport` and `ValidateIncomingFrameForBridge` were defined
and used by the implementation but absent from `qnx_frame_importer.h`.

## Root cause

The consolidated Screen bridge patch was generated from five implementation
files and accidentally omitted `qnx_frame_importer.h`. The live development
tree already contained the declaration, so incremental builds did not expose
the missing durable hunk.

## Fix pattern

When regenerating a multi-file CEF patch, derive and verify an explicit file
manifest from the implementation plan. Apply the patch to the reconstructed
clean base and compare every resulting file, including headers, with the
validated live tree.

## Applied change

Regenerated `qnx_screen_bridge_render_only_fallback.patch` with all six QNX
Ozone producer/importer/service files, including `qnx_frame_importer.h`.
Marked the release-only `ThreadChecker` storage `[[maybe_unused]]` because its
`DCHECK` use is compiled out in official builds.

## Verification

- Clean-base patch application succeeded for all six files.
- All six patched files matched the validated source byte-for-byte.
- `qnx_frame_importer.o` compiled with `BUILD_RC=0` and no warnings.

## Files touched

- `cef/patch/patches/qnx/chromium/qnx_screen_bridge_render_only_fallback.patch`

## Related notes

- `docs/qnx/history/build-errors/bootstrap/build-graph/qnx-overlapping-patches-break-reverse-detection.md`
