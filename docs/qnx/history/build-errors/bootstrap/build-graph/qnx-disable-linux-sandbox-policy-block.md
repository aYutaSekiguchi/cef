# QNX bootstrap must exclude sandbox/policy's Linux sandbox block

- Date: 2026-06-09
- Signature: `//sandbox/policy:policy needs //sandbox/linux:suid_sandbox_client`
- Stage: bootstrap
- Category: build-graph
- Scope: `sandbox/policy/BUILD.gn`

## Symptoms

- `cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>` reached Phase 5 (`gn gen`) and then aborted with:
  ```
  ERROR Unresolved dependencies.
  //sandbox/policy:policy(//build/toolchain/qnx:clang_x64)
    needs //sandbox/linux:suid_sandbox_client(//build/toolchain/qnx:clang_x64)
  ```
- This happened **after** the QNX bootstrap already wrote the expected sandbox-disabling args into `out/qnx_release/args.gn`:
  ```gn
  v8_enable_sandbox = false
  use_seccomp_bpf = false
  compile_suid_client = false
  compile_credentials = false
  compile_syscall_broker = false
  ```
- The same bootstrap also disabled Linux desktop audio defaults on QNX:
  ```gn
  use_alsa = false
  use_pulseaudio = false
  ```
  and the previous `media/audio/libpulse_stubs/pulse_stubs.o` failure did not reappear.

## Root cause

- QNX is treated as Linux-like by GN for a number of build-file conditions (`is_linux` is true on QNX), even though the C++ platform layer does **not** expose Linux-only APIs such as seccomp, namespace clone flags, `sys/syscall.h`, or `sys/prctl.h`.
- `sandbox/policy/BUILD.gn` contains a large Linux sandbox block:
  ```gn
  if (is_linux || is_chromeos) {
    sources += [ "linux/bpf_*.cc", ... ]
    deps += [
      "//sandbox/linux:sandbox_services",
      "//sandbox/linux:seccomp_bpf",
      "//sandbox/linux:suid_sandbox_client",
    ]
  }
  ```
- Layer 1 of the QNX sandbox-disable fix sets `compile_suid_client = false` in `cef/tools/cef_create_projects_qnx.sh`, which correctly prevents `//sandbox/linux:suid_sandbox_client` from being generated in `sandbox/linux/BUILD.gn`.
- However, `sandbox/policy/BUILD.gn` still requests that target because its Linux block is keyed only on `is_linux || is_chromeos`, and QNX flows through `is_linux`. That leaves `gn gen` with an unresolved dependency before any compile step starts.

## Fix pattern

- Keep the bootstrap args change (layer 1):
  - `use_seccomp_bpf = false`
  - `compile_suid_client = false`
  - `compile_credentials = false`
  - `compile_syscall_broker = false`
- Add the minimal layer 2 build-graph exclusion at the first failing consumer:
  ```gn
  if ((is_linux || is_chromeos) && !is_qnx) {
  ```
  in `sandbox/policy/BUILD.gn`.
- Do **not** try to stub Linux sandbox headers or constants on QNX. The unresolved target is a build-graph problem, not a missing-header problem. The Linux sandbox stack (seccomp BPF, setuid sandbox, namespace sandbox, clone flags, Linux signal layout) is unsupported on QNX and should be excluded, not emulated.
- Do **not** broaden this into a full-tree `is_linux -> is_linux && !is_qnx` sweep until a concrete downstream blocker proves another consumer still routes Linux sandbox code into QNX. This note records the minimal second-layer exclusion that was actually required.

## Applied change

- `sandbox/policy/BUILD.gn`:
  ```diff
  -  if (is_linux || is_chromeos) {
  +  if ((is_linux || is_chromeos) && !is_qnx) {
  ```
- This excludes QNX from the Linux sandbox policy sources and deps while leaving Linux and ChromeOS unchanged.

## Verification

- **Before** the patch, a clean-tree bootstrap reproduced:
  ```
  ERROR Unresolved dependencies.
  //sandbox/policy:policy(//build/toolchain/qnx:clang_x64)
    needs //sandbox/linux:suid_sandbox_client(//build/toolchain/qnx:clang_x64)
  ```
- **After** the layer 1 args landed, the same failure still reproduced, proving layer 1 alone was insufficient.
- **After this patch**:
  1. `cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800` completed successfully (`bootstrap exit: 0`).
  2. `out/qnx_release/args.gn` contained the intended sandbox/audio disable knobs:
     ```gn
     v8_enable_sandbox = false
     use_seccomp_bpf = false
     compile_suid_client = false
     compile_credentials = false
     compile_syscall_broker = false
     use_alsa = false
     use_pulseaudio = false
     ```
  3. `./out/qnx_release/ninja_qnx.sh base_unittests` completed successfully (`base_unittests exit: 0`) and linked `./base_unittests` at `[2247/2247]` with no FAILED lines.
- The next blocker therefore moves out of bootstrap and back into the wider `cefsimple` build loop.

## Files touched

- `cef/tools/cef_create_projects_qnx.sh`
- `cef/patch/patches/qnx/chromium/sandbox_policy_disable_linux_sandbox_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/bootstrap/build-graph/qnx-disable-linux-sandbox-policy-block.md`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/research/impact-analysis.md`
- `docs/qnx/history/research/external-apis.md`
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-crashpad-no-op-implementation-for-linux-only-sources.md`
