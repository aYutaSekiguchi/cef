# SwiftShader LLVM ELF headers collide with QNX sysroot ELF macros

- Date: 2026-06-03
- Signature: expected identifier or redefinition of enumerator in llvm ELF.h
- Stage: compile
- Category: platform-api-gap
- Scope: SwiftShader LLVM and llvm-subzero

## Symptoms

- SwiftShader builds failed with ELF enumerator collisions such as `EV_NONE`, `ELFOSABI_GNU`, and `PT_ARM_EXIDX`.

## Root cause

- `qnx_macros.h` force-included `<sys/elf.h>`, which polluted every translation unit with QNX ELF macros.
- LLVM ELF headers then collided with those macro names when declaring their own enums.

## Fix pattern

- Remove broad force-include pollution at the source first.
- Keep small, explicit hygiene `#undef` blocks at sensitive consumer headers as a second line of defense.

## Applied change

- Removed `<sys/elf.h>` from `qnx_macros.h`.
- Added an explicit `<sys/elf.h>` include to `stack_trace_posix.cc`, which actually needed the types.
- Added QNX hygiene `#undef` blocks to the SwiftShader vendored LLVM ELF headers.

## Verification

- SwiftShader compiled past the previous ELF header collision point.

## Files touched

- `cef/patch/qnx/chromium/new_files/build/config/qnx/qnx_macros.h`
- `cef/patch/patches/qnx/chromium/stack_trace_posix_qnx_elf.patch`
- `cef/patch/patches/qnx/chromium/swiftshader_qnx_llvm_elf.patch`
- `cef/patch/patches/qnx/chromium/swiftshader_qnx_elf.patch`

## Related notes

- `docs/qnx/build-error-index.md`
