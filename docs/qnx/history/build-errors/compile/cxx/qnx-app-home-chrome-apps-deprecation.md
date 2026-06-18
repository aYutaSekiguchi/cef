# QNX: App Home Chrome Apps deprecation guards

## Stage

- stage: compile
- category: cxx / undeclared helper and feature symbol
- target: `obj/chrome/browser/ui/webui/app_home/impl/app_home_page_handler.o`

## Failure signature

```text
../../chrome/browser/ui/webui/app_home/app_home_page_handler.cc:128:21: error: no member named 'IsExtensionUnsupportedDeprecatedApp' in namespace 'extensions'

../../chrome/browser/ui/webui/app_home/app_home_page_handler.cc:156:46: error: no member named 'kChromeAppsDeprecation' in namespace 'features'
```

## Root cause

App Home was enabled for QNX, so `app_home_page_handler.cc` is compiled. That
implementation uses Chrome Apps deprecation support to label and block legacy
Chrome apps:

- `extensions::IsExtensionUnsupportedDeprecatedApp()`
- `features::kChromeAppsDeprecation`

Both were guarded for Win/Mac/Linux desktop builds only. QNX was not in the
allowed set even though App Home now compiles on QNX.

## Fix

Extend the relevant desktop guards to include `BUILDFLAG(IS_QNX)` in:

- `chrome/browser/web_applications/extension_status_utils.h`
- `chrome/browser/web_applications/extensions/extension_status_utils.cc`
- `chrome/common/chrome_features.h`
- `chrome/common/chrome_features.cc`

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/ui/webui/app_home/impl/app_home_page_handler.o \
  obj/chrome/browser/web_applications/extensions/extensions/extension_status_utils.o \
  obj/chrome/common/chrome_features/chrome_features.o
```

Result: `EXIT:0`; no C++ errors emitted.

## Search hints

```bash
rg -n "IsExtensionUnsupportedDeprecatedApp|kChromeAppsDeprecation|app_home_page_handler" docs/qnx/history/build-errors
```
