# QNX: HaTS Autofill survey feature guards

## Stage

- stage: compile
- category: cxx / undeclared feature symbol
- target: `obj/chrome/browser/ui/hats/impl/survey_config.o`

## Failure signature

```text
../../chrome/browser/ui/hats/survey_config.cc:512:42: error: no member named 'kAutofillAddressSurvey' in namespace 'features'
  512 |   survey_configs.emplace_back(&features::kAutofillAddressSurvey,

../../chrome/browser/ui/hats/survey_config.cc:514:42: error: no member named 'kAutofillCardSurvey' in namespace 'features'

../../chrome/browser/ui/hats/survey_config.cc:516:42: error: no member named 'kAutofillPasswordSurvey' in namespace 'features'
```

## Root cause

`survey_config.cc` is built for QNX and registers desktop HaTS survey configs,
including the Autofill address/card/password surveys.

The corresponding feature declarations and definitions in
`chrome/common/chrome_features.{h,cc}` were guarded for:

```cpp
IS_WIN || IS_MAC || IS_LINUX || IS_CHROMEOS
```

QNX was not included, so the feature symbols were not declared even though the
survey config code was compiled.

## Fix

Extend the Autofill survey feature flag guards to include `BUILDFLAG(IS_QNX)`
in:

- `chrome/common/chrome_features.h`
- `chrome/common/chrome_features.cc`

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/ui/hats/impl/survey_config.o \
  obj/chrome/common/chrome_features/chrome_features.o
```

Result: `EXIT:0`; no C++ errors emitted.

## Search hints

```bash
rg -n "kAutofillAddressSurvey|kAutofillCardSurvey|kAutofillPasswordSurvey|survey_config" docs/qnx/history/build-errors
```
