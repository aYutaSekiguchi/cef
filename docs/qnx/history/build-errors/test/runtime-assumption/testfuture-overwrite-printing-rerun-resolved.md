# TestFuture overwrite-printing failures disappeared after adjacent formatting fixes

- Date: 2026-05-23
- Signature: TestFutureTest.ShouldPrint* overwrite cases failed once and passed on rerun
- Stage: test
- Category: runtime-assumption
- Scope: base/test future formatting

## Symptoms

- Two `TestFutureTest` overwrite-printing cases failed in a broad run and passed on rerun.

## Root cause

- The exact cause was not isolated.
- Most likely the failures were fallout from the adjacent `ToStringHelper<T*>` formatting regression or transient build-cache/test-order effects.

## Fix pattern

- For one-off failures that disappear after a nearby root fix, record the likely dependency and avoid inventing a second workaround without a stable reproducer.

## Applied change

- No dedicated change beyond the neighboring pointer-formatting fixes.

## Verification

- Both overwrite-printing tests passed on rerun.

## Files touched

- `base/strings/to_string.h`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/type-trait-template/tostring-helper-char-pointer-exclusion.md`
