# GmockExpectedSupportTest.PrintTest was resolved by the char-pointer ToStringHelper fix

- Date: 2026-05-23
- Signature: GmockExpectedSupportTest.PrintTest resolved after ToStringHelper character-pointer exclusion
- Stage: test
- Category: runtime-assumption
- Scope: gmock expected-support printing

## Symptoms

- `GmockExpectedSupportTest.PrintTest` had been failing in the same formatting family as the pointer/string rendering regressions.

## Root cause

- The failure was downstream fallout from the overly broad QNX `ToStringHelper<T*>` specialization that treated character pointers like raw pointers.

## Fix pattern

- When a test is resolved entirely by another root fix, keep a short alias note so future searches for the original test name still land on an explanation.

## Applied change

- No dedicated code change beyond the `ToStringHelper<T*>` character-pointer exclusion.

## Verification

- `GmockExpectedSupportTest.PrintTest` passed after the `ToStringHelper<T*>` fix landed.

## Files touched

- `base/strings/to_string.h`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/type-trait-template/tostring-helper-char-pointer-exclusion.md`
