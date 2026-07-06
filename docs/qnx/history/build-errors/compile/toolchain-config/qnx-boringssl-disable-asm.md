# QNX BoringSSL generated assembly fails under clang temporary-file creation

- Date: 2026-07-04
- Signature: `obj/third_party/boringssl/boringssl_asm/*.o` / `clang: error: unable to make temporary file: No such file or directory`
- Stage: compile
- Category: toolchain-config
- Scope: `third_party/boringssl/BUILD.gn`

## Symptoms

After the compiler-rt builtins action was bypassed, `content/shell:content_shell`
failed compiling multiple generated BoringSSL assembly sources for QNX x64:

```text
FAILED: obj/third_party/boringssl/boringssl_asm/aes-gcm-avx512-x86_64-linux.o
../../third_party/llvm-build/Release+Asserts/bin/clang ... \
  -c ../../third_party/boringssl/src/gen/bcm/aes-gcm-avx512-x86_64-linux.S ...
clang: error: unable to make temporary file: No such file or directory
```

The failing sources were Linux/Apple generated GAS assembly selected by the
fallback `boringssl_asm` target.

## Root cause

QNX was falling into BoringSSL's generic non-Windows/non-MSan GAS assembly branch.
Those generated assembly files are targeted at Linux/Apple/Windows assembler
conditions, not a validated QNX assembler environment. For the QNX port, the
safe build target is BoringSSL's generic C implementation with
`OPENSSL_NO_ASM` propagated to consumers.

## Fix pattern

When bringing BoringSSL up for QNX, prefer an explicit `is_qnx` branch in
`third_party/boringssl/BUILD.gn` that mirrors the existing MSan no-assembly
shape:

- empty `source_set("boringssl_asm")`;
- `public_configs = [ ":no_asm_config" ]` so consumers see `OPENSSL_NO_ASM`;
- empty test support asm target.

## Applied change

Added CEF-managed patch:

- `cef/patch/patches/qnx/chromium/boringssl_disable_asm_qnx.patch`

and registered it in:

- `cef/patch/patch.cfg`

## Verification

Clean QNX bootstrap applied the patch and completed `gn gen`.
Subsequent `content/shell:content_shell` build commands contained
`-DOPENSSL_NO_ASM` and progressed beyond `third_party/boringssl/boringssl_asm`.

## Files touched

- `cef/patch/patches/qnx/chromium/boringssl_disable_asm_qnx.patch`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/history/archive/reviews/boringssl_getentropy_analysis.md`
- `docs/qnx/history/build-errors/compile/toolchain-config/qnx-compiler-rt-builtins-depfile-tmpdir.md`