# Policy generator must treat QNX target_os as "linux" so kSensitivePolicies is non-empty

- Date: 2026-06-08
- Signature: no viable conversion from returned value of type 'const char *const[0]' to function return type 'base::span<const char *const>'
- Stage: compile
- Category: toolchain-config
- Scope: components/policy/tools/generate_policy_source.gni

## Symptoms

- Clean QNX `out/qnx_release` builds stopped at step 841/57557 (≈ 1.4 %) with `FAILED: obj/components/policy/generated/policy_constants.o` and the trailer `ninja: build stopped: subcommand failed.`
- The compile command for the failing TU is the standard QNX clang_x64 invocation (sysroot `/home/yuta/qnx800/target/qnx`, target `x86_64-unknown-nto`, the `qnx_std_polyfill.h` force-include wired through `build/toolchain/qnx/BUILD.gn`). All flags match the rest of the QNX tree.
- The compiler emitted:
  ```
  gen/components/policy/policy_constants.cc:32:10: error: no viable conversion
    from returned value of type 'const char *const[0]' to function return type
    'base::span<const char *const>'
     32 |   return kSensitivePolicies;
  ../../base/containers/span.h:1049:13: note: candidate template ignored:
    substitution failure [with N = 0]:
    zero-length arrays are not permitted in C++
  ```
- A clean Linux x64 chromium 147 build (Debug, `~/chromium/test/src/out/Debug_GN_x64`) does **not** reproduce the error. On Linux x64, `policy_constants.o` builds successfully and the generated `policy_constants.cc` contains a 19-entry `kSensitivePolicies` array.
- The earlier three QNX fixes (`audio_parameters.h` capability gate, `power_metrics` exclusion + nullptr fallback, `os_crypt.h` `BUILDFLAG(IS_QNX)` extension) all remain in place and unrelated to this regression.

## Root cause

- `components/policy/tools/generate_policy_source.gni:62` passes `--target-platform=$target_os` to `components/policy/tools/generate_policy_source.py`. On QNX, `target_os` is the string `"qnx"`.
- The generator's `PolicyDetails` class (line 141-142) decides whether each YAML policy is supported on the target via:
  ```python
  self.is_supported = (target_platform in self.platforms
                       or target_platform in self.future_on)
  ```
  The `platforms` set is populated from each YAML's `supported_on:` field. The Chromium 147 YAML templates only declare `chrome.linux`, `chrome.win`, `chrome.mac`, `chrome.*`, `chrome_os`, `android`, `ios`, `fuchsia` (and their `chrome.linux:147-`, `chrome.mac:147-`, etc. version-suffixed variants). There is no `chrome.qnx` entry.
- On QNX the generator's `target_platform` argument is therefore not a member of any policy's `platforms` set, so every `PolicyDetails.is_supported` is `False`. The `_WriteSensitivePoliciesSource` writer at line 1417 then emits an empty array:
  ```cpp
  const char* const kSensitivePolicies[] = {
  };
  ```
  C++23 rejects the empty `const char* const[0]` to `base::span<const char* const>` conversion in `policy_constants.cc:32`.
- Linux x64 (the same generator, the same JSON, the same `policy_templates.py`) does not hit this because its `--target-platform=linux` matches the `chrome.linux` / `chrome.*` entries in every policy's `supported_on:` list.
- The QNX port already declares `is_linux = current_os == "linux" || is_qnx` in `BUILDCONFIG.gn` (see `build_qnx_toolchain.patch`), so policy-related GN-level decisions on QNX already treat the platform as Linux-like. The only place where this Linux-like treatment was not propagated was the per-target generator argument.

## Verification of root cause

Direct execution of the generator on the same JSON, varying only `--target-platform`:

| `--target-platform` | `kSensitivePolicies` count | Output `policy_constants.cc` size |
|---|---|---|
| `linux` (Linux x64 build, or manual run) | 19 | 445,866 B |
| `qnx` (QNX bootstrap) | 0 | 12,135 B |

The generator's Python logic is identical; only the `target_platform` argument changes the result. The QNX bootstrap produces a QNX build output identical in content and size to a manual `--target-platform=qnx` run. After a full `out/qnx_release` cache clear, the bootstrap still produces the empty-array output because the QNX bootstrap unconditionally passes `target_os == "qnx"` through the unmodified `generate_policy_source.gni`.

