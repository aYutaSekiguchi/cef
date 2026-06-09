# update_client's kOs map needs a BUILDFLAG(IS_QNX) branch

- Date: 2026-06-08
- Signature: "unknown os" / use of undeclared identifier 'kArch'
- Stage: compile
- Category: feature-guard
- Scope: components/update_client/update_query_params.cc

## Symptoms

- Clean QNX `out/qnx_release` builds stopped at step 895/58997 (≈ 1.5 %) with `FAILED: obj/components/update_client/update_client/update_query_params.o` and the trailer `ninja: build stopped: subcommand failed.`
- The compile command for the failing TU is the standard QNX clang_x64 invocation (sysroot `/home/yuta/qnx800/target/qnx`, target `x86_64-unknown-nto`, the `qnx_std_polyfill.h` force-include wired through `build/toolchain/qnx/BUILD.gn`). All flags match the rest of the QNX tree.
- The compiler emitted four diagnostics before bailing out at `-ferror-limit=`:
  ```
  ../../components/update_client/update_query_params.cc:43:2: error: "unknown os"
     43 | #error "unknown os"
        |  ^
  ../../components/update_client/update_query_params.cc:46:1: error: expected expression
     46 | constexpr std::string_view kArch =
        | ^
  ../../components/update_client/update_query_params.cc:90:73: error: use of undeclared identifier 'kArch'
     90 |       "os=%s&arch=%s&osarch=%s&prod=%s%s&acceptformat=crx3,puff", kOs, kArch,
        |                                                                 ^~~~~
  ../../components/update_client/update_query_params.cc:118:10: error: use of undeclared identifier 'kArch'
    118 |   return kArch;
        |          ^~~~~
  4 errors generated.
  ```
- A clean Linux x64 chromium 147 build (`~/chromium/test/src/out/Debug_GN_x64`) does **not** reproduce the error. On Linux x64 the `IS_LINUX` branch fires and the file compiles successfully.
- The earlier four QNX fixes (`audio_parameters.h` capability gate, `power_metrics` exclusion + nullptr fallback, `os_crypt.h` `BUILDFLAG(IS_QNX)` extension, `policy_target_platform_qnx_fallback`) all remain in place and unrelated to this regression.

## Root cause

- `components/update_client/update_query_params.cc:32-43` builds a `constexpr std::string_view kOs` by walking a chain of `BUILDFLAG(IS_FOO)` branches:
  ```cpp
  constexpr std::string_view kOs =
  #if BUILDFLAG(IS_APPLE)
      "mac";
  #elif BUILDFLAG(IS_WIN)
      "win";
  #elif BUILDFLAG(IS_ANDROID)
      "android";
  #elif BUILDFLAG(IS_CHROMEOS)
      "cros";
  #elif BUILDFLAG(IS_LINUX)
      "linux";
  #elif BUILDFLAG(IS_FUCHSIA)
      "fuchsia";
  #elif BUILDFLAG(IS_OPENBSD)
      "openbsd";
  #else
  #error "unknown os"
  #endif
  ```
  None of the seven branches covers QNX.
- The first error (`#error "unknown os"`) at line 43 makes the preprocessor fail before the `kArch` definition on line 46 is parsed. The remaining two errors are the parser cascading on the unparseable `kArch` constexpr.
- This is the same `BUILDFLAG(IS_LINUX) = 0 on QNX` root cause documented in the `os_crypt_bUILDFLAG_IS_LINUX_qnx` and `policy_target_platform_qnx_fallback` notes: `build/build_config.h` defines `BUILDFLAG_INTERNAL_IS_LINUX()` only when `__linux__` is predefined, and the QNX `x86_64-unknown-nto` target predefines `__QNXNTO__` (so `BUILDFLAG(IS_QNX) = 1`) but not `__linux__`. The QNX port propagates the "treat QNX as Linux-like" framing at the GN level (`is_linux = current_os == "linux" || is_qnx` in `BUILDCONFIG.gn`, via `build_qnx_toolchain.patch`) but the `update_query_params.cc` author did not add a QNX branch when the string map was last touched.

## Fix pattern

