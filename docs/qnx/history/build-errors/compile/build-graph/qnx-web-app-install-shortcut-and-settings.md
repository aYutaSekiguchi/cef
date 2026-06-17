# QNX: web_app install shortcut flow and ShowWebAppSettings guards

## Stage

- stage: compile
- category: build-graph / missing-symbol
- files:
  - `chrome/browser/web_applications/web_app_install_params.h`
  - `chrome/browser/ui/chrome_pages.cc`
  - `chrome/browser/ui/chrome_pages.h`
- downstream: `chrome/browser/ui/browser_command_controller.cc`

## Failure signature

```text
../../chrome/browser/ui/browser_command_controller.cc:1002:49: error: no member named 'kCreateShortcut' in 'web_app::WebAppInstallFlow'
 1002 |           browser_, web_app::WebAppInstallFlow::kCreateShortcut);
../../chrome/browser/ui/browser_command_controller.cc:1281:7: error: use of undeclared identifier 'ShowWebAppSettings'
 1281 |       ShowWebAppSettings(browser_, browser_->app_controller()->app_id(),
2 errors generated.
```

## Root cause

`browser_command_controller.cc` references two desktop-only symbols
unconditionally:

1. `web_app::WebAppInstallFlow::kCreateShortcut`, gated by
   `BUILDFLAG(IS_CHROMEOS)` in `web_app_install_params.h`. The CrOS DIY
   "Create Shortcut" flow is reused on QNX in the same way as other
   desktop web app platforms.
2. `chrome::ShowWebAppSettings(Browser*, std::string,
   web_app::AppSettingsPageEntryPoint)`, declared and defined under
   `BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)`.

Both guards excluded QNX even though the call sites did not.

## Fix

Extend both guards to allow QNX:

```cpp
// web_app_install_params.h
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_QNX)
  kCreateShortcut,
#endif

// chrome_pages.h / chrome_pages.cc
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
void ShowWebAppSettings(Browser* browser, ...);
#endif
```

`chrome_pages.h` previously forward-declared
`web_app::AppSettingsPageEntryPoint` under the same guard. Once QNX
needs the full type for the declaration to be visible, swap the
forward declaration for an include of
`chrome/browser/web_applications/web_app_utils.h` (which provides the
complete `enum class AppSettingsPageEntryPoint`).

This mirrors the rationale used by `web_app_install_info_qnx` and
`web_app_dialogs_qnx`.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/ui/ui/browser_command_controller.o
```

Result: `browser_command_controller.o` compiles successfully.

## Search hints

```bash
rg -n "kCreateShortcut|ShowWebAppSettings|WebAppInstallFlow|browser_command_controller" docs/qnx/history/build-errors/compile
```