## Fix pattern

- One-line override in the GN template that builds the generator's `args` list: substitute `linux` for `qnx` only when `target_os == "qnx"`. This keeps every other platform's behavior bit-for-bit identical.
- The fix lives in `components/policy/tools/generate_policy_source.gni` (a single ~10-line hunk, including a 7-line comment explaining the QNX-specific behavior). The Python generator, the YAML templates, the GN build, and the build.ninja are all left untouched.
- Do **not** modify `generate_policy_source.py` to accept `qnx` as a target. Adding `qnx` to its `PLATFORM_STRINGS` table would require also extending every YAML's `supported_on:` to list `chrome.qnx:N-` and would have a far wider blast radius (YAML schema, per-policy override review, future template-validation tooling). The QNX port is intentionally not first-class in upstream policy generation.
- Do **not** switch the QNX bootstrap to `--target-platform=linux` directly: that would require touching every action invocation that calls `generate_policy_source.py`, and the action's `target_os` comes from the toolchain. Centralizing the override in the GN template is the surgical change.

## Applied change

- `components/policy/tools/generate_policy_source.gni` (1 hunk, +11 / -1 lines):
  ```gn
        "--target-platform=" + target_os,
  +    "--target-platform=" +
  +        (target_os == "qnx" ? "linux" : target_os),
  +    # QNX: generate_policy_source.py does not know the "qnx"
  +    # target_platform. Policy YAML supported_on entries only list
  +    # chrome.linux / chrome.win / chrome.mac / chrome.*, so on QNX
  +    # every policy is filtered out and the generated kSensitivePolicies
  +    # array becomes zero-length. C++23 then rejects the empty
  +    # const char* const[0] -> base::span<const char* const> conversion in
  +    # policy_constants.cc:32. Fall back to "linux" so the generator
  +    # emits a non-empty array; the generated code is otherwise identical
  +    # because the QNX build treats linux as the closest supported
  +    # platform for policy filtering purposes.
  ```

## Verification

- `git apply --check` and `git apply` both succeed against the current Chromium tree at the upstream `components/policy/tools/generate_policy_source.gni` revision pinned by `CHROMIUM_BUILD_COMPATIBILITY.txt`.
- Re-running the QNX build with the patch applied is expected to:
  - regenerate `gen/components/policy/policy_constants.cc` with a 19-entry `kSensitivePolicies` array (matching Linux x64 output) instead of an empty one,
  - clear the `policy_constants.cc:32` `kSensitivePolicies` zero-length-array diagnostic,
  - leave all other QNX translation units unchanged (the override is scoped to one arg in one GN template).
- Upstream-changeset impact: the patch touches a 1-line GN template that is upstream-owned. The fix is the minimum surface area that brings QNX back to "linux-like" behavior for policy filtering, which is already the project's stated direction (`is_linux = current_os == "linux" || is_qnx` from `build_qnx_toolchain.patch`).

## Files touched

- `cef/patch/patches/qnx/chromium/policy_target_platform_qnx_fallback.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/toolchain-config/policy-target-platform-qnx-fallback.md`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/media-audio-parameters-stdatomic-ref-capability-gate.md` (companion fix in the same build run: capability-gate pattern)
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-power-metrics-linux-perf-event-no-op-fallback.md` (companion fix in the same build run: source-set exclusion)
- `docs/qnx/history/build-errors/compile/feature-guard/os-crypt-bUILDFLAG-IS-LINUX-qnx-extension.md` (companion fix in the same build run: BUILDFLAG guard extension)
- `cef/patch/patches/qnx/chromium/build_qnx_toolchain.patch` (defines `is_linux = current_os == "linux" || is_qnx`, the QNX-is-Linux-like framing this fix aligns with)
- `build/config/BUILDCONFIG.gn:322` (`is_linux = current_os == "linux" || is_qnx` — the upstream framing this fix is consistent with)
- `components/policy/tools/generate_policy_source.gni:62` (the one-line site this fix patches)
- `components/policy/tools/generate_policy_source.py:1411-1426` (the `_WriteSensitivePoliciesSource` writer whose output the fix turns back on for QNX)
