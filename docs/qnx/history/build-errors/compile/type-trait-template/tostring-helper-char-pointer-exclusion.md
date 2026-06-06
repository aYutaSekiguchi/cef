# QNX pointer-string helper must exclude character pointer types

- Date: 2026-05-23
- Signature: ToStringTest.Tuple prints string literal as pointer address
- Stage: compile
- Category: type-trait-template
- Scope: base/strings to_string helper

## Symptoms

- `ToString(std::make_tuple(..., "a string"))` produced a pointer address instead of the string contents.

## Root cause

- The QNX-specific `ToStringHelper<T*>` specialization matched `const char*` because `char` is an object type.
- That stole resolution from the normal streamable-string path.

## Fix pattern

- When adding pointer specializations, explicitly exclude string-like character pointer types if they should keep higher-level formatting behavior.

## Applied change

- Tightened the `requires` clause to exclude character pointer types from the QNX `T*` specialization.

## Verification

- `ToStringTest.Tuple` passed.
- Dependent formatting regressions such as `GmockExpectedSupportTest.PrintTest` also disappeared.

## Files touched

- `base/strings/to_string.h`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/test-environment/qnx-base-unittests-environment-issues.md`
