# os_crypt.h must include BUILDFLAG(IS_QNX) in its BUILDFLAG(IS_LINUX) guards

- Date: 2026-06-08
- Signature: no member named 'X' in 'OSCryptImpl' / use of undeclared identifier 'Y' (X, Y in {SetConfig, UseMockKeyStorageForTesting, ClearCacheForTesting, SetEncryptionPasswordForTesting, DeriveV11Key, v11_key_, config_, try_v11_, kDerivedKeyBytes, GetLock})
- Stage: compile
- Category: feature-guard
- Scope: components/os_crypt/sync/os_crypt.h

## Symptoms

- Clean QNX `out/qnx_release` builds stopped at step 6144/62653 (≈ 9.6 %) with `FAILED: obj/components/os_crypt/sync/sync/os_crypt_linux.o` and the trailer `ninja: build stopped: subcommand failed.`
- The compile command for the failing TU is the standard QNX clang_x64 invocation (sysroot `/home/yuta/qnx800/target/qnx`, target `x86_64-unknown-nto`, the `qnx_std_polyfill.h` force-include wired through `build/toolchain/qnx/BUILD.gn`). All flags match the rest of the QNX tree.
- The compiler emitted 18 diagnostics before bailing out at `-ferror-limit=`:
  ```
  ../../components/os_crypt/sync/os_crypt_linux.cc:72:31: error: no member named 'SetConfig' in 'OSCryptImpl'
  ../../components/os_crypt/sync/os_crypt_linux.cc:98:31: error: no member named 'UseMockKeyStorageForTesting' in 'OSCryptImpl'
  ../../components/os_crypt/sync/os_crypt_linux.cc:102:31: error: no member named 'ClearCacheForTesting' in 'OSCryptImpl'
  ../../components/os_crypt/sync/os_crypt_linux.cc:105:31: error: no member named 'SetEncryptionPasswordForTesting' in 'OSCryptImpl'
  ../../components/os_crypt/sync/os_crypt_linux.cc:143:7: error: use of undeclared identifier 'DeriveV11Key'
  ../../components/os_crypt/sync/os_crypt_linux.cc:144:12: error: use of undeclared identifier 'v11_key_'
  ... (12 more)
  fatal error: too many errors emitted, stopping now [-ferror-limit=]
  ```
- The other os_crypt sources in the same target (`key_storage_linux.cc`, `key_storage_config_linux.cc`, `os_crypt_mocker_linux.cc`) compile cleanly in the same run, so the failure is isolated to `os_crypt_linux.cc`.

## Root cause

- `components/os_crypt/sync/BUILD.gn` selects `os_crypt_linux.cc` for `if (is_linux && !is_castos)`. `BUILDCONFIG.gn:322` sets `is_linux = current_os == "linux" || is_qnx`, so the Linux source is added to the QNX build graph.
- `components/os_crypt/sync/os_crypt.h` gates every Linux-specific member of the `OSCryptImpl` class — `SetConfig`, `UseMockKeyStorageForTesting`, `ClearCacheForTesting`, `SetEncryptionPasswordForTesting`, `DeriveV11Key`, `v11_key_`, `config_`, `try_v11_`, `kDerivedKeyBytes`, `GetLock`, plus the `OSCrypt::SetConfig` free function and the `KeyStorageLinux` forward declaration — behind `#if BUILDFLAG(IS_LINUX)` (or `#if (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))`).
- `BUILDFLAG(IS_LINUX)` is implemented in `build/build_config.h` and resolves to 1 only when `OS_LINUX` is defined, which is gated on `defined(__linux__)`. The QNX target triple `x86_64-unknown-nto` (set by `build/toolchain/qnx/BUILD.gn` via `--target=x86_64-unknown-nto`) predefines `__QNXNTO__` (so `BUILDFLAG(IS_QNX) = 1`) but not `__linux__` (so `BUILDFLAG(IS_LINUX) = 0`).
- The net effect on QNX is a GN-level / C++-level split:
  - GN sees QNX as Linux (`is_linux = true`), so the Linux source is compiled.
  - C++ sees QNX as not-Linux (`BUILDFLAG(IS_LINUX) = 0`), so the class declaration hides the members the source references.
- The same source compiles cleanly on stock Linux because both sides agree.
- At runtime the existing `os_crypt_linux.cc::DeriveV11Key()` already falls back to the v10 hardcoded key when `KeyStorageLinux::CreateService` returns nullptr (no libsecret / kwallet on QNX), so no encryption is lost: the cookie / saved-password path is functional in v10 mode once the class declaration matches the source.

## Fix pattern

