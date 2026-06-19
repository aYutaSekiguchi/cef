# QNX libjpeg-turbo SIMD NASM symbols need ELF mode

## Failure signature

Stage: link
Category: toolchain-config
Target: `//cef:cefsimple` via `./libcef.so`

After the QNX-specific `cefsimple` entrypoint fix, the executable link failed with many `libcef.so` undefined references. The largest repeated group was libjpeg-turbo SIMD:

```text
./libcef.so: undefined reference to `jsimd_extbgrx_ycc_convert_sse2'
./libcef.so: undefined reference to `jsimd_ycc_extxrgb_convert_avx2'
./libcef.so: undefined reference to `jconst_fancy_upsample_sse2'
...
```

## Root cause

`//third_party/libjpeg_turbo:simd` compiled QNX C objects that reference ELF-style symbols without a leading underscore:

```text
U jsimd_extbgrx_ycc_convert_sse2
U jconst_fancy_upsample_sse2
```

But `//third_party/libjpeg_turbo:simd_asm` assembled NASM sources without defining `ELF` for QNX. The resulting QNX x64 objects exported Mach-O-style leading-underscore names:

```text
T _jsimd_extbgrx_ycc_convert_sse2
D _jconst_fancy_upsample_sse2
```

QNX x86/x64 uses ELF symbol naming, so the assembler target must follow the Linux/Android/Fuchsia/ChromeOS ELF branch, not the implicit fallback.

## Fix

Patch: `cef/patch/patches/qnx/chromium/libjpeg_turbo_elf_qnx.patch`

Patch root: `third_party/libjpeg_turbo`

Change `third_party/libjpeg_turbo/BUILD.gn`:

```gn
} else if (is_linux || is_android || is_fuchsia || is_chromeos || is_qnx) {
  defines += [ "ELF" ]
}
```

## Verification

Regenerate GN and rebuild the QNX libjpeg SIMD assembly archive:

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
buildtools/linux64/gn --root=. -q --regeneration gen out/qnx_release

ninja -C out/qnx_release obj/third_party/libjpeg_turbo/libsimd_asm.a
```

Then verify exported symbols are ELF-style and match the C references:

```bash
/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-nm -g \
  out/qnx_release/obj/third_party/libjpeg_turbo/libsimd_asm.a \
  | rg '(_)?jsimd_extbgrx_ycc_convert_sse2|(_)?jconst_fancy_upsample_sse2'
```

Expected/observed after fix:

```text
0000000000001240 T jsimd_extbgrx_ycc_convert_sse2
0000000000000000 R jconst_fancy_upsample_sse2
```

A full `ninja -C out/qnx_release cefsimple` verification was started after this fix and produced no diagnostics before the local timeout; the build was still progressing through a broad rebuild.

## Search hints

```bash
rg -n "jsimd_extbgrx_ycc_convert_sse2|jconst_fancy_upsample_sse2|libjpeg_turbo_elf_qnx|simd_asm|NASM|ELF" docs/qnx/history/build-errors
```
