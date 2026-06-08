# power_metrics EnergyMetricsProvider must return nullptr on QNX (no perf_event / RAPL support)

- Date: 2026-06-08
- Signature: 'linux/perf_event.h' file not found
- Stage: compile
- Category: platform-api-gap
- Scope: components/power_metrics/{BUILD.gn, energy_metrics_provider_linux.cc, energy_metrics_provider.cc}

## Symptoms

- Clean QNX `out/qnx_release` builds stopped at step 808/57352 with `FAILED: obj/components/power_metrics/power_metrics/energy_metrics_provider_linux.o` and the trailer `ninja: build stopped: subcommand failed.`
- The compile command for the failing TU is the standard QNX clang_x64 invocation (sysroot `/home/yuta/qnx800/target/qnx`, target `x86_64-unknown-nto`, the `qnx_std_polyfill.h` force-include wired through `build/toolchain/qnx/BUILD.gn`). All flags match the rest of the QNX tree.
- The compiler emitted:
  ```
  ../../components/power_metrics/energy_metrics_provider_linux.cc:8:10:
    fatal error: 'linux/perf_event.h' file not found
      8 | #include <linux/perf_event.h>
        |          ^~~~~~~~~~~~~~~~~~~~
  1 error generated.
  ```
- No other files in the same target fail in the same run; the failure is isolated to `energy_metrics_provider_linux.cc`.

## Root cause

- `components/power_metrics/energy_metrics_provider_linux.cc` is the Linux implementation of the `EnergyMetricsProvider` abstract base (the `Win` and `Mac` implementations live in `_win.cc` / `_mac.mm`). It uses Linux-only APIs that have no QNX equivalent:
  - `<linux/perf_event.h>` UAPI header (not shipped by QNX SDP 8).
  - `__NR_perf_event_open` syscall number and the corresponding `syscall(__NR_perf_event_open, ...)` invocation.
  - `perf_event_attr` struct and the `PERF_FLAG_FD_CLOEXEC` flag.
  - `/proc/sys/kernel/perf_event_paranoid` procfs entry (used to gate access to `perf_event_open`).
  - `/sys/bus/event_source/devices/power/...` sysfs paths for Intel RAPL energy metrics.
- The source is selected by `components/power_metrics/BUILD.gn:46-51` under `if (is_linux || is_chromeos)`. `BUILDCONFIG.gn:322` sets `is_linux = current_os == "linux" || is_qnx`, so on QNX this branch fires and the Linux source is added to the build.
- The `EnergyMetricsProvider` abstract base was designed with a null fallback in mind: `EnergyMetricsProvider::Create()` returns `nullptr` on platforms that have no concrete implementation, and `system_power_monitor.cc:214` consumes the factory result with the implicit `if (provider)` check that the rest of the file relies on (the abstract base is a `std::unique_ptr` that callers may receive as null).

## Fix pattern

- Exclude the Linux-only source from the QNX build with the same `&& !is_qnx` pattern used by `swiftshader_qnx_memfd.patch` for `Linux/MemFd.cpp`. Use parentheses to keep the operator precedence obvious to future readers:
  ```gn
  if ((is_linux || is_chromeos) && !is_qnx) { ... }
  ```
- Short-circuit the factory's `IS_LINUX || IS_CHROMEOS` branch to `return nullptr` on QNX with a nested `#if BUILDFLAG(IS_QNX)`. Guard the corresponding `#include "components/power_metrics/energy_metrics_provider_linux.h"` the same way so the unused header is not parsed.
- Do **not** add a new QNX implementation file: the abstract base's `nullptr` path is the intended escape hatch, and the chromium tree intentionally has no `_qnx.cc` for power_metrics. Adding a stub would be dead code.
- Do **not** stub `<linux/perf_event.h>` in the QNX shim: the struct is large, versioned, and structurally fragile to fake, and `system_power_monitor` already has a clean null path.

## Applied change

- `components/power_metrics/BUILD.gn` (1 line):
  ```diff
  -  if (is_linux || is_chromeos) {
  +  if ((is_linux || is_chromeos) && !is_qnx) {
       sources += [
         "energy_metrics_provider_linux.cc",
         "energy_metrics_provider_linux.h",
       ]
     }
  ```
- `components/power_metrics/energy_metrics_provider.cc` (two hunks, +9 / -2 lines):
  ```diff
   #elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  +#if BUILDFLAG(IS_QNX)
  +// QNX has no perf_event / RAPL support; the abstract provider returns
  +// nullptr on QNX (see Create() below and the BUILD.gn is_qnx guard).
  +#else
   #include "components/power_metrics/energy_metrics_provider_linux.h"
  +#endif
   #endif  // BUILDFLAG(IS_WIN)
  ...
   #elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  +#if BUILDFLAG(IS_QNX)
  +  return nullptr;
  +#else
     return EnergyMetricsProviderLinux::Create();
  +#endif
  ```

## Verification

- `git apply --check` and `git apply` both succeed against the current Chromium tree at the upstream `components/power_metrics/BUILD.gn` and `components/power_metrics/energy_metrics_provider.cc` revisions pinned by `CHROMIUM_BUILD_COMPATIBILITY.txt`. The hunk anchors (`@@ -41,7 +41,7 @@` and `@@ -8,7 +8,12 @@` / `@@ -21,7 +26,11 @@`) and three-line context are stable against the small line shifts expected from Chromium 147 → 147.0.7727.147 rebase work.
- Re-running the QNX build with the patch applied is expected to:
  - clear the `FAILED: obj/components/power_metrics/power_metrics/energy_metrics_provider_linux.o` line,
  - leave `system_power_monitor.{cc,h}` unaffected (the abstract `EnergyMetricsProvider` base and the `if (provider)` consumer pattern handle the null return by design),
  - leave all other QNX TUs (including `base/atomicops.cc`, which exercises the polyfilled `std::atomic_ref`) untouched.
- A pre-fix grep confirmed `energy_metrics_provider_linux.h` is `#include`d only from the two files modified in this patch (`energy_metrics_provider_linux.cc` and `energy_metrics_provider.cc`), so excluding the source does not break any other TU's compilation.

## Files touched

- `cef/patch/patches/qnx/chromium/power_metrics_energy_metrics_provider_linux_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/platform-api-gap/qnx-power-metrics-linux-perf-event-no-op-fallback.md`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/media-audio-parameters-stdatomic-ref-capability-gate.md` (companion fix in the same build run: std::atomic_ref polyfill capability gating)
- `cef/patch/patches/qnx/chromium/swiftshader_qnx_memfd.patch` (the canonical `&& !is_qnx` exclusion pattern that this fix mirrors)
- `cef/patch/patches/qnx/chromium/libsync_qnx_stub.patch` (alternative no-op source-set pattern, used when a target has QNX consumers but no Linux-header sources)
- `cef/patch/patches/qnx/chromium/webrtc_qnx_platform_thread_names.patch` (similar Linux-only syscall / header situation, but solved with a QNX replacement implementation rather than exclusion)
