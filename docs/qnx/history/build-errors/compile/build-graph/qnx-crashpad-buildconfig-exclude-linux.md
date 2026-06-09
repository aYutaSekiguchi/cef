# QNX must not inherit Crashpad's Linux platform classification

- Date: 2026-06-09
- Signature: `fatal error: 'features.h' file not found` / `fatal error: 'linux/futex.h' file not found`
- Stage: compile
- Category: build-graph
- Scope: `third_party/crashpad/crashpad/build/crashpad_buildconfig.gni`

## Symptoms

- After the Linux sandbox consumer cleanup, `cef_create_projects_qnx.sh` succeeded and `cefsimple` advanced past the previous `sandbox/linux/services/*` failures.
- The next compile-stage blockers moved into Crashpad's Linux-specific compat/client sources:
  ```
  FAILED: obj/third_party/crashpad/crashpad/compat/compat/mman_memfd_create.o
  ../../third_party/crashpad/crashpad/compat/linux/sys/mman.h:20:10: fatal error: 'features.h' file not found
  ```
  and
  ```
  FAILED: obj/third_party/crashpad/crashpad/client/client/crashpad_client_linux.o
  ../../third_party/crashpad/crashpad/client/crashpad_client_linux.cc:20:10: fatal error: 'linux/futex.h' file not found
  ```
- The failing targets were reached from `//cef:cefsimple` through Crashpad's handler/client/util chain, not through the already-fixed `components/crash/core/app/crashpad_linux.cc` path.

## Root cause

- Crashpad has its own platform classification file: `third_party/crashpad/crashpad/build/crashpad_buildconfig.gni`.
- In Chromium builds it defined:
  ```gn
  crashpad_is_linux = is_linux || is_chromeos
  ```
- On QNX, GN sets `is_linux = true`, so Crashpad internally classified QNX as Linux.
- That caused multiple Linux-only source selections inside `third_party/crashpad/crashpad/*/BUILD.gn`, including:
  - `client/crashpad_client_linux.cc`
  - `compat/linux/sys/mman_memfd_create.cc`
  - `compat/linux/sys/mman.h`
  - `compat/linux/sys/user.h`
  - Linux handler sources under `handler/linux/*`
- These sources expect Linux/glibc headers and APIs (`features.h`, `linux/futex.h`, Linux futex/syscall semantics), none of which are provided by QNX.
- This is distinct from the earlier `components/crash/core/app` fix. That prior patch replaced Chromium's top-level Linux Crashpad integration with `crashpad_qnx.cc`, but Crashpad's own internal buildconfig still classified QNX as Linux and kept pulling Linux implementation files into the graph.

## Fix pattern

- Exclude QNX from Crashpad's Linux platform predicate at the source of the decision:
  ```gn
  crashpad_is_linux = (is_linux && !is_qnx) || is_chromeos
  ```
- Keep ChromeOS in the Linux set.
- Do **not** try to shim `features.h` or `linux/futex.h` on QNX.
- Do **not** remove Crashpad from QNX entirely. The tree already carries a QNX-specific no-op Crashpad path (`components/crash/core/app/crashpad_qnx.cc`) plus `capture_context.h` QNX support. This patch only prevents Crashpad's internal Linux-only implementation files from being selected accidentally on QNX.
- This preserves a future migration path where QNX-specific Crashpad behavior can be added explicitly, instead of inheriting Linux behavior implicitly.

## Applied change

- `third_party/crashpad/crashpad/build/crashpad_buildconfig.gni`:
  ```diff
  -  crashpad_is_linux = is_linux || is_chromeos
  +  crashpad_is_linux = (is_linux && !is_qnx) || is_chromeos
  ```

## Verification

- `git apply --check cef/patch/patches/qnx/chromium/crashpad_buildconfig_disable_linux_qnx.patch` passed against the clean Chromium tree.
- A clean-tree bootstrap still succeeded after registering the patch in `patch.cfg` (`bootstrap exit: 0`).
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` removed the current Crashpad Linux residual signatures:
  - `features.h` hits: `0`
  - `linux/futex.h` hits: `0`
  - `crashpad_client_linux.cc` hits: `0`
  - `mman_memfd_create` hits: `0`
- The next blocker stayed within Crashpad, but moved to a different root cause:
  ```
  FAILED: obj/third_party/crashpad/crashpad/handler/handler/handler_main.o
  ../../third_party/crashpad/crashpad/util/misc/address_types.h:33:2: error: "Unhandled OS type"
  ```
  and
  ```
  FAILED: obj/third_party/crashpad/crashpad/minidump/minidump/minidump_misc_info_writer.o
  ../../third_party/crashpad/crashpad/minidump/minidump_misc_info_writer.cc:163:2: error: define kOS for this operating system
  ```
- This confirms the Linux compat/client source selection problem was removed, and the next blocker is a separate Crashpad platform-definition gap rather than a residual Linux header path.

## Files touched

- `cef/patch/patches/qnx/chromium/crashpad_buildconfig_disable_linux_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/build-graph/qnx-crashpad-buildconfig-exclude-linux.md`
- `cef/docs/qnx/build-error-index.md`

## Carry-forward note

This patch intentionally narrows Crashpad's Linux predicate for QNX; it does not declare QNX permanently unsupported by Crashpad. If a future QNX-native Crashpad integration is added, the likely follow-up is to introduce explicit QNX platform handling in Crashpad's buildconfig and source selection rather than reusing Linux branches.

## Related notes

- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-no-op-implementation-for-linux-only-sources.md`
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-client-header-needs-nativecpucontext-shim.md`
- `docs/qnx/history/build-errors/compile/build-graph/qnx-disable-linux-sandbox-consumers.md`
- `docs/qnx/history/research/external-apis.md`
