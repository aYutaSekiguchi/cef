# QNX Crashpad client header needs NativeCPUContext shim

- Date: 2026-06-07
- Signature: incomplete type 'crashpad::CrashpadClient' / NativeCPUContext used in type trait expression
- Stage: compile
- Category: platform-api-gap
- Scope: third_party/crashpad/crashpad/util/misc

## Symptoms

- `third_party/crashpad/crashpad/util/misc/capture_context.h:84:21: error: unknown type name 'NativeCPUContext'`
- The error was triggered transitively by `components/crash/core/app/crashpad_qnx.cc` including `third_party/crashpad/crashpad/client/crashpad_client.h`.
- The failure happened while building `obj/components/crash/core/app/app/crashpad_qnx.o`.

## Root cause

- Crashpad's `capture_context.h` only defines `NativeCPUContext` for Apple, Windows, Linux/ChromeOS, and Android.
- QNX was missing from both the `#include <ucontext.h>` branch and the `using NativeCPUContext = ucontext_t;` branch.
- Because the QNX no-op Crashpad implementation still needs to include `crashpad_client.h` for type compatibility, the missing alias broke the compile even after the larger Crashpad source swap.

## Fix pattern

- When a third-party header is only missing a platform typedef/alias, prefer the smallest QNX-specific header extension rather than adding more local stubs.
- Keep the no-op implementation in the QNX path, but make the shared header parse on QNX by adding the missing platform case.

## Applied change

- Added `BUILDFLAG(IS_QNX)` to `third_party/crashpad/crashpad/util/misc/capture_context.h` in both the `#include <ucontext.h>` block and the `NativeCPUContext` alias block.
- Saved the change as `patch/patches/qnx/chromium/crashpad_capture_context_qnx.patch`.
- Added a managed QNX shim file under `patch/qnx/chromium/new_files/build/config/qnx/shim/util/misc/capture_context.h` (used during bootstrap if the shim path is needed).
- Registered the patch in `cef/tools/cef_create_projects_qnx.sh` alongside the other Crashpad QNX fix.

## Verification

- `ninja -C out/qnx_release obj/components/crash/core/app/app/crashpad_qnx.o` completed successfully (exit 0).
- This confirmed the `NativeCPUContext` compile error was removed.
- The next blockers are outside Crashpad (`sandbox/linux/...` QNX missing-header / Linux-namespace issues).

## Files touched

- `third_party/crashpad/crashpad/util/misc/capture_context.h`
- `components/crash/core/app/crashpad_qnx.cc`
- `patch/patches/qnx/chromium/crashpad_capture_context_qnx.patch`
- `patch/qnx/chromium/new_files/build/config/qnx/shim/util/misc/capture_context.h`
- `tools/cef_create_projects_qnx.sh`

## Related notes

- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-no-op-implementation-for-linux-only-sources.md`
