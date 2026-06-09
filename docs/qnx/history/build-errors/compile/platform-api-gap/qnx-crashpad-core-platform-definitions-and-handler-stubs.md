# QNX Crashpad core needs platform definitions and handler stubs

- Date: 2026-06-09
- Signature: `"Unhandled OS type"` / `define kOS for this operating system`
- Stage: compile
- Category: platform-api-gap
- Scope: `third_party/crashpad/crashpad/util/misc`, `third_party/crashpad/crashpad/minidump`, `third_party/crashpad/crashpad/handler`

## Symptoms

- After `qnx/chromium/crashpad_buildconfig_disable_linux_qnx` removed Crashpad's accidental Linux source selection, `cefsimple` advanced to a new Crashpad-specific blocker:
  ```
  FAILED: obj/third_party/crashpad/crashpad/handler/handler/handler_main.o
  ../../third_party/crashpad/crashpad/util/misc/address_types.h:33:2: error: "Unhandled OS type"
  ../../third_party/crashpad/crashpad/util/misc/address_types.h:72:35: error: use of undeclared identifier 'VMSize'
  ```
- The same build also failed in minidump metadata code:
  ```
  FAILED: obj/third_party/crashpad/crashpad/minidump/minidump/minidump_misc_info_writer.o
  ../../third_party/crashpad/crashpad/minidump/minidump_misc_info_writer.cc:163:2: error: define kOS for this operating system
  ../../third_party/crashpad/crashpad/minidump/minidump_misc_info_writer.cc:188:55: error: use of undeclared identifier 'kOS'
  ```
- In `handler_main.cc`, once QNX stopped inheriting Linux handler sources, the file no longer had declarations for `CrashReportExceptionHandler` / `ExceptionHandlerServer` on QNX.

## Root cause

- The previous fix corrected Crashpad's *build graph* (`crashpad_is_linux`), but Crashpad's *generic platform layer* still only knew Apple, Windows, Linux/ChromeOS/Android, and Fuchsia.
- `util/misc/address_types.h` had no QNX branch, so the `VMAddress` / `VMSize` typedefs were never defined.
- `minidump_misc_info_writer.cc` had no `kOS = "qnx"` branch for Crashpad's debug build string.
- `handler_main.cc` only included Linux/macOS/Windows handler-side classes. Once the Linux branch was no longer selected, QNX had no declarations for the exception handler delegate/server types used by `handler_main.cc`.
- This is not a reason to re-enable Linux Crashpad internals on QNX. The tree already carries a QNX-specific no-op integration (`components/crash/core/app/crashpad_qnx.cc`). What is missing is just enough Crashpad core/platform definition to compile cleanly while that no-op integration remains in place.

## Fix pattern

- Add a QNX branch to Crashpad's core platform definitions:
  - `VMAddress = uintptr_t`
  - `VMSize = size_t`
- Add a QNX branch to minidump metadata:
  - `kOS = "qnx"`
- Provide **header-only QNX handler stubs** with the same surface names as the Linux handler classes used by `handler_main.cc`:
  - `handler/qnx/exception_handler_server.h`
  - `handler/qnx/crash_report_exception_handler.h`
- The stubs should be explicitly no-op / non-handling implementations. Their purpose is to keep the code compilable and preserve a future insertion point for a real QNX Crashpad backend, not to pretend Linux exception handling works on QNX.

## Applied change

- `third_party/crashpad/crashpad/util/misc/address_types.h`
  ```diff
  +#include <stddef.h>
  ...
  +#elif BUILDFLAG(IS_QNX)
  +using VMAddress = uintptr_t;
  +using VMSize = size_t;
  ```
- `third_party/crashpad/crashpad/minidump/minidump_misc_info_writer.cc`
  ```diff
  +#elif BUILDFLAG(IS_QNX)
  +  static constexpr char kOS[] = "qnx";
  ```
- `third_party/crashpad/crashpad/handler/handler_main.cc`
  - include QNX handler stub headers
  - allow QNX to use the existing signal-install path and handler-server variable declarations
- New files:
  - `third_party/crashpad/crashpad/handler/qnx/exception_handler_server.h`
  - `third_party/crashpad/crashpad/handler/qnx/crash_report_exception_handler.h`

## Verification

- `git apply --check cef/patch/patches/qnx/chromium/crashpad_qnx_core_stubs.patch` passed against the bootstrap-applied tree.
- A clean-tree bootstrap still succeeded after registering the patch in `patch.cfg` (`bootstrap exit: 0`).
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` removed the current Crashpad core/platform-definition signatures:
  - `"Unhandled OS type"` hits: `0`
  - `VMSize` hits: `0`
  - `define kOS for this operating system` hits: `0`
- The next blocker stayed in Crashpad util, but moved to smaller QNX compatibility gaps:
  ```
  FAILED: obj/third_party/crashpad/crashpad/util/util/file_writer.o
  ../../third_party/crashpad/crashpad/util/file/file_writer.cc:91:26: error: use of undeclared identifier 'IOV_MAX'
  ```
  plus follow-on failures in:
  - `util/misc/metrics.cc`
  - `util/misc/uuid.cc` (`error: Port.`)
- This confirms the QNX platform-definition / handler-stub layer is now in place and the build has advanced to a different Crashpad util portability surface.

## Files touched

- `cef/patch/patches/qnx/chromium/crashpad_qnx_core_stubs.patch`
- `cef/patch/patch.cfg`
- `cef/patch/qnx/chromium/new_files/third_party/crashpad/crashpad/handler/qnx/exception_handler_server.h`
- `cef/patch/qnx/chromium/new_files/third_party/crashpad/crashpad/handler/qnx/crash_report_exception_handler.h`
- `cef/docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-core-platform-definitions-and-handler-stubs.md`
- `cef/docs/qnx/build-error-index.md`

## Carry-forward note

This patch is intentionally a **stubbed bring-up step**. It does not claim that Crashpad exception handling is fully implemented on QNX. Its purpose is to keep the build graph and core metadata types consistent while preserving a clear path toward a future real QNX Crashpad backend.

## Related notes

- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-no-op-implementation-for-linux-only-sources.md`
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-client-header-needs-nativecpucontext-shim.md`
- `docs/qnx/history/build-errors/compile/build-graph/qnx-crashpad-buildconfig-exclude-linux.md`
