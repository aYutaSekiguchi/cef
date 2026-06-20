# QNX web app OS integration link gaps

## Failure signature

Stage: link
Category: build-graph / platform-guard
Target: `//cef:cefsimple` via `./libcef.so`

After the content child process launcher fix, the first unresolved Web App symbols were:

```text
./libcef.so: undefined reference to `web_app::FileHandlingIconsSupportedByOs()'
./libcef.so: undefined reference to `web_app::MaskIconOnOs(SkBitmap, base::OnceCallback<void (SkBitmap)>)'
./libcef.so: undefined reference to `web_app::UnregisterFileHandlersWithOs(...)'
./libcef.so: undefined reference to `web_app::ShouldRegisterFileHandlersWithOs()'
./libcef.so: undefined reference to `web_app::startup::FinalizeWebAppLaunch(...)'
./libcef.so: undefined reference to `web_app::startup::MaybeHandleWebAppLaunch(...)'
./libcef.so: undefined reference to `web_app::RegisterFileHandlersWithOs(...)'
```

## Root cause

QNX reaches desktop web app manager/startup code, but several OS integration implementation sources were not compiled for QNX:

- file handler registration functions are Linux/Win/Mac-specific; Linux uses `xdg-mime` and is not a good QNX dependency.
- `icons/icon_masker.cc` provides a generic passthrough implementation for Linux/Win only.
- `chrome/browser/ui/startup/web_app_startup_utils.cc` was gated to Win/Mac/Linux/ChromeOS.

## Fix

Files/patches:

- New file: `cef/patch/qnx/chromium/new_files/chrome/browser/web_applications/os_integration/web_app_file_handler_registration_qnx.cc`
- `cef/patch/patches/qnx/chromium/web_app_file_handler_qnx.patch`
- `cef/patch/patches/qnx/chromium/web_app_icon_masker_qnx.patch`
- `cef/patch/patches/qnx/chromium/web_app_startup_utils_qnx.patch`

The QNX file handler implementation is a no-op/unsupported backend:

```cpp
bool ShouldRegisterFileHandlersWithOs() { return false; }
bool FileHandlingIconsSupportedByOs() { return false; }
```

and registration/unregistration callbacks complete with `Result::kError`.

The icon masker guard is extended to QNX, reusing the generic passthrough implementation. Startup Web App utilities are compiled for QNX.

## Verification

After GN regeneration, `out/qnx_release/obj/cef/libcef.ninja` contains:

```text
web_app_file_handler_registration_qnx.o
icon_masker.o
web_app_startup_utils.o
```

## Search hints

```bash
rg -n "FileHandlingIconsSupportedByOs|MaskIconOnOs|web_app_startup_utils_qnx|web_app_file_handler_qnx|web_app_icon_masker_qnx" docs/qnx/history/build-errors
```
