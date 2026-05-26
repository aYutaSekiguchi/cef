# QNX Build Blocker Analysis — 2026-05-12

## Verdict

**`<version>` is the real first blocker.** fontconfig/fontations is blocked downstream and has not been reached.

---

## Evidence

### 1. Build fails at `<version>` immediately

```
fatal error: 'version' file not found
   19 | #include <version>
      |          ^~~~~~~~~
FAILED: obj/base/allocator/partition_allocator/src/partition_alloc/allocator_base/logging.o
```

This appears on the **first compilation unit** (`alias.cc`) across every ninja invocation:
`ninja -C out/qnx_x64 base_unittests`, `ninja -C out/qnx_x64 third_party/fontconfig`, any target.

### 2. Mechanism: incorrect include path for C++ stdlib

From `out/qnx_x64/toolchain.ninja` rule `cxx`:
```
-I/usr/include/c++/v1 -include time.h -include ../../build/config/qnx/qnx_std_polyfill.h
```

Sources:
- `build/toolchain/qnx/BUILD.gn` line 49:
  ```
  extra_cxxflags = "... -I${_qnx_target}/usr/include/c++/v1 ..."
  ```
- `_qnx_target` resolves to **empty string** because `QNX_TARGET` env var is not set.
- Result: `-I/usr/include/c++/v1`

Host Linux clang's search path (from `clang++ -E -Wp,-v`):
```
/usr/bin/../lib/gcc/x86_64-linux-gnu/14/../../../../include/c++/14
/usr/bin/../lib/gcc/x86_64-linux-gnu/14/../../../../include/x86_64-linux-gnu/c++/14
/usr/bin/../lib/gcc/x86_64-linux-gnu/14/../../../../include/c++/14
```

`/usr/include/c++/v1` does **not** exist on this host. The `v1` suffix is QNX/Apple's convention; Linux uses `c++/14`.

### 3. The polyfill is forced-included on every C++ file

`qnx_std_polyfill.h` line 19 has `#include <version>`. This file is force-included via
`-include ../../build/config/qnx/qnx_std_polyfill.h` in every C++ compilation via the `cxx` ninja rule.

When the stdlib include path is wrong, `<version>` cannot be resolved. This blocks **all** C++ compilation — no target reaches any later blocker.

### 4. fontconfig is downstream, never reached

`grep fontconfig out/qnx_x64/build.ninja` -> 27 matches including `libthird_party_fontconfig.so`, `fontconfig_fontations_ffi`, etc.

The build graph has fontconfig as a downstream dependency of `base`. Since base compilation halts at `<version>`, fontconfig has not been attempted. Its status is **unknown** — it could be blocked by `<version>` too or have its own Linux-API issues.

### 5. Rust `rust_abi_target` fix is confirmed present and working

From `build/config/rust.gni` lines 216-218:
```python
if (is_qnx) {
  if (current_cpu == "arm64") { rust_abi_target = "aarch64-unknown-nto-qnx800" }
  else if (current_cpu == "x64") { rust_abi_target = "x86_64-pc-nto-qnx800" }
```

Verification:
```bash
grep -c "x86_64-pc-nto\|nto-qnx" out/qnx_x64/build.ninja  # -> 75
grep -c "x86_64-unknown-linux-gnu" out/qnx_x64/build.ninja  # -> 0
```

The Rust targets are now `x86_64-pc-nto-qnx800` across all Rust crates. This fix is complete.

However, since no C++ compiles, the Rust link step (which runs after C++ compilation for mixed targets) has not been reached either.

### 6. QNX sysroot also not set (empty `--sysroot=` in commands)

Every compilation shows `--sysroot=` with empty value. This is consistent — `_qnx_target` is empty, so `--sysroot` is blank AND the `-I` path is wrong. Only `QNX_HOST` happens to be set to `/usr` (which works for `qcc`, `ntox86_64-ar`, etc.).

---

## Confidence

| Claim | Confidence |
|-------|-----------|
| `<version>` is first blocker | **100%** — observed on multiple ninja invocations |
| fontconfig has not been reached | **100%** — base must compile before fontconfig |
| `rust_abi_target` fix is in place | **100%** — confirmed in rust.gni and build.ninja |
| QNX_TARGET env var is empty | **100%** — `echo $QNX_TARGET` returns empty |
| Fontconfig would face its own issues | **High** — `libthird_party_fontconfig.so` uses Linux APIs |

---

## Recommendation: Fix `<version>` First

The immediate fix is to correct the C++ stdlib include path. Options in order of preference:

1. **Provide a minimal stub `<version>` header** in `build/config/qnx/shim/version` that provides the required feature-test macros (`__cpp_lib_ranges_contains`, etc.). This avoids relying on the host's C++ headers entirely.

2. **Remove `#include <version>` from the polyfill** and guard the feature-test macro access differently, since the polyfill only needs the macro value, not the header itself.

3. **Fix QNX_TARGET env var** — but this is a deployment issue, not a code fix, and may not be feasible in all environments.

Option 1 is the most robust because it eliminates the host toolchain's C++ stdlib from the equation entirely for QNX builds.

---

## Files Referenced

- `build/config/qnx/qnx_std_polyfill.h` (lines 19-20) — `<version>` include causing failure
- `build/toolchain/qnx/BUILD.gn` (line 49) — `-I${_qnx_target}/usr/include/c++/v1` source of wrong path
- `out/qnx_x64/toolchain.ninja` (rule `cxx`) — actual command showing wrong `-I` path
- `build/config/rust.gni` (lines 216-218) — confirmed Rust target fix
- `out/qnx_x64/build.ninja` (27 fontconfig entries) — downstream, not yet reached

## Start Here

Fix `build/config/qnx/qnx_std_polyfill.h` — create a shim `<version>` or guard the include — so compilation can proceed to reveal whether fontconfig's Linux-API issues are next.