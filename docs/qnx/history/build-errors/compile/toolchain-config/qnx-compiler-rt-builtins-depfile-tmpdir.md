# QNX compiler-rt builtins action reruns and clang cannot make temporary files

- Date: 2026-07-04
- Signature: `//build/config/clang:qnx_compiler_builtins` / `clang: error: unable to make temporary file: No such file or directory`
- Stage: compile
- Category: toolchain-config
- Scope: `build/config/qnx/build_compiler_rt_builtins.py`

## Symptoms

`content/shell:content_shell` stopped early in the QNX toolchain helper target:

```text
FAILED: qnx_clang_rt/x86_64-unknown-nto/libclang_rt.builtins.a
python3 ../../build/config/qnx/build_compiler_rt_builtins.py ...
failed to compile .../compiler-rt/src/lib/builtins/x86_64/floatundidf.S
clang: error: unable to make temporary file: No such file or directory
```

Manual invocation of the same script could succeed, but the action reran on every
subsequent ninja invocation.

## Root cause

Two issues combined:

1. The helper wrote a depfile whose output target was absolute, while ninja's
   rule output is relative (`qnx_clang_rt/.../libclang_rt.builtins.a`). Ninja
   therefore considered the action dirty every time:

   ```text
   expected depfile ... to mention 'qnx_clang_rt/.../libclang_rt.builtins.a', got '/home/.../out/qnx_release/qnx_clang_rt/.../libclang_rt.builtins.a'
   ```

2. Clang's assembler temporary-file creation was flaky under the action's build
   environment unless a known-good temporary directory was provided.

## Fix pattern

For custom QNX toolchain actions:

- write depfile output targets exactly as ninja names them, not as resolved
  absolute paths;
- provide an action-owned `TMPDIR`/`TMP`/`TEMP` directory for subprocess clang;
- keep this helper deterministic by compiling compiler-rt builtins serially.

## Applied change

Updated the CEF-managed new file
`cef/patch/qnx/chromium/new_files/build/config/qnx/build_compiler_rt_builtins.py`:

- default `--jobs` to `1`;
- set subprocess `TMPDIR`, `TMP`, and `TEMP` to `.build/tmp`;
- preserve `args.output` as the depfile target while using the resolved output
  path for filesystem operations.

## Verification

After clean bootstrap, running the action twice produced no stale work on the
second check:

```text
ninja -C out/qnx_release -j<N> build/config/clang:qnx_compiler_builtins
ninja -C out/qnx_release -d explain -n build/config/clang:qnx_compiler_builtins
ninja: no work to do.
```

`content/shell:content_shell` then progressed beyond this target. Choose `<N>` per build-machine resources and current load; it is not part of the fix pattern.

## Files touched

- `cef/patch/qnx/chromium/new_files/build/config/qnx/build_compiler_rt_builtins.py`

## Related notes

- `docs/qnx/history/build-errors/compile/toolchain-config/qnx-boringssl-disable-asm.md`