# QNX must exclude Linux sandbox consumers while leaving room for a future QNX sandbox

- Date: 2026-06-09
- Signature: `sandbox/linux/services/syscall_wrappers.cc:12:10: fatal error: 'sys/syscall.h' file not found`
- Stage: compile
- Category: build-graph
- Scope: Linux sandbox consumer targets (`content/common`, `services/network`, `services/audio`, `chrome/common`, `content/utility`, `components/on_device_translation`, `screen_ai`, `//sandbox` router)

## Symptoms

- After the bootstrap-stage fix `qnx/chromium/sandbox_policy_disable_linux_sandbox_qnx` landed, `cef/tools/cef_create_projects_qnx.sh` succeeded and `./out/qnx_release/ninja_qnx.sh base_unittests` linked successfully.
- The next wider build (`./out/qnx_release/ninja_qnx.sh cefsimple`) still failed early in the compile phase with multiple Linux sandbox implementation sources:
  ```
  FAILED: obj/sandbox/linux/sandbox_services/syscall_wrappers.o
  ../../sandbox/linux/services/syscall_wrappers.cc:12:10: fatal error: 'sys/syscall.h' file not found

  FAILED: obj/sandbox/linux/sandbox_services/scoped_process.o
  ../../sandbox/linux/services/scoped_process.cc:11:10: fatal error: 'sys/syscall.h' file not found

  FAILED: obj/sandbox/linux/sandbox_services/proc_util.o
  ../../sandbox/linux/services/proc_util.cc:105:50: error: no member named 'd_type' in 'dirent'
  ../../sandbox/linux/services/proc_util.cc:105:60: error: use of undeclared identifier 'DT_LNK'
  ```
- Audio regressions were already gone (`pulse/pulseaudio.h` and ALSA header failures no longer appeared), so this was a pure Linux sandbox residual path.

## Root cause

- The bootstrap fix removed QNX from `sandbox/policy/BUILD.gn`'s Linux sandbox policy block and disabled `use_seccomp_bpf`, `compile_suid_client`, `compile_credentials`, and `compile_syscall_broker` in `args.gn`.
- That was sufficient for `gn gen` and `base_unittests`, but not for the wider `cefsimple` graph. Several downstream consumer targets still had direct Linux-only deps such as:
  - `//sandbox/linux:sandbox_services`
  - `//sandbox/linux:seccomp_bpf`
  - Linux sandbox hook source files (`*_sandbox_hook_linux.cc`)
  - the `//sandbox` meta-target, which on QNX still routed to `//sandbox/linux:sandbox`
- `gn path out/qnx_release //cef:cefsimple //sandbox/linux:sandbox_services --all` showed the remaining immediate predecessors on the `cefsimple` path:
  - `//content/common:common`
  - `//services/network:network_service`
  - `//services/network:network_sandbox_hook`
  - `//services/audio:audio`
  - `//chrome/common:common_lib`
  - `//components/on_device_translation/service:on_device_translation_service`
  - `//content/utility/speech:speech_recognition_sandbox_hook`
  - `//services/screen_ai:screen_ai_sandbox_hook`
  - `//sandbox/linux:sandbox`
  - `//sandbox/linux:seccomp_bpf`
- These are all build-graph issues: QNX is treated as Linux-like by GN (`is_linux` true on QNX), but the referenced implementation sources are Linux-specific and rely on APIs QNX does not provide (`sys/syscall.h`, `sys/prctl.h`, namespace clone flags, `dirent.d_type`, Linux signal numbering, etc.).

## Fix pattern

- Exclude QNX from Linux sandbox **consumer** branches rather than stubbing Linux sandbox internals.
- Keep the generic `//sandbox` and `//sandbox/policy` architecture conceptually intact for future QNX work. The patch only removes QNX from the *current Linux-specific wiring*.
- For Linux-only consumer blocks, convert:
  ```gn
  if (is_linux || is_chromeos) {
  ```
  into:
  ```gn
  if ((is_linux || is_chromeos) && !is_qnx) {
  ```
- For the `//sandbox` router, preserve Android while excluding QNX from the Linux branch:
  ```gn
  } else if (is_android || ((is_linux || is_chromeos) && !is_qnx)) {
  ```
