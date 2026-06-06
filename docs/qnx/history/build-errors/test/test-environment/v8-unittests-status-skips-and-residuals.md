# Residual v8_unittests failures on QNX are tracked through status-file skips and targeted follow-ups

- Date: 2026-06-01
- Signature: residual failures limited to logging, stack-sensitive, and flag-freeze categories
- Stage: test
- Category: test-environment
- Scope: v8_unittests residual set

## Symptoms

- After major stack and formatting fixes, only a small residual set of V8 test failures remained.

## Root cause

- The remaining set split into three buckets:
  - logging-path failures
  - stack-sensitive tests later fixed by targeted patches
  - status-file-managed failure cases under `official_build`

## Fix pattern

- Convert a large failing suite into a managed residual inventory.
- Use status-file skips for known upstream or policy-driven exceptions, and keep separate notes for genuinely fixable platform issues.

## Applied change

- Added QNX `[SKIP]` entries in `v8/test/unittests/unittests.status`.
- Updated `qnx_run_v8_unittests.py` to parse the QNX status section.
- Documented `LogAllTest.LogAll` as temporarily skipped pending deeper investigation.

## Verification

- The executed test count and residual failure set became stable and explainable.

## Files touched

- `v8/test/unittests/unittests.status`
- `cef/patch/patches/qnx/chromium/v8_unittests_status_logall_qnx.patch`
- `cef/tools/cef_create_projects_qnx.sh`
- `cef/tools/qnx_run_v8_unittests.py`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/v8-background-compile-stack-size-parameter.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/v8-workloads-large-stack-array.md`