- Extend every `BUILDFLAG(IS_LINUX)` guard in `os_crypt.h` to also fire on `BUILDFLAG(IS_QNX)`. This is the same per-patch pattern used by `base_memory_misc_qnx.patch`, `base_files_qnx.patch`, `base_rand_util_qnx.patch`, `crashpad_capture_context_qnx.patch`, and `process_thread_qnx.patch`. Reuses `os_crypt_linux.cc` as-is; relies on its existing v10 fallback path at runtime.
- For the negative guard that scopes `OSCrypt::SetEncryptionAvailableForTesting` to non-Linux POSIX, also add `BUILDFLAG(IS_QNX)` to the negated clause so QNX ends up in the Linux group (whose `UseMockKeyStorageForTesting` / `ClearCacheForTesting` / `SetEncryptionPasswordForTesting` methods are a strict functional superset of the available-for-testing flag). Net effect on QNX: gains the Linux testing methods, loses `SetEncryptionAvailableForTesting` (test-only, no production callers).
- Do **not** add a `os_crypt_qnx.cc` / `os_crypt_qnx.h` pair. The class declaration is shared infrastructure; introducing a separate QNX class hierarchy would push `BUILDFLAG(IS_QNX)` branching into every caller, multiplying the surface area and diverging from upstream. A separate `.cc` would also duplicate the v10-only logic that `os_crypt_linux.cc::DeriveV11Key()` already provides as a fallback.
- Do **not** patch `build/build_config.h` to also define `OS_LINUX` on QNX. That would change `BUILDFLAG(IS_LINUX)` globally for every Linux-conditional in the chromium tree — including guards that should remain false on QNX (Linux-only syscalls, glibc extensions, etc.). The per-patch strategy isolates the change to the guards that legitimately apply to QNX.

## Applied change

- `components/os_crypt/sync/os_crypt.h` (8 hunks, +20 / -20 lines):
  - Hunk 1: `#if BUILDFLAG(IS_LINUX)` (forward decl `class KeyStorageLinux;`) → `#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)`.
  - Hunk 2: `#if BUILDFLAG(IS_LINUX)` (free function `OSCrypt::SetConfig`) → `#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)`.
  - Hunk 3: `#if (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))` (free-function testing methods: `UseMockKeyStorageForTesting`, `ClearCacheForTesting`, `SetEncryptionPasswordForTesting`) → `#if ((BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)) && !BUILDFLAG(IS_CASTOS))`.
  - Hunk 4: `#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !(BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))` (negative guard for `SetEncryptionAvailableForTesting`) → `#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE) && !((BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)) && !BUILDFLAG(IS_CASTOS))` (and the corresponding `#endif` comment).
  - Hunk 5: `#if BUILDFLAG(IS_LINUX)` (class `OSCryptImpl::SetConfig` method) → `#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)`.
  - Hunk 6: `#if (BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CASTOS))` (class testing methods) → `#if ((BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)) && !BUILDFLAG(IS_CASTOS))`.
  - Hunk 7: `#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_APPLE)` (class static `GetLock`) → `#if (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)) || BUILDFLAG(IS_APPLE)`.
  - Hunk 8: `#if BUILDFLAG(IS_LINUX)` (private members: `kDerivedKeyBytes`, `Pbkdf2`, `DeriveV11Key`, `v11_key_`, `try_v11_`, `config_`, `storage_provider_factory_for_testing_`) → `#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)` (opening and closing guard).

## Verification

- `git apply --check` and `git apply` both succeed against the current Chromium tree at the upstream `components/os_crypt/sync/os_crypt.h` revision pinned by `CHROMIUM_BUILD_COMPATIBILITY.txt`. Hunk anchors are pinned to surrounding lines that survive the small line shifts expected from Chromium 147 → 147.0.7727.147 rebase work.
- Re-running the QNX build with the patch applied is expected to:
  - clear the 18 `os_crypt_linux.cc` diagnostics (`SetConfig`, `DeriveV11Key`, etc. all become visible to the compiler),
  - leave `key_storage_linux.cc` / `key_storage_config_linux.cc` / `os_crypt_mocker_linux.cc` unchanged (they have no `BUILDFLAG(IS_LINUX)` guards of their own),
  - leave all other Chromium TUs unaffected (the change is purely additive to QNX-side `BUILDFLAG` evaluations; Linux / ChromeOS / Android / Fuchsia / Apple are byte-for-byte unchanged).
- Runtime impact on QNX: `OSCrypt::SetEncryptionAvailableForTesting` is no longer declared; `OSCrypt::UseMockKeyStorageForTesting` / `ClearCacheForTesting` / `SetEncryptionPasswordForTesting` are now declared. The production cookie / saved-password paths use the same `OSCrypt::EncryptString` / `DecryptString` / `SetRawEncryptionKey` / `GetRawEncryptionKey` free functions, which were already non-guarded and unchanged.

## Files touched

- `cef/patch/patches/qnx/chromium/os_crypt_bUILDFLAG_IS_LINUX_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/os-crypt-bUILDFLAG-IS-LINUX-qnx-extension.md`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/media-audio-parameters-stdatomic-ref-capability-gate.md` (companion fix in the same build run: capability-gate pattern, QNX polyfill is incomplete)
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-power-metrics-linux-perf-event-no-op-fallback.md` (companion fix in the same build run: previous failure in the build)
- `cef/patch/patches/qnx/chromium/base_memory_misc_qnx.patch` (canonical `|| BUILDFLAG(IS_QNX)` extension pattern that this fix mirrors)
- `cef/patch/patches/qnx/chromium/base_files_qnx.patch` (same pattern, multiple `BUILDFLAG(IS_LINUX)` guards in one file)
- `cef/patch/patches/qnx/chromium/crashpad_capture_context_qnx.patch` (same pattern, `|| BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_QNX)` four-way combination)
- `build/build_config.h` (the source of the `BUILDFLAG(IS_LINUX) / BUILDFLAG(IS_QNX)` buildflag implementations that this fix targets)
