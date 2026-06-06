# V8 Perfetto tracing expectations need QNX-specific number and pointer formatting

- Date: 2026-05-31
- Signature: JsonIntegrationTest expected 1e+100 but QNX emitted full decimal
- Stage: test
- Category: runtime-assumption
- Scope: v8 libplatform tracing tests

## Symptoms

- Perfetto JSON integration expected scientific notation for `1e100`.
- Pointer-string comparisons expected `0x` prefixes that QNX did not emit through the same path.

## Root cause

- QNX libc formatted these values differently from glibc while still being internally consistent.

## Fix pattern

- Adjust test expectations when the serialized representation differs but the semantic value is unchanged.

## Applied change

- Added QNX-specific expected strings for the JSON scientific-notation case and pointer formatting case.

## Verification

- `JsonIntegrationTest` and `MultipleArgsAndCopy` passed on QNX.

## Files touched

- `v8/test/unittests/libplatform/tracing-unittest.cc`
- `cef/patch/patches/qnx/chromium/v8_perfetto_trace_qnx.patch`

## Related notes

- `docs/qnx/build-error-index.md`
