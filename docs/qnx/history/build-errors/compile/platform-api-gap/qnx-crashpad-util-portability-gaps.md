# QNX Crashpad util portability gaps after core stub bring-up

- Date: 2026-06-09
- Signature: `IOV_MAX` / `METRICS_OS_NAME` / `uuid.cc: error: Port.`
- Stage: compile
- Category: platform-api-gap
- Scope: `third_party/crashpad/crashpad/util/file`, `third_party/crashpad/crashpad/util/misc`

## Symptoms

After the Crashpad core/platform-definition stub patch, `cefsimple` advanced to smaller util portability failures:

```text
FAILED: obj/third_party/crashpad/crashpad/util/util/file_writer.o
../../third_party/crashpad/crashpad/util/file/file_writer.cc:91:26: error: use of undeclared identifier 'IOV_MAX'

FAILED: obj/third_party/crashpad/crashpad/util/util/metrics.o
../../third_party/crashpad/crashpad/util/misc/metrics.cc:103:54: error: expected ')'
../../third_party/crashpad/crashpad/util/misc/metrics.cc:122:46: error: expected ')'

FAILED: obj/third_party/crashpad/crashpad/util/util/uuid.o
../../third_party/crashpad/crashpad/util/misc/uuid.cc:127:2: error: Port.
```

## Root cause

These were three independent but small QNX portability gaps inside Crashpad util code:

1. `file_writer.cc`
   - assumes `IOV_MAX` is always exposed as a macro on non-Android POSIX systems.
   - QNX exposes `_SC_IOV_MAX` / `UIO_MAXIOV`, but not the plain `IOV_MAX` macro in this build configuration.

2. `metrics.cc`
   - uses string literal concatenation:
     ```cc
     "Crashpad.ExceptionCode." METRICS_OS_NAME
     ```
   - without a QNX `#define METRICS_OS_NAME ...`, the token sequence becomes syntactically invalid, producing `expected ')'` instead of a clearer missing-platform error.

3. `uuid.cc`
   - still routes QNX to the unsupported-platform `#error Port.` block.
   - QNX does not need a special UUID system API here; the existing pseudo-random `base::RandBytes()` path already used for Linux/Android/Fuchsia/Windows is sufficient.

## Fix pattern

Use the smallest adjacent-platform patterns already present in Crashpad:

- treat QNX like Android for `writev()` vector-limit discovery:
  - use `sysconf(_SC_IOV_MAX)` instead of assuming `IOV_MAX`
- define `METRICS_OS_NAME "QNX"`
- include `BUILDFLAG(IS_QNX)` in the existing pseudo-random UUID generation branch

This keeps behavior aligned with current Crashpad assumptions and avoids inventing QNX-only runtime logic.

## Applied change

- `third_party/crashpad/crashpad/util/file/file_writer.cc`
  ```diff
  -#if BUILDFLAG(IS_ANDROID)
  +#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_QNX)
  ```
- `third_party/crashpad/crashpad/util/misc/metrics.cc`
  ```diff
  +#elif BUILDFLAG(IS_QNX)
  +#define METRICS_OS_NAME "QNX"
  ```
- `third_party/crashpad/crashpad/util/misc/uuid.cc`
  ```diff
  -    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)
  +    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_QNX)
  ```

## Verification

- `git apply --check cef/patch/patches/qnx/chromium/crashpad_util_qnx_portability.patch` passed against the bootstrap-applied tree.
- A clean-tree bootstrap still succeeded after registering the patch in `patch.cfg` (`bootstrap exit: 0`).
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` removed all three target signatures:
  - `IOV_MAX` hits: `0`
  - `metrics.cc: expected ')'` hits: `0`
  - `uuid.cc: error: Port.` hits: `0`
- The next blocker moved forward to the next Crashpad POSIX portability layer:
  ```
  FAILED: obj/third_party/crashpad/crashpad/util/util/drop_privileges.o
  ../../third_party/crashpad/crashpad/util/posix/drop_privileges.cc:89:2: error: Port this function to your system.
  ```
  with follow-on failures in:
  - `util/posix/close_multiple.cc` (`kFDDir`, `OPEN_MAX`)
  - `util/posix/symbolic_constants_posix.cc` (`kSignalNames length`)

## Files touched

- `cef/patch/patches/qnx/chromium/crashpad_util_qnx_portability.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-util-portability-gaps.md`
- `cef/docs/qnx/build-error-index.md`

## Related notes

- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-core-platform-definitions-and-handler-stubs.md`
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-no-op-implementation-for-linux-only-sources.md`
