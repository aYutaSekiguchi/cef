# QNX Chrome Apps deprecation Learn More URL constant

## Failure signature

Stage: compile
Target: `//chrome/browser/ui:ui`
Object: `force_installed_deprecated_apps_dialog_view.o`

```text
chrome/browser/ui/views/web_apps/force_installed_deprecated_apps_dialog_view.cc:78:30:
error: no member named 'kChromeAppsDeprecationLearnMoreURL' in namespace 'chrome'
```

## Root cause

After QNX began compiling the Win/Mac/Linux desktop Views source block in `chrome/browser/ui/BUILD.gn`, deprecated Chrome Apps dialog sources were compiled for QNX:

```text
views/web_apps/deprecated_apps_dialog_view.cc
views/web_apps/force_installed_deprecated_apps_dialog_view.cc
views/web_apps/force_installed_preinstalled_deprecated_app_dialog_view.cc
```

Those sources reference `chrome::kChromeAppsDeprecationLearnMoreURL`, but `chrome/common/url_constants.h` only exposed the constant for Win/Mac/Linux.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/chrome_apps_deprecation_url_constants_qnx.patch
```

Change the URL constant guard to include QNX:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
```

## Verification

```bash
autoninja -C out/qnx_release obj/chrome/browser/ui/ui/force_installed_deprecated_apps_dialog_view.o
```

The object builds successfully.

## Search hints

```bash
rg -n "kChromeAppsDeprecationLearnMoreURL|chrome_apps_deprecation_url_constants_qnx|force_installed_deprecated_apps_dialog_view" docs/qnx/history/build-errors
```
