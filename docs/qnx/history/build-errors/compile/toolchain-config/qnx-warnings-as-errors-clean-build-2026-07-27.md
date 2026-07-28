# QNX clean `ceftests` build is warning-clean with warnings treated as errors

- Date: 2026-07-27
- Signature: `-Werror`, `treat_warnings_as_errors=true`, and the first warning emitted by a clean `ceftests` build
- Stage: compile
- Category: toolchain-config
- Scope: QNX Release `ceftests` clean build

## Symptoms

- The QNX project generator set `treat_warnings_as_errors=false`, so compiler
  warnings accumulated without blocking builds.
- Enabling warnings-as-errors exposed 29 independent warning sites across
  Chromium, CEF, V8, ANGLE, Perfetto, Mojo, GPU, Blink, and bundled libraries.
- Unrestricted Ninja parallelism also made the build host unstable, so the
  review build needed an explicit concurrency limit.

## Root cause

The warning sites fell into four recurring classes:

- platform and feature guards did not match the declarations or call sites
  selected for QNX;
- QNX-only early returns or stubs left unreachable or unused non-QNX code in
  the translation unit;
- portable-looking aggregate, type, and ABI assumptions were not valid for
  QNX headers;
- process-lifetime objects used constructors or destructors that Chromium's
  warning policy rejects.

The project-level issue was that `treat_warnings_as_errors=false` hid all four
classes until the full target graph was compiled.

## Fix pattern

- Keep `treat_warnings_as_errors=true` in the generated QNX GN arguments.
- Build with `-j10` to bound host load.
- Fix the first warning, rebuild its narrow object target, then resume the
  broad target.
- Align definitions, declarations, and switch cases to the build flag that
  actually owns the feature.
- Use explicit QNX implementations or compile-time `#else` branches when the
  common implementation is not valid on QNX.
- Correct initialization, lifetime, and type semantics at the source instead
  of adding warning pragmas or diagnostic suppressions.

## Applied change

The clean build exposed and resolved these warning signatures in order:

| # | Stable signature | Durable fix |
|---:|---|---|
| 1 | Perfetto pessimizing `std::move` on return | Return the local result directly. |
| 2 | ANGLE QNX terminate capture global constructor | Install the handler explicitly from the existing TLS initialization path. |
| 3 | cpuinfo QNX tautological cache-index comparison | Remove dead validation and use the decoded cache ordering already consumed by the fork. |
| 4 | dav1d `_POSIX_C_SOURCE` redefinition | Do not add the older POSIX feature define for QNX. |
| 5 | V8 stack-trace unused demangling constants | Compile them only with the execinfo implementation that consumes them. |
| 6 | V8 duplicate QNX `kNoThread` | Remove the obsolete duplicate sentinel. |
| 7 | fontconfig QNX `dirent::d_name` bounds warning | Compare the readdir-provided name with `strcmp`. |
| 8 | allocator-shim exception specification mismatch | Add the standard `noexcept` specification to nothrow allocation overloads. |
| 9 | V8 sampler unused QNX `mcontext` | Extract x86_64 register state from QNX `mcontext_t`. |
| 10 | heap-profiler unreachable non-QNX body | Put the normal collection path in the compile-time `#else`. |
| 11 | heap-profiler unused helper | Compile the helper only outside QNX. |
| 12 | GPU `GpuMemoryBufferType::NATIVE_PIXMAP` unhandled | Guard the diagnostic switch case with `IS_OZONE`, matching the enum. |
| 13 | GPU init unused `filter_set` | Declare shared filter state only when Vulkan or ChromeOS Dawn installs a filter. |
| 14 | Mojo QNX `iovec` missing braces | Zero-initialize the aggregate and assign `iov_base` and `iov_len` explicitly. |
| 15 | sandbox `kScreenAI` unhandled switch | Give QNX an explicit `kScreenAI` mapping while keeping print-backend guards separate. |
| 16 | QNX canvas unused `kScreenFormat` | Remove the dead constant. |
| 17 | QNX event source unused handled-event table | Remove the dead table. |
| 18 | `about_flags` unused unsupported flag groups | Exclude unavailable QNX flag groups at compile time. |
| 19 | pseudonymization salt unreachable body | Put the non-QNX shared-memory import in `#else`. |
| 20 | spelling submenu unused private fields | Keep only proxy/model state in the QNX stub and guard desktop state fields. |
| 21 | profile disclaimer exit-time destructor | Store the process-lifetime account ID in `base::NoDestructor`. |
| 22 | metrics `PLATFORM_QNX` unhandled switch | Return the `QNX` platform display string. |
| 23 | Blink QNX layout-theme exit-time destructor | Use the existing static-ref lifetime pattern used by the Linux-style theme. |
| 24 | supervised-user QNX non-void path without return | Add a QNX handler whose unsupported local-approval callback returns `false`. |
| 25 | CEF OSR video consumer unused `pixel_format` | Limit accelerated-paint conversion to platforms with exportable native pixmap handles. |
| 26 | Chrome main delegate unused profiling helpers | Match helper definitions to their Linux/ChromeOS zygote callers. |
| 27 | QNX `ranges::contains` signed comparison | Compare through `std::ranges::equal_to`. |
| 28 | WebUI controller factory unused IWA favicon helper | Match the helper definition guard to its supported call sites. |
| 29 | web-app dialog `kCreateShortcut` unhandled switch | Enable the QNX shortcut case while retaining ChromeOS-only metrics guards. |

No warning pragma, `-Wno-*` flag, or diagnostic suppression was added.

## Verification

- Clean QNX source synchronization and bootstrap completed with 497 managed
  patches considered, 477 applied, 20 already present/skipped, and 0 failed.
- GN generation completed with 33,074 targets and
  `treat_warnings_as_errors=true`.
- `base_unittests` built successfully.
- The broad validation command completed all 32,973 steps:

  ```bash
  ./out/qnx_release/ninja_qnx.sh ceftests -j10 -k1 2>&1 | tee build.log
  ```

- The final two steps linked `libcef.so` and `ceftests` successfully.
- The managed patch-format validator accepted all 515 CEF patch files.
- `git diff --check` and reverse-apply checks for the changed patches passed.

## Files touched

- `cef/tools/cef_create_projects_qnx.sh`
- `cef/patch/patch.cfg`
- `cef/patch/patches/qnx/cef_video_consumer_osr_supported_platforms.patch`
- `cef/patch/patches/qnx/cpuinfo_qnx_cache_index_cleanup.patch`
- `cef/patch/patches/qnx/fontconfig_dirent_d_name_qnx.patch`
- `cef/patch/patches/qnx/chromium/`
- `cef/patch/qnx/chromium/new_files/`
- `cef/libcef/browser/osr/video_consumer_osr.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/build-and-toolchain.md`
- `docs/qnx/patch-hygiene.md`
