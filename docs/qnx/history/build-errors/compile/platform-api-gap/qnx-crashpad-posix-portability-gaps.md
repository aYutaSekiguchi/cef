# QNX Crashpad POSIX portability gaps after util bring-up

- Date: 2026-06-09
- Signature: `Port this function to your system.` / `kFDDir` / `OPEN_MAX` / `kSignalNames length`
- Stage: compile
- Category: platform-api-gap
- Scope: `third_party/crashpad/crashpad/util/posix`

## Symptoms

After the smaller Crashpad util fixes, `cefsimple` advanced into generic POSIX support code:

```text
FAILED: obj/third_party/crashpad/crashpad/util/util/drop_privileges.o
../../third_party/crashpad/crashpad/util/posix/drop_privileges.cc:89:2: error: Port this function to your system.

FAILED: obj/third_party/crashpad/crashpad/util/util/close_multiple.o
../../third_party/crashpad/crashpad/util/posix/close_multiple.cc:83:35: error: use of undeclared identifier 'kFDDir'
../../third_party/crashpad/crashpad/util/posix/close_multiple.cc:150:29: error: use of undeclared identifier 'OPEN_MAX'

FAILED: obj/third_party/crashpad/crashpad/util/util/symbolic_constants_posix.o
../../third_party/crashpad/crashpad/util/posix/symbolic_constants_posix.cc:146:15: error: static assertion failed due to requirement 'std::size(kSignalNames) == 57': kSignalNames length
```

## Root cause

QNX had moved past Crashpad's core and misc layers, but these remaining POSIX helpers still only modeled Apple/Linux/Android:

1. `drop_privileges.cc`
   - QNX fell into a hard `#error Port this function to your system.` branch.
   - QNX provides `setresgid()` / `setresuid()`, so the existing Linux-style permanent drop pattern is usable.

2. `close_multiple.cc`
   - the FD directory path only had Apple (`/dev/fd`) and Linux-family (`/proc/self/fd`) branches, leaving QNX with no `kFDDir`.
   - the fallback logic also referenced `OPEN_MAX` in the non-Linux path, but QNX does not expose that macro in this build configuration.

3. `symbolic_constants_posix.cc`
   - the existing static assertions only knew the Linux/Android 32-entry table or Apple-style `NSIG`-sized tables.
   - QNX has `NSIG == 57` and different signal numbering than generic Linux (for example `SIGEMT`, `SIGPWR`, `SIGPOLL`, `SIGDOOM`).

## Fix pattern

- Reuse the Linux/Android privilege-drop implementation on QNX:
  - include `BUILDFLAG(IS_QNX)` in the `setresgid()` / `setresuid()` branch
- Reuse `/dev/fd` enumeration on QNX for descriptor walking
- Skip the `OPEN_MAX` fallback on QNX when the macro is absent, relying on `sysconf(_SC_OPEN_MAX)` / `getdtablesize()` instead
- Add a QNX-specific signal-name table sized to `NSIG`, matching the QNX sysroot signal numbering used by the toolchain

## Applied change

- `third_party/crashpad/crashpad/util/posix/drop_privileges.cc`
  - add `BUILDFLAG(IS_QNX)` to the Linux/Android privilege-drop branch
- `third_party/crashpad/crashpad/util/posix/close_multiple.cc`
  - define `kFDDir = "/dev/fd"` for QNX
  - treat QNX like Linux/Android for the `OPEN_MAX` guard
- `third_party/crashpad/crashpad/util/posix/symbolic_constants_posix.cc`
  - add a QNX-specific signal table
  - assert `std::size(kSignalNames) == NSIG` on QNX

## Verification

- A clean-tree bootstrap still succeeded after registering the patch in `patch.cfg` (`bootstrap exit: 0`).
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` removed all four target signatures:
  - `drop_privileges.cc: Port this function to your system.` hits: `0`
  - `close_multiple.cc: kFDDir` hits: `0`
  - `close_multiple.cc: OPEN_MAX` hits: `0`
  - `symbolic_constants_posix.cc: kSignalNames length` hits: `0`
- The tree advanced past Crashpad POSIX util and the next blocker moved to PDFium Linux-only code:
  ```
  FAILED: obj/third_party/pdfium/core/fxge/fxge/fx_linux_impl.o
  ../../third_party/pdfium/core/fxge/linux/fx_linux_impl.cpp:23:2: error: "Included on the wrong platform"
  ```

## Files touched

- `cef/patch/patches/qnx/chromium/crashpad_posix_qnx_portability.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-posix-portability-gaps.md`
- `cef/docs/qnx/build-error-index.md`

## Related notes

- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-util-portability-gaps.md`
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-core-platform-definitions-and-handler-stubs.md`