- This leaves a clear future insertion point for a QNX-native sandbox implementation, e.g. an explicit `is_qnx` branch in `//sandbox` or dedicated QNX hook targets, without carrying Linux-only consumer edges in the meantime.

## Applied change

The patch updates the Linux sandbox consumer conditions in:

- `sandbox/BUILD.gn`
- `services/audio/BUILD.gn`
- `services/network/BUILD.gn`
- `chrome/common/BUILD.gn`
- `components/on_device_translation/service/BUILD.gn`
- `content/utility/BUILD.gn`
- `content/utility/speech/BUILD.gn`
- `services/screen_ai/BUILD.gn`

`content/common/BUILD.gn` uses the same `&& !is_qnx` sandbox-consumer
condition, but that hunk is now carried by the earlier
`content_common_font_list_fontconfig_qnx.patch` because the fontconfig wiring
also updates the adjacent `content/common` Linux/ChromeOS branch. Keeping the
same hunk in both patches made clean bootstrap fail with
`content/common/BUILD.gn: patch does not apply`.

Representative hunks:

```diff
-  } else if (is_linux || is_chromeos || is_android) {
+  } else if (is_android || ((is_linux || is_chromeos) && !is_qnx)) {
     public_deps = [ "//sandbox/linux:sandbox" ]
   }
```

```diff
-  if (is_linux || is_chromeos) {
+  if ((is_linux || is_chromeos) && !is_qnx) {
     deps += [
       ":network_sandbox_hook",
       "//sandbox/linux:sandbox_services",
```

```diff
-source_set("speech_recognition_sandbox_hook") {
+if ((is_linux || is_chromeos) && !is_qnx) {
+  source_set("speech_recognition_sandbox_hook") {
```

## Verification

- 2026-07-01 refresh: removed the duplicate `content/common/BUILD.gn` hunk
  now owned by `content_common_font_list_fontconfig_qnx.patch`; clean bootstrap
  had failed with `content/common/BUILD.gn: patch does not apply` when this
  later sandbox patch tried to re-apply the same condition change.
- Clean-tree bootstrap patch phase succeeded after the refresh:
  `431 patches total (364 applied, 67 skipped, 0 failed)`.
- `gn gen` completed and `cef_create_projects_qnx.sh` exited successfully.
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` no longer produced any of the previous Linux sandbox residual signatures:
  - `sandbox/linux/sandbox_services` hits: `0`
  - `sys/syscall.h` hits: `0`
  - `DT_LNK` hits: `0`
- The build advanced to a new, unrelated blocker in Crashpad's Linux compatibility layer:
  ```
  FAILED: obj/third_party/crashpad/crashpad/compat/compat/mman_memfd_create.o
  ../../third_party/crashpad/crashpad/compat/linux/sys/mman.h:20:10: fatal error: 'features.h' file not found

  FAILED: obj/third_party/crashpad/crashpad/client/client/crashpad_client_linux.o
  ../../third_party/crashpad/crashpad/client/crashpad_client_linux.cc:20:10: fatal error: 'linux/futex.h' file not found
  ```
- This confirms the Linux sandbox consumer path was removed from `cefsimple`, and the next blocker has moved into a different Linux-only residual area (Crashpad compat/client linux sources).

## Files touched

- `cef/patch/patches/qnx/chromium/sandbox_consumers_disable_linux_sandbox_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/build-graph/qnx-disable-linux-sandbox-consumers.md`
- `cef/docs/qnx/build-error-index.md`

## Carry-forward note

This patch is intentionally scoped to **Linux sandbox consumer exclusion**, not to a permanent “no sandbox on QNX” policy. If/when a QNX-native sandbox implementation is introduced, the expected follow-up is:

1. add explicit `is_qnx` branches in the relevant BUILD files,
2. route `//sandbox` to a QNX implementation target,
3. replace Linux-only `*_sandbox_hook_linux.cc` references with QNX equivalents where needed,
4. drop the `&& !is_qnx` exclusions once real QNX targets exist.

Until then, carrying Linux sandbox consumers in the QNX graph only produces Linux-header and Linux-namespace build failures.

## Related notes

- `docs/qnx/history/build-errors/bootstrap/build-graph/qnx-disable-linux-sandbox-policy-block.md`
- `docs/qnx/history/research/impact-analysis.md`
- `docs/qnx/history/research/external-apis.md`
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-no-op-implementation-for-linux-only-sources.md`
