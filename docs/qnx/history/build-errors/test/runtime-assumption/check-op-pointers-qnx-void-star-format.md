# CheckOpPointers on QNX must accept pointer formatting without a 0x prefix

- Date: 2026-05-23
- Signature: CheckDeathTest.CheckOpPointers actual output missing 0x prefix
- Stage: test
- Category: runtime-assumption
- Scope: base/check death tests

## Symptoms

- `CheckDeathTest.CheckOpPointers` failed because expected output assumed `0x...`, while QNX emitted bare hexadecimal digits.

## Root cause

- QNX libc++ formats `const void*` differently from glibc in this test path.
- A Windows-specific expectation branch already existed for the same shape.

## Fix pattern

- Reuse existing expectation branches for equivalent output-format contracts instead of inventing a QNX-only regex when the behavior already matches another platform family.

## Applied change

- Added `BUILDFLAG(IS_QNX)` to the Windows-style pointer-format expectation branch.

## Verification

- `CheckDeathTest.CheckOpPointers` passed.

## Files touched

- `base/check_unittest.cc`

## Related notes

- `docs/qnx/build-error-index.md`
