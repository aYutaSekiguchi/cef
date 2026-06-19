# QNX ICU hidden-visibility duplicate symbols in libcef.so

## Failure signature

Stage: link
Category: duplicate-symbols
Target: `libcef.so` / `cefsimple`

Representative diagnostics:

```text
/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-ld: obj/third_party/icu/icuuc_private_hidden_visibility/uvectr64.o: in function `icu_77::UVector64::UVector64(int, UErrorCode&)':
third_party/icu/source/common/uvectr64.cpp:40: multiple definition of `icu_77::UVector64::UVector64(int, UErrorCode&)'; obj/third_party/icu/icuuc_private/uvectr64.o:first defined here
/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-ld: obj/third_party/icu/icuuc_private_hidden_visibility/stubdata.o:(.rodata+0x0): multiple definition of `icudt77_dat'; obj/third_party/icu/icuuc_private/stubdata.o:(.rodata+0x0): first defined here
```

`out/qnx_release/libcef.so.rsp` contains both normal and hidden ICU archives:

```text
obj/third_party/icu/libicui18n.a
obj/third_party/icu/libicuuc.a
obj/third_party/icu/libicui18n_hidden_visibility.a
obj/third_party/icu/libicuuc_hidden_visibility.a
```

## Root cause

This is not a generic upstream CEF Linux issue. Upstream CEF Linux can link the normal ICU archives and the hidden-visibility ICU archives in the same final link because the hidden variants are actually compiled with hidden symbol visibility.

`third_party/icu/BUILD.gn` has an ICU-local `visibility_hidden` config. ICU first removes Chromium's default `//build/config:symbol_visibility_hidden` from ICU component targets, then applies this local config to the hidden variants. Before the QNX fix the local config was:

```gn
config("visibility_hidden") {
  cflags = []
  if (is_mac || is_linux || is_chromeos || is_android || is_fuchsia) {
    cflags += [ "-fvisibility=hidden" ]
  }
}
```

QNX was omitted, so `icuuc_private_hidden_visibility` and `icui18n_hidden_visibility` had hidden target names but were built without `-fvisibility=hidden`. Their symbols remained default-visible and collided with the normal ICU archives when linked into `libcef.so`.

## Fix

Patch: `cef/patch/patches/qnx/chromium/icu_hidden_visibility_qnx.patch`

Change:

- Add `is_qnx` to ICU's local `visibility_hidden` config in `third_party/icu/BUILD.gn`:

```gn
if (is_mac || is_linux || is_chromeos || is_android || is_fuchsia || is_qnx) {
  cflags += [ "-fvisibility=hidden" ]
}
```

## build/config/compiler review

Reviewed `build/config/BUILDCONFIG.gn`, `build/config/gcc/BUILD.gn`, `build/config/compiler/BUILD.gn`, and `build/config/compiler/compiler.gni` for the same class of omission.

Findings:

- Chromium's default `//build/config/gcc:symbol_visibility_hidden` is already applied to QNX through `is_posix` in `BUILDCONFIG.gn`.
- `build/config/gcc: symbol_visibility_hidden` itself is OS-generic except for AIX and therefore already supports QNX.
- QNX-specific compiler/linker exceptions already exist in `build/config/compiler/BUILD.gn` for target selection, QNX startup-object executable-stack warnings, `-no-canonical-prefixes`, Fortify, and thin archives.
- The ICU failure was caused by ICU's target-local visibility config, not by a missing default compiler config.

No additional `build/config/compiler` patch is recommended for this specific duplicate-symbol issue.

## Verification

```text
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
ninja -C out/qnx_release obj/third_party/icu/icuuc_private_hidden_visibility/uvectr64.o
EXIT:0
```

The regenerated command includes `-fvisibility=hidden` for the hidden ICU object:

```text
ninja -C out/qnx_release -t commands obj/third_party/icu/icuuc_private_hidden_visibility/uvectr64.o | grep -o -- '-fvisibility=hidden'
-fvisibility=hidden
-fvisibility=hidden
```

## Search hints

```bash
rg -n "icuuc_private_hidden_visibility|icui18n_hidden_visibility|icudt77_dat|UVector64|visibility_hidden|multiple definition" docs/qnx/history/build-errors
```
