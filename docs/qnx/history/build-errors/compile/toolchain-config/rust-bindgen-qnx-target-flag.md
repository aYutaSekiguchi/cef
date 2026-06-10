# rust_bindgen needs QNX --target and platform defines for libclang

- Date: 2026-06-09
- Signature: `sys/compiler_gnu.h:60: error: Endian not defined` / `not configured for target` / `unknown type name '_Intleast8t'`
- Stage: compile
- Category: toolchain-config
- Scope: `build/rust/rust_bindgen_generator.gni`, `//mojo/public/rust/system:mojo_c_system_bindings_generator`

## Symptoms

After the ipcz core layer unblocks, `mojo:mojo_unittests` build moves into the Mojo C bindings generator and produces:

```text
FAILED: gen/mojo/public/rust/system/mojo_c_system_bindings_generator/bindings.rs
python3 ../../build/rust/gni_impl/run_bindgen.py ... -- ../../mojo/public/c/system/thunks.h --
...
/home/yuta/qnx800/target/qnx/usr/include/sys/compiler_gnu.h:60:3: error: Endian not defined
/home/yuta/qnx800/target/qnx/usr/include/sys/platform.h:428:3: error: not configured for target
/home/yuta/qnx800/target/qnx/usr/include/stdint.h:70:9: error: unknown type name '_Intleast8t'
fatal error: too many errors emitted, stopping now [-ferror-limit=]
```

## Root cause

- The QNX clang toolchain (`build/toolchain/qnx/BUILD.gn`) injects `--target=x86_64-unknown-nto` and the QNX platform defines (`-D__QNXNTO__ -D__QNX__ -D__LITTLEENDIAN__ -D__X86_64__`) via `extra_cflags` / `extra_cxxflags`. Those flags flow into the regular C/C++ compile action, but `rust_bindgen_generator` reads `{{cflags}}` / `{{cflags_c}}` for its libclang invocation and **those vars do not include the QNX extra flags**.
- As a result, libclang parses the QNX sysroot headers with no target and no endianness defined, and the first includes (`sys/compiler_gnu.h` -> `sys/platform.h` -> `stdint.h`) fail before they even reach `mojo/public/c/system/thunks.h`.

## Fix pattern

Inject the same target / defines the C++ compile uses directly into the bindgen invocation. The QNX toolchain already has authoritative values for these, so a small `is_qnx` block in `rust_bindgen_generator.gni` is enough.

## Applied change

- `build/rust/rust_bindgen_generator.gni`
  ```gn
  if (is_qnx) {
    if (current_cpu == "x64") {
      qnx_target = "x86_64-unknown-nto"
    } else if (current_cpu == "arm64") {
      qnx_target = "aarch64-unknown-nto"
    } else {
      qnx_target = ""
    }
    qnx_cpu_def = ""
    if (current_cpu == "x64") {
      qnx_cpu_def = "-D__X86_64__"
    } else if (current_cpu == "arm64") {
      qnx_cpu_def = "-D__AARCH64EL__"
    }
    if (qnx_target != "") {
      args += [
        "--target=" + qnx_target,
        "-D__QNXNTO__",
        "-D__QNX__",
        "-D__LITTLEENDIAN__",
        qnx_cpu_def,
      ]
    }
  }
  ```

## Verification

- `git apply --check` succeeds for the new patch.
- A clean-tree bootstrap with the new patch applied removes the `sys/compiler_gnu.h` / `sys/platform.h` / `stdint.h` failures and lets the Mojo C bindings generator emit a `bindings.rs` for QNX.
- The `mojo:mojo_unittests` build advances to the next blocker (the remaining C/C++ tests in `mojo/public/cpp`).

## Files touched

- `cef/patch/patches/qnx/chromium/rust_bindgen_qnx_target.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/toolchain-config/rust-bindgen-qnx-target-flag.md`
- `cef/docs/qnx/build-error-index.md`
