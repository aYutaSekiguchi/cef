# gpu_test_config needs a QNX OS branch

- Date: 2026-06-09
- Signature: `gpu/config/gpu_test_config.cc:91:2: error: "unknown os"`
- Stage: compile
- Category: feature-guard
- Scope: `gpu/config/gpu_test_config.cc`

## Symptoms

After syncing QNX external sources via `cef/tools/qnx_sync_sources.sh` and building `mojo:mojo_unittests`, the build reaches `gpu/config` and fails with:

```text
FAILED: obj/gpu/config/config/gpu_test_config.o
../../gpu/config/gpu_test_config.cc:91:2: error: "unknown os"
```

## Root cause

`gpu/config/gpu_test_config.cc` maps compile-time OS buildflags to `GPUTestConfig::OS` values, but it only has branches for Windows, macOS, Android, Fuchsia, and iOS before the final hard error.

Although GN routes QNX through Linux-like file selection in many places, `BUILDFLAG(IS_LINUX)` is not true in C++ sources on QNX. Therefore QNX reaches the final `#error "unknown os"`.

## Fix pattern

Add an explicit `BUILDFLAG(IS_QNX)` branch before the final `#error`. Return `GPUTestConfig::kOsLinux` as a neutral placeholder because the QNX build currently has no GPU test infrastructure; this only lets the enum helper compile.

## Applied change

```diff
 #elif BUILDFLAG(IS_IOS)
   return GPUTestConfig::kOsIOS;
+#elif BUILDFLAG(IS_QNX)
+  // QNX is routed through is_linux by GN; report kOsLinux as a neutral
+  // placeholder so the gpu_test_config enumeration compiles on QNX.
+  return GPUTestConfig::kOsLinux;
 #else
 #error "unknown os"
 #endif
```

## Verification

With `third_party/epoll/src` present from `cef/tools/qnx_sync_sources.sh`, `mojo:mojo_unittests` advances past the `gpu_test_config.cc` `"unknown os"` failure. The next blocker is currently BoringSSL's `crypto/rand/getentropy.cc` calling unavailable `getentropy()` on QNX.

## Files touched

- `cef/patch/patches/qnx/chromium/gpu_test_config_qnx_os_branch.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/gpu-test-config-qnx-os-branch.md`
- `cef/docs/qnx/build-error-index.md`