- One-line addition: insert a new `#elif BUILDFLAG(IS_QNX) "qnx";` branch in the `kOs` string map, immediately before the existing `#elif BUILDFLAG(IS_LINUX)` branch. The `kArch` definition is unaffected because `ARCH_CPU_X86_64` is defined correctly for the QNX x64 target in `build/build_config.h:291` (via `defined(__x86_64__)`).
- This mirrors the established QNX port pattern of adding `BUILDFLAG(IS_QNX)` branches to compile-time feature maps that upstream forgot to extend. Past precedents:
  - `base_files_qnx.patch` — adds `BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)` to the file_enumerator path.
  - `process_thread_qnx.patch` — adds `BUILDFLAG(IS_QNX) || BUILDFLAG(IS_POSIX)` to ProcessIterator's per-platform block.
  - `os_crypt_bUILDFLAG_IS_LINUX_qnx.patch` — the `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)` extension of the OSCryptImpl class declaration.
  - `policy_target_platform_qnx_fallback.patch` — the parent decision that the QNX build already treats chrome.linux as its closest supported policy platform; the `os= "qnx"` string in the update query mirrors that decision (the server sees `os=qnx` rather than the misleading `os=linux`).
- Do **not** rename `os=linux` to `os=chromium.linux` or to `os=chromeos.linux`. The Omaha update server treats `os=linux` as a stable identifier across all chromium-on-linux-flavor builds. The "qnx" string is intentionally new so the server can route QNX-specific CRXes if/when CEF for QNX wants them.
- Do **not** delete the `#elif BUILDFLAG(IS_LINUX)` branch and replace it with the QNX one. QNX already shares the "linux" build with the QNX-specific `os= "qnx"` string only for the *update query*; other platform-conditional code paths (e.g. the policy filters from `policy_target_platform_qnx_fallback`) still want QNX to behave exactly like Linux. Per-site surgical addition is the right granularity.

## Applied change

- `components/update_client/update_query_params.cc` (1 hunk, +2 / -0 lines):
  ```diff
       "android";
   #elif BUILDFLAG(IS_CHROMEOS)
       "cros";
  +#elif BUILDFLAG(IS_QNX)
  +    "qnx";
   #elif BUILDFLAG(IS_LINUX)
       "linux";
   #elif BUILDFLAG(IS_FUCHSIA)
  ```

## Verification

- `git apply --check` and `git apply` both succeed against the current Chromium tree at the upstream `components/update_client/update_query_params.cc` revision pinned by `CHROMIUM_BUILD_COMPATIBILITY.txt`.
- Re-running the QNX build with the patch applied is expected to:
  - clear the `#error "unknown os"` diagnostic at line 43,
  - clear the cascading `kArch` "use of undeclared identifier" diagnostics at lines 90 and 118,
  - make `update_client` emit a CRX update query with the `os=qnx` field, which is the only behavioral change for production code.
- Linux x64 / macOS / Windows / Android / ChromeOS / Fuchsia / OpenBSD builds are byte-for-byte unchanged; the new branch only fires when `BUILDFLAG(IS_QNX)` is true.

## Files touched

- `cef/patch/patches/qnx/chromium/update_client_unknown_os_qnx_branch.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/update-client-unknown-os-qnx-branch.md`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/media-audio-parameters-stdatomic-ref-capability-gate.md` (companion fix: capability-gate pattern, fix #1 in this build run)
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-power-metrics-linux-perf-event-no-op-fallback.md` (companion fix: source-set exclusion, fix #2)
- `docs/qnx/history/build-errors/compile/feature-guard/os-crypt-bUILDFLAG-IS-LINUX-qnx-extension.md` (companion fix: BUILDFLAG guard extension, fix #3)
- `docs/qnx/history/build-errors/compile/toolchain-config/policy-target-platform-qnx-fallback.md` (companion fix: toolchain/args wrapper, fix #4)
- `cef/patch/patches/qnx/chromium/build_qnx_toolchain.patch` (parent decision: `is_linux = current_os == "linux" || is_qnx`, the "QNX is Linux-like" framing this fix follows)
- `components/update_client/update_query_params.cc:32-43` (the kOs chain this fix extends)
- `build/build_config.h:291` (`#define ARCH_CPU_X86_64 1` via `defined(__x86_64__)`, which the QNX x64 target predefines; explains why the kArch chain does not need a QNX branch)
- `build/build_config.h:84` (`OS_LINUX` is gated on `defined(__linux__)`, which is why the `BUILDFLAG(IS_LINUX)` branch never fires on QNX)
- `build/config/BUILDCONFIG.gn:322` (`is_linux = current_os == "linux" || is_qnx`, the upstream framing this fix aligns with)
