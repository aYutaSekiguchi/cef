# ANGLE end2end tests on QNX are blocked by missing Vulkan-capable GPU exposure in the test environment

- Date: 2026-06-05
- Signature: VK_KHR_surface or VK_EXT_headless_surface not supported in angle_end2end_tests
- Stage: test
- Category: test-environment
- Scope: ANGLE runtime validation

## Symptoms

- `angle_system_info_test` and `angle_unittests` were usable, but `angle_end2end_tests` failed broadly on Vulkan surface support.

## Root cause

- The QEMU guest environment did not expose a Vulkan-capable GPU, Vulkan ICD, or relevant surface extensions.
- This was an infrastructure limitation, not a source-port blocker in the ANGLE code itself.

## Fix pattern

- Stop source-level churn when the remaining failures are caused by missing test hardware or guest graphics infrastructure.
- Record suspension criteria and the conditions needed to resume.

## Applied change

- Suspended ANGLE source work after achieving stable unit-test results and documented the GPU/environment dependency for future resumption.

## Verification

- `angle_unittests` achieved `5989 PASS / 0 FAIL`.
- `angle_end2end_tests` failures were consistently Vulkan-environment related.

## Files touched

- `cef/tools/qnx_run_test.sh`
- `cef/tools/qnx_tests/modules/angle.py`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/angle-executable-path-via-chrome-exe-path.md`
