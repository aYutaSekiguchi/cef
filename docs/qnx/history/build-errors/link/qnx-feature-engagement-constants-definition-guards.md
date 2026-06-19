# QNX feature_engagement constants need matching definition guards

## Failure signature

Stage: link
Category: build-graph / platform-guard
Target: `//cef:cefsimple` via `./libcef.so`

After libjpeg-turbo SIMD symbols were fixed, `cefsimple` still failed while linking against `libcef.so` with feature engagement constants unresolved:

```text
./libcef.so: undefined reference to `feature_engagement::kIPHDesktopCustomizeChromeAutoOpenFeature[abi:logically_const]'
./libcef.so: undefined reference to `feature_engagement::kIPHAutofillCardInfoRetrievalSuggestionFeature[abi:logically_const]'
./libcef.so: undefined reference to `feature_engagement::events::kSplitViewCreated'
```

## Root cause

Existing QNX patches extended `components/feature_engagement/public/feature_constants.h` and `event_constants.h` so desktop declarations were visible on QNX. The corresponding `.cc` files still excluded QNX from the same platform guards.

That created declarations without definitions: QNX desktop Chromium sources referenced these constants, the relevant archives were linked, but the constants were not compiled into those archives.

## Fix

Patches updated:

- `cef/patch/patches/qnx/chromium/feature_constants_qnx.patch`
- `cef/patch/patches/qnx/chromium/event_constants_qnx.patch`

Both patches now extend matching `.h` and `.cc` guards to include QNX:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_QNX)
```

and for the broader feature constants block:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA) || \
    BUILDFLAG(IS_QNX)
```

## Verification

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
ninja -C out/qnx_release obj/components/feature_engagement/public/libpublic.a
```

Symbols are now defined:

```text
0000000000000000 R feature_engagement::events::kSplitViewCreated
0000000000000000 D feature_engagement::kIPHDesktopCustomizeChromeAutoOpenFeature[abi:logically_const]
```

## Search hints

```bash
rg -n "kIPHDesktopCustomizeChromeAutoOpenFeature|kSplitViewCreated|feature_constants_qnx|event_constants_qnx|feature_engagement::events" docs/qnx/history/build-errors
```
