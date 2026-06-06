# Validated V8 QNX patches must be registered in bootstrap, not left as local tree edits

- Date: 2026-06-01
- Signature: validated V8 patches existed locally but were not applied by bootstrap
- Stage: bootstrap
- Category: build-graph
- Scope: V8 patch registration

## Symptoms

- Clean bootstrap builds silently missed previously validated V8 fixes because the patches were not registered in `UNREGISTERED_CHROMIUM_PATCHES`.

## Root cause

- Testing had occurred against a directly edited V8 working tree.
- Bootstrap registration lagged behind the live-tree state, so production bootstrap did not replay the fixes.

## Fix pattern

- Treat bootstrap registration as part of completing any validated QNX patch, especially for downstream test-only fixes that are easy to leave in a dirty tree.

## Applied change

- Added `v8_stack_limit_qnx`, `v8_perfetto_trace_qnx`, `v8_bytecode_expectations_qnx`, and the related dependency-ordered entries to `UNREGISTERED_CHROMIUM_PATCHES`.

## Verification

- After bootstrap, `v8_unittests` ran with the expected reduced residual-failure set instead of regressing to the pre-registration state.

## Files touched

- `cef/tools/cef_create_projects_qnx.sh`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/bootstrap/build-graph/cef-managed-patch-registration-and-clean-bootstrap.md`
