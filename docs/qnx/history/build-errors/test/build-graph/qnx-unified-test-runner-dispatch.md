# QNX test execution should be driven by a unified module-based runner

- Date: 2026-06-04
- Signature: qnx_run.sh, qnx_run_test.sh, and qnx_run_v8_unittests.py duplicated boot and serial logic
- Stage: test
- Category: build-graph
- Scope: QNX test-runner tooling

## Symptoms

- Separate scripts duplicated QEMU boot, login, serial, and per-module logic.
- Adding a new test module required another top-level script or more copy-paste.

## Root cause

- There was no single source of truth for QNX module execution requirements and guest boot orchestration.

## Fix pattern

- Centralize QEMU/session management and let modules declare only their own binary, strategy, skips, and flags.

## Applied change

- Added `cef/tools/qnx_tests/` with shared boot logic, module registry, CLI, and module implementations.
- Converted `qnx_run_test.sh` into a thin dispatcher shim.
- Kept `qnx_run_v8_unittests.py` as a backward-compatible shim.

## Verification

- The unified CLI listed registered modules and supported `--base`, `--v8`, `--swiftshader`, and `--all`.
- Module imports and helper-level parsing tests passed offline.

## Files touched

- `cef/tools/qnx_tests/__init__.py`
- `cef/tools/qnx_tests/common.py`
- `cef/tools/qnx_tests/registry.py`
- `cef/tools/qnx_tests/cli.py`
- `cef/tools/qnx_tests/modules/*.py`
- `cef/tools/qnx_run_test.sh`
- `cef/tools/qnx_run_v8_unittests.py`

## Related notes

- `docs/qnx/build-error-index.md`
